#pragma once

#include "Dialect.h"
#include "TypeMapper.h"
#include "Column.h"
#include "RowView.h"
#include "Expression.h"
#include "Relations.h"
#include "Connection.h"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <stdexcept>
#include <concepts>
#include <functional>
#include <unordered_map>
#include <span>

#if __has_include(<glaze/glaze.hpp>)
#include <glaze/glaze.hpp>
#endif

namespace aegon::data::orm::sql {

template <typename ParentEntity, typename TargetEntity>
class ManyToManyThrough;

template <typename Entity>
class TableDef {
    std::string table_name_;
    std::string primary_key_name_{"id"};
    std::vector<ColumnMetadata> columns_;
    std::vector<std::function<void(Entity&, const RowView&, size_t)>> hydrators_;
    std::vector<std::function<std::string(const Entity&)>> extractors_;
    std::vector<std::pair<size_t, std::string>> offset_to_col_name_;

public:
    struct RelationDescriptor {
        RelationKind kind;
        size_t member_offset{0};
        std::string rel_name;
        std::string target_table;
        std::function<core::Task<void>(std::span<Entity>, Connection&, DatabaseDialect)> eager_loader;
        std::function<void(Entity&, const std::string&)> install_lazy_loader;
    };

private:
    std::vector<RelationDescriptor> relations_;
    std::function<std::string(const Entity&)> pk_extractor_;

    ColumnMetadata& current_col() {
        if (columns_.empty()) {
            throw std::runtime_error("TableDef: cannot configure column attributes before adding a column.");
        }
        return columns_.back();
    }

    template <typename FieldType>
    static size_t calculate_offset(FieldType Entity::* ptr) noexcept {
        alignas(Entity) std::byte storage[sizeof(Entity)];
        const Entity* e = reinterpret_cast<const Entity*>(storage);
        return static_cast<size_t>(
            reinterpret_cast<const char*>(&(e->*ptr)) - reinterpret_cast<const char*>(e)
        );
    }

public:
    explicit TableDef(std::string name) : table_name_(std::move(name)) {}

    [[nodiscard]] const std::string& table_name() const noexcept { return table_name_; }
    [[nodiscard]] const std::string& primary_key_name() const noexcept { return primary_key_name_; }
    [[nodiscard]] const std::vector<ColumnMetadata>& columns() const noexcept { return columns_; }
    [[nodiscard]] std::vector<ColumnMetadata>& columns() noexcept { return columns_; }

    template <typename FieldType>
    TableDef& id(FieldType Entity::* ptr, std::string col_name = "id") {
        primary_key_name_ = col_name.empty() ? "id" : col_name;

        pk_extractor_ = [ptr](const Entity& e) -> std::string {
            return format_param_value(e.*ptr);
        };

        ColumnMetadata meta;
        meta.column_name = primary_key_name_;
        meta.is_primary_key = true;
        meta.is_nullable = false;
        meta.sql_type_fn = [](DatabaseDialect d, size_t len) -> std::string {
            return TypeMapper<FieldType>::column_type(d, len);
        };

        if constexpr (std::is_integral_v<FieldType> && !std::is_same_v<FieldType, bool>) {
            meta.is_auto_increment = true;
        }

        size_t offset = calculate_offset(ptr);
        meta.member_offset = offset;
        offset_to_col_name_.emplace_back(offset, primary_key_name_);

        hydrators_.push_back([ptr](Entity& e, const RowView& row, size_t col_idx) {
            e.*ptr = row.get<FieldType>(col_idx);
        });

        extractors_.push_back([ptr](const Entity& e) -> std::string {
            return format_param_value(e.*ptr);
        });

        columns_.push_back(std::move(meta));
        return *this;
    }

    template <typename FieldType>
    TableDef& column(FieldType Entity::* ptr, std::string col_name = "") {
        ColumnMetadata meta;
        meta.column_name = std::move(col_name);
        meta.is_nullable = TypeMapper<FieldType>::is_nullable;
        meta.sql_type_fn = [](DatabaseDialect d, size_t len) -> std::string {
            return TypeMapper<FieldType>::column_type(d, len);
        };

        size_t offset = calculate_offset(ptr);
        meta.member_offset = offset;
        offset_to_col_name_.emplace_back(offset, meta.column_name);

        hydrators_.push_back([ptr](Entity& e, const RowView& row, size_t col_idx) {
            e.*ptr = row.get<FieldType>(col_idx);
        });

        extractors_.push_back([ptr](const Entity& e) -> std::string {
            return format_param_value(e.*ptr);
        });

        columns_.push_back(std::move(meta));
        return *this;
    }

    TableDef& unique(bool val = true) {
        current_col().is_unique = val;
        return *this;
    }

    TableDef& indexed(bool val = true) {
        current_col().is_indexed = val;
        return *this;
    }

    TableDef& length(size_t len) {
        current_col().length = len;
        return *this;
    }

    TableDef& default_value(std::string val) {
        current_col().default_value = std::move(val);
        return *this;
    }

    TableDef& default_value(const char* val) {
        current_col().default_value = std::string(val);
        return *this;
    }

    template <typename T>
    requires std::is_arithmetic_v<T>
    TableDef& default_value(T val) {
        current_col().default_value = std::to_string(val);
        return *this;
    }

    TableDef& nullable(bool val = true) {
        current_col().is_nullable = val;
        return *this;
    }

    TableDef& not_null() {
        current_col().is_nullable = false;
        return *this;
    }

    template <typename TargetEntity, typename TargetField>
    TableDef& references(TargetField TargetEntity::* /*target_ptr*/) {
        ForeignKeyMetadata fk;
        auto target_schema = TargetEntity::schema();
        fk.target_table = target_schema.table_name();
        fk.target_column = target_schema.primary_key_name();
        current_col().foreign_key = std::move(fk);
        return *this;
    }

    TableDef& on_delete(OnDeleteAction action) {
        if (current_col().foreign_key) {
            current_col().foreign_key->on_delete = action;
        }
        return *this;
    }

    TableDef& on_delete_cascade() { return on_delete(OnDeleteAction::Cascade); }
    TableDef& on_delete_set_null() { return on_delete(OnDeleteAction::SetNull); }
    TableDef& on_delete_restrict() { return on_delete(OnDeleteAction::Restrict); }

    TableDef& on_update_cascade() {
        if (current_col().foreign_key) {
            current_col().foreign_key->on_update = OnDeleteAction::Cascade;
        }
        return *this;
    }

    TableDef& created_at(types::DateTime Entity::* ptr, std::string col_name = "created_at") {
        column(ptr, col_name.empty() ? "created_at" : col_name);
        current_col().is_created_at = true;
        current_col().is_nullable = false;
        return *this;
    }

    TableDef& updated_at(types::DateTime Entity::* ptr, std::string col_name = "updated_at") {
        column(ptr, col_name.empty() ? "updated_at" : col_name);
        current_col().is_updated_at = true;
        current_col().is_nullable = false;
        return *this;
    }

    template <typename FieldType>
    [[nodiscard]] std::string resolve_column_name(FieldType Entity::* ptr) const {
        size_t offset = calculate_offset(ptr);
        for (const auto& [off, name] : offset_to_col_name_) {
            if (off == offset) {
                return name;
            }
        }
        return "";
    }

    [[nodiscard]] Entity map_row(const RowView& row) const {
        Entity e{};
        for (size_t i = 0; i < hydrators_.size() && i < row.column_count(); ++i) {
            hydrators_[i](e, row, i);
        }
        if (pk_extractor_) {
            std::string pk_val = pk_extractor_(e);
            for (const auto& rel : relations_) {
                if (rel.install_lazy_loader) {
                    rel.install_lazy_loader(e, pk_val);
                }
            }
        }
        return e;
    }

    // 1:1 Relation
    template <typename ChildEntity, typename ForeignKeyField>
    TableDef& has_one(HasOne<ChildEntity> Entity::* rel_ptr, ForeignKeyField ChildEntity::* fk_ptr, std::string rel_name = "") {
        RelationDescriptor desc;
        desc.kind = RelationKind::OneToOne;
        desc.member_offset = calculate_offset(rel_ptr);
        desc.rel_name = rel_name.empty() ? ChildEntity::schema().table_name() : std::move(rel_name);
        desc.target_table = ChildEntity::schema().table_name();

        desc.install_lazy_loader = [rel_ptr, fk_ptr](Entity& e, const std::string& owner_key) {
            (e.*rel_ptr).set_loader(owner_key, [fk_ptr](Connection& conn, const std::string& pk_val) -> core::Task<std::optional<ChildEntity>> {
                auto child_schema = ChildEntity::schema();
                auto dialect = conn.dialect();
                std::string fk_col = child_schema.resolve_column_name(fk_ptr);

                std::string sql = "SELECT ";
                for (size_t i = 0; i < child_schema.columns().size(); ++i) {
                    if (i > 0) sql.append(", ");
                    sql.append(DialectTraits::quote_identifier(dialect, child_schema.columns()[i].column_name));
                }
                sql.append(" FROM ");
                sql.append(DialectTraits::quote_identifier(dialect, child_schema.table_name()));
                sql.append(" WHERE ");
                sql.append(DialectTraits::quote_identifier(dialect, fk_col));
                sql.append(" = ");
                std::string p;
                DialectTraits::format_placeholder(dialect, 1, p);
                sql.append(p);
                sql.append(" LIMIT 1;");

                auto rows = co_await conn.query(sql, {pk_val});
                if (rows.empty()) co_return std::nullopt;
                co_return child_schema.map_row(rows[0]);
            });
        };

        desc.eager_loader = [rel_ptr, fk_ptr, pk_ext = pk_extractor_](std::span<Entity> parents, Connection& conn, DatabaseDialect dialect) -> core::Task<void> {
            if (parents.empty()) co_return;

            auto child_schema = ChildEntity::schema();
            std::string fk_col = child_schema.resolve_column_name(fk_ptr);

            std::vector<std::string> parent_pks;
            std::unordered_map<std::string, std::vector<Entity*>> parent_map;

            for (auto& p : parents) {
                (p.*rel_ptr).set_null();
                std::string pk_str = (p.*rel_ptr).owner_key();
                if (pk_str.empty() && pk_ext) {
                    pk_str = pk_ext(p);
                }
                if (!pk_str.empty()) {
                    parent_pks.push_back(pk_str);
                    parent_map[pk_str].push_back(&p);
                }
            }

            if (parent_pks.empty()) co_return;

            std::string sql = "SELECT ";
            for (size_t i = 0; i < child_schema.columns().size(); ++i) {
                if (i > 0) sql.append(", ");
                sql.append(DialectTraits::quote_identifier(dialect, child_schema.columns()[i].column_name));
            }
            sql.append(" FROM ");
            sql.append(DialectTraits::quote_identifier(dialect, child_schema.table_name()));
            sql.append(" WHERE ");
            sql.append(DialectTraits::quote_identifier(dialect, fk_col));
            sql.append(" IN (");

            std::vector<std::string> params;
            for (size_t i = 0; i < parent_pks.size(); ++i) {
                std::string p;
                DialectTraits::format_placeholder(dialect, i + 1, p);
                sql.append(p);
                if (i + 1 < parent_pks.size()) sql.append(", ");
                params.push_back(parent_pks[i]);
            }
            sql.append(");");

            auto rows = co_await conn.query(sql, params);
            for (const auto& r : rows) {
                ChildEntity child = child_schema.map_row(r);
                std::string fk_val = format_param_value(child.*fk_ptr);
                auto it = parent_map.find(fk_val);
                if (it != parent_map.end()) {
                    for (auto* parent_ptr : it->second) {
                        (parent_ptr->*rel_ptr).set_value(child);
                    }
                }
            }
        };

        relations_.push_back(std::move(desc));
        return *this;
    }

    // 1:N Relation
    template <typename ChildEntity, typename ForeignKeyField>
    TableDef& has_many(HasMany<ChildEntity> Entity::* rel_ptr, ForeignKeyField ChildEntity::* fk_ptr, std::string rel_name = "") {
        RelationDescriptor desc;
        desc.kind = RelationKind::OneToMany;
        desc.member_offset = calculate_offset(rel_ptr);
        desc.rel_name = rel_name.empty() ? ChildEntity::schema().table_name() : std::move(rel_name);
        desc.target_table = ChildEntity::schema().table_name();

        desc.install_lazy_loader = [rel_ptr, fk_ptr](Entity& e, const std::string& owner_key) {
            (e.*rel_ptr).set_loader(owner_key, [fk_ptr](Connection& conn, const std::string& pk_val) -> core::Task<std::vector<ChildEntity>> {
                auto child_schema = ChildEntity::schema();
                auto dialect = conn.dialect();
                std::string fk_col = child_schema.resolve_column_name(fk_ptr);

                std::string sql = "SELECT ";
                for (size_t i = 0; i < child_schema.columns().size(); ++i) {
                    if (i > 0) sql.append(", ");
                    sql.append(DialectTraits::quote_identifier(dialect, child_schema.columns()[i].column_name));
                }
                sql.append(" FROM ");
                sql.append(DialectTraits::quote_identifier(dialect, child_schema.table_name()));
                sql.append(" WHERE ");
                sql.append(DialectTraits::quote_identifier(dialect, fk_col));
                sql.append(" = ");
                std::string p;
                DialectTraits::format_placeholder(dialect, 1, p);
                sql.append(p);
                sql.push_back(';');

                auto rows = co_await conn.query(sql, {pk_val});
                std::vector<ChildEntity> results;
                results.reserve(rows.size());
                for (const auto& r : rows) {
                    results.push_back(child_schema.map_row(r));
                }
                co_return results;
            });
        };

        desc.eager_loader = [rel_ptr, fk_ptr, pk_ext = pk_extractor_](std::span<Entity> parents, Connection& conn, DatabaseDialect dialect) -> core::Task<void> {
            if (parents.empty()) co_return;

            auto child_schema = ChildEntity::schema();
            std::string fk_col = child_schema.resolve_column_name(fk_ptr);

            std::vector<std::string> parent_pks;
            std::unordered_map<std::string, std::vector<Entity*>> parent_map;

            for (auto& p : parents) {
                (p.*rel_ptr).set_loaded(true);
                std::string pk_str = (p.*rel_ptr).owner_key();
                if (pk_str.empty() && pk_ext) {
                    pk_str = pk_ext(p);
                }
                if (!pk_str.empty()) {
                    parent_pks.push_back(pk_str);
                    parent_map[pk_str].push_back(&p);
                }
            }

            if (parent_pks.empty()) co_return;

            std::string sql = "SELECT ";
            for (size_t i = 0; i < child_schema.columns().size(); ++i) {
                if (i > 0) sql.append(", ");
                sql.append(DialectTraits::quote_identifier(dialect, child_schema.columns()[i].column_name));
            }
            sql.append(" FROM ");
            sql.append(DialectTraits::quote_identifier(dialect, child_schema.table_name()));
            sql.append(" WHERE ");
            sql.append(DialectTraits::quote_identifier(dialect, fk_col));
            sql.append(" IN (");

            std::vector<std::string> params;
            for (size_t i = 0; i < parent_pks.size(); ++i) {
                std::string p;
                DialectTraits::format_placeholder(dialect, i + 1, p);
                sql.append(p);
                if (i + 1 < parent_pks.size()) sql.append(", ");
                params.push_back(parent_pks[i]);
            }
            sql.append(");");

            auto rows = co_await conn.query(sql, params);
            for (const auto& r : rows) {
                ChildEntity child = child_schema.map_row(r);
                std::string fk_val = format_param_value(child.*fk_ptr);
                auto it = parent_map.find(fk_val);
                if (it != parent_map.end()) {
                    for (auto* parent_ptr : it->second) {
                        (parent_ptr->*rel_ptr).push_back(child);
                    }
                }
            }
        };

        relations_.push_back(std::move(desc));
        return *this;
    }

    // N:M Relation Builder Entrypoint
    template <typename TargetEntity>
    ManyToManyThrough<Entity, TargetEntity> has_many(HasMany<TargetEntity> Entity::* rel_ptr, std::string rel_name = "") {
        return ManyToManyThrough<Entity, TargetEntity>(*this, rel_ptr, std::move(rel_name));
    }

    template <typename TargetEntity, typename JunctionEntity, typename ParentFkField, typename ChildFkField>
    void add_many_to_many(HasMany<TargetEntity> Entity::* rel_ptr,
                          ParentFkField JunctionEntity::* parent_fk,
                          ChildFkField JunctionEntity::* child_fk,
                          std::string rel_name = "") {
        RelationDescriptor desc;
        desc.kind = RelationKind::ManyToMany;
        desc.member_offset = calculate_offset(rel_ptr);
        desc.rel_name = rel_name.empty() ? TargetEntity::schema().table_name() : std::move(rel_name);
        desc.target_table = TargetEntity::schema().table_name();

        desc.install_lazy_loader = [rel_ptr, parent_fk, child_fk](Entity& e, const std::string& owner_key) {
            (e.*rel_ptr).set_loader(owner_key, [parent_fk, child_fk](Connection& conn, const std::string& pk_val) -> core::Task<std::vector<TargetEntity>> {
                auto junction_schema = JunctionEntity::schema();
                auto target_schema = TargetEntity::schema();
                auto dialect = conn.dialect();

                std::string junction_tbl = junction_schema.table_name();
                std::string target_tbl = target_schema.table_name();
                std::string parent_fk_col = junction_schema.resolve_column_name(parent_fk);
                std::string child_fk_col = junction_schema.resolve_column_name(child_fk);
                std::string target_pk_col = target_schema.primary_key_name();

                std::string sql = "SELECT ";
                for (size_t i = 0; i < target_schema.columns().size(); ++i) {
                    if (i > 0) sql.append(", ");
                    sql.append(DialectTraits::quote_identifier(dialect, "t"));
                    sql.push_back('.');
                    sql.append(DialectTraits::quote_identifier(dialect, target_schema.columns()[i].column_name));
                }
                sql.append(" FROM ");
                sql.append(DialectTraits::quote_identifier(dialect, target_tbl));
                sql.append(" ");
                sql.append(DialectTraits::quote_identifier(dialect, "t"));
                sql.append(" INNER JOIN ");
                sql.append(DialectTraits::quote_identifier(dialect, junction_tbl));
                sql.append(" ");
                sql.append(DialectTraits::quote_identifier(dialect, "j"));
                sql.append(" ON ");
                sql.append(DialectTraits::quote_identifier(dialect, "j"));
                sql.push_back('.');
                sql.append(DialectTraits::quote_identifier(dialect, child_fk_col));
                sql.append(" = ");
                sql.append(DialectTraits::quote_identifier(dialect, "t"));
                sql.push_back('.');
                sql.append(DialectTraits::quote_identifier(dialect, target_pk_col));
                sql.append(" WHERE ");
                sql.append(DialectTraits::quote_identifier(dialect, "j"));
                sql.push_back('.');
                sql.append(DialectTraits::quote_identifier(dialect, parent_fk_col));
                sql.append(" = ");
                std::string p;
                DialectTraits::format_placeholder(dialect, 1, p);
                sql.append(p);
                sql.push_back(';');

                auto rows = co_await conn.query(sql, {pk_val});
                std::vector<TargetEntity> results;
                results.reserve(rows.size());
                for (const auto& r : rows) {
                    results.push_back(target_schema.map_row(r));
                }
                co_return results;
            });
        };

        desc.eager_loader = [rel_ptr, parent_fk, child_fk, pk_ext = pk_extractor_](std::span<Entity> parents, Connection& conn, DatabaseDialect dialect) -> core::Task<void> {
            if (parents.empty()) co_return;

            auto junction_schema = JunctionEntity::schema();
            auto target_schema = TargetEntity::schema();

            std::string junction_tbl = junction_schema.table_name();
            std::string target_tbl = target_schema.table_name();
            std::string parent_fk_col = junction_schema.resolve_column_name(parent_fk);
            std::string child_fk_col = junction_schema.resolve_column_name(child_fk);
            std::string target_pk_col = target_schema.primary_key_name();

            std::vector<std::string> parent_pks;
            std::unordered_map<std::string, std::vector<Entity*>> parent_map;

            for (auto& p : parents) {
                (p.*rel_ptr).set_loaded(true);
                std::string pk_str = (p.*rel_ptr).owner_key();
                if (pk_str.empty() && pk_ext) {
                    pk_str = pk_ext(p);
                }
                if (!pk_str.empty()) {
                    parent_pks.push_back(pk_str);
                    parent_map[pk_str].push_back(&p);
                }
            }

            if (parent_pks.empty()) co_return;

            std::string sql = "SELECT ";
            sql.append(DialectTraits::quote_identifier(dialect, "j"));
            sql.push_back('.');
            sql.append(DialectTraits::quote_identifier(dialect, parent_fk_col));

            for (const auto& col : target_schema.columns()) {
                sql.append(", ");
                sql.append(DialectTraits::quote_identifier(dialect, "t"));
                sql.push_back('.');
                sql.append(DialectTraits::quote_identifier(dialect, col.column_name));
            }

            sql.append(" FROM ");
            sql.append(DialectTraits::quote_identifier(dialect, target_tbl));
            sql.append(" ");
            sql.append(DialectTraits::quote_identifier(dialect, "t"));
            sql.append(" INNER JOIN ");
            sql.append(DialectTraits::quote_identifier(dialect, junction_tbl));
            sql.append(" ");
            sql.append(DialectTraits::quote_identifier(dialect, "j"));
            sql.append(" ON ");
            sql.append(DialectTraits::quote_identifier(dialect, "j"));
            sql.push_back('.');
            sql.append(DialectTraits::quote_identifier(dialect, child_fk_col));
            sql.append(" = ");
            sql.append(DialectTraits::quote_identifier(dialect, "t"));
            sql.push_back('.');
            sql.append(DialectTraits::quote_identifier(dialect, target_pk_col));

            sql.append(" WHERE ");
            sql.append(DialectTraits::quote_identifier(dialect, "j"));
            sql.push_back('.');
            sql.append(DialectTraits::quote_identifier(dialect, parent_fk_col));
            sql.append(" IN (");

            std::vector<std::string> params;
            for (size_t i = 0; i < parent_pks.size(); ++i) {
                std::string p;
                DialectTraits::format_placeholder(dialect, i + 1, p);
                sql.append(p);
                if (i + 1 < parent_pks.size()) sql.append(", ");
                params.push_back(parent_pks[i]);
            }
            sql.append(");");

            auto rows = co_await conn.query(sql, params);
            for (const auto& r : rows) {
                std::string parent_id_val = std::string(r.get_raw(0));
                OffsetRowView offset_row(r, 1);
                TargetEntity target = target_schema.map_row(offset_row);
                auto it = parent_map.find(parent_id_val);
                if (it != parent_map.end()) {
                    for (auto* parent_ptr : it->second) {
                        (parent_ptr->*rel_ptr).push_back(target);
                    }
                }
            }
        };

        relations_.push_back(std::move(desc));
    }

    template <typename TargetField>
    [[nodiscard]] const RelationDescriptor* find_relation(TargetField Entity::* ptr) const {
        size_t offset = calculate_offset(ptr);
        for (const auto& rel : relations_) {
            if (rel.member_offset == offset) {
                return &rel;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const std::vector<RelationDescriptor>& relations() const noexcept {
        return relations_;
    }

    [[nodiscard]] std::vector<std::pair<std::string, std::string>> extract_values(const Entity& entity, bool skip_auto_inc = true) const {
        std::vector<std::pair<std::string, std::string>> result;
        result.reserve(columns_.size());
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (skip_auto_inc && columns_[i].is_auto_increment) {
                continue;
            }
            result.emplace_back(columns_[i].column_name, extractors_[i](entity));
        }
        return result;
    }
};

template <typename ParentEntity, typename TargetEntity>
class ManyToManyThrough {
    TableDef<ParentEntity>& table_;
    HasMany<TargetEntity> ParentEntity::* rel_ptr_;
    std::string rel_name_;

public:
    ManyToManyThrough(TableDef<ParentEntity>& table,
                      HasMany<TargetEntity> ParentEntity::* rel_ptr,
                      std::string rel_name)
        : table_(table), rel_ptr_(rel_ptr), rel_name_(std::move(rel_name)) {}

    template <typename JunctionEntity, typename ParentFkField, typename ChildFkField>
    TableDef<ParentEntity>& through(ParentFkField JunctionEntity::* parent_fk, ChildFkField JunctionEntity::* child_fk) {
        table_.add_many_to_many(rel_ptr_, parent_fk, child_fk, rel_name_);
        return table_;
    }
};

template <typename Entity>
inline TableDef<Entity> table(std::string name) {
    return TableDef<Entity>(std::move(name));
}

} // namespace aegon::data::orm::sql
