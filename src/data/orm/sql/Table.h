#pragma once

#include "Dialect.h"
#include "TypeMapper.h"
#include "Column.h"
#include "RowView.h"
#include "Expression.h"
#include "Relations.h"
#include "Connection.h"
#include "QueryResult.h"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <stdexcept>
#include <concepts>
#include <functional>
#include <unordered_map>
#include <span>
#include <algorithm>

#if __has_include(<glaze/glaze.hpp>)
#include <glaze/glaze.hpp>
#endif

namespace aegon::data::orm::sql {

enum class InvalidationMode {
    StrictEpoch,       // Increments table epoch on insert/delete (O(1))
    Partitioned,       // Increments partition-specific epoch (scoped to parent/tenant ID)
    PredicateAware,    // Checks if mutation matches cached WHERE conditions
    TtlOnly            // Time-decay only; writes do NOT invalidate query cache
};

enum class MutationSync {
    EvictOnWrite,      // Delete entity key on update (re-fetched on next read)
    UpdateOnWrite      // Overwrite entity key with new serialized data on update
};

template <typename Entity>
struct TableCacheConfig {
    std::chrono::seconds ttl{300};
    bool enabled{false};
    bool by_id{true};
    std::optional<std::string> unique_column;
    std::function<std::string(const Entity&)> unique_extractor;
    std::optional<std::string> partition_column;
    std::function<std::string(const Entity&)> partition_extractor;
    InvalidationMode invalidation{InvalidationMode::StrictEpoch};
    MutationSync mutation_sync{MutationSync::EvictOnWrite};
};

template <typename ParentEntity, typename TargetEntity>
class ManyToManyThrough;

template <typename Entity>
class TableDef {
    TableCacheConfig<Entity> cache_config_{};
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
        std::function<core::Task<void>(Entity&, Connection&, DatabaseDialect, const std::string&)> tree_inserter;
        std::function<std::string(DatabaseDialect, bool, const std::string&, const std::vector<Condition>&, size_t&, std::vector<std::string>&)> exists_builder;
        std::function<core::Task<void>(std::span<Entity>, Connection&, DatabaseDialect, const std::vector<Condition>&, const std::vector<OrderByClause>&, std::optional<size_t>)> scoped_eager_loader;
    };

private:
    std::vector<RelationDescriptor> relations_;
    std::function<std::string(const Entity&)> pk_extractor_;
    std::function<void(Entity&, std::string_view)> pk_setter_;
    bool has_version_{false};
    std::string version_column_{"version"};
    std::function<int64_t(const Entity&)> version_extractor_;
    std::function<void(Entity&, int64_t)> version_setter_;

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

        pk_setter_ = [ptr](Entity& e, std::string_view val) {
            e.*ptr = parse_field_value<FieldType>(val);
        };

        columns_.push_back(std::move(meta));
        return *this;
    }

    [[nodiscard]] std::string get_primary_key(const Entity& entity) const {
        if (pk_extractor_) {
            return pk_extractor_(entity);
        }
        return "";
    }

    void set_primary_key(Entity& entity, std::string_view val) const {
        if (pk_setter_) {
            pk_setter_(entity, val);
        }
    }

    void set_primary_key(Entity& entity, int64_t val) const {
        if (pk_setter_) {
            pk_setter_(entity, std::to_string(val));
        }
    }

    void init_relations(Entity& entity) const {
        std::string pk_val = get_primary_key(entity);
        for (const auto& rel : relations_) {
            if (rel.install_lazy_loader) {
                rel.install_lazy_loader(entity, pk_val);
            }
        }
    }

    [[nodiscard]] bool has_auto_increment_pk() const noexcept {
        for (const auto& col : columns_) {
            if (col.is_auto_increment) return true;
        }
        return false;
    }

    QueryResult build_insert_sql(const Entity& entity, DatabaseDialect dialect) const {
        QueryResult result;
        std::string& sql = result.sql;
        sql.reserve(256);

        sql.append("INSERT INTO ");
        sql.append(DialectTraits::quote_identifier(dialect, table_name_));
        sql.append(" (");

        std::vector<size_t> insert_col_indices;
        bool auto_inc = false;

        for (size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i].is_auto_increment) {
                auto_inc = true;
                continue;
            }
            insert_col_indices.push_back(i);
        }

        for (size_t i = 0; i < insert_col_indices.size(); ++i) {
            sql.append(DialectTraits::quote_identifier(dialect, columns_[insert_col_indices[i]].column_name));
            if (i + 1 < insert_col_indices.size()) sql.append(", ");
        }

        sql.append(") VALUES (");

        auto extracted = extract_values(entity, true);
        for (size_t col_i = 0; col_i < extracted.size(); ++col_i) {
            std::string p;
            DialectTraits::format_placeholder(dialect, col_i + 1, p);
            sql.append(p);
            if (col_i + 1 < extracted.size()) sql.append(", ");
            result.params.push_back(extracted[col_i].second);
        }
        sql.push_back(')');

        if ((dialect == DatabaseDialect::PostgreSQL || dialect == DatabaseDialect::SQLite) && auto_inc) {
            sql.append(" RETURNING ");
            sql.append(DialectTraits::quote_identifier(dialect, primary_key_name_));
        }

        sql.push_back(';');
        return result;
    }

    core::Task<int64_t> insert_entity(Entity& entity, Connection& conn) const {
        auto dialect = conn.dialect();
        auto query = build_insert_sql(entity, dialect);
        int64_t generated_id = 0;
        if ((dialect == DatabaseDialect::PostgreSQL || dialect == DatabaseDialect::SQLite) && has_auto_increment_pk()) {
            auto rows = co_await conn.query(query.sql, query.params);
            if (!rows.empty()) {
                generated_id = rows[0].template get<int64_t>(0);
                if (generated_id > 0) {
                    set_primary_key(entity, generated_id);
                }
            }
        } else {
            co_await conn.execute(query.sql, query.params);
        }
        init_relations(entity);
        co_return generated_id;
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

    template <typename FieldType>
    TableDef& version(FieldType Entity::* ptr, std::string col_name = "version") {
        static_assert(std::is_integral_v<FieldType>, "Version column must be an integral type");
        has_version_ = true;
        version_column_ = col_name.empty() ? "version" : col_name;

        version_extractor_ = [ptr](const Entity& e) -> int64_t {
            return static_cast<int64_t>(e.*ptr);
        };

        version_setter_ = [ptr](Entity& e, int64_t val) {
            e.*ptr = static_cast<FieldType>(val);
        };

        bool already_added = false;
        for (const auto& col : columns_) {
            if (col.column_name == version_column_) {
                already_added = true;
                break;
            }
        }
        if (!already_added) {
            column(ptr, version_column_);
        }
        return *this;
    }

    [[nodiscard]] bool has_version() const noexcept { return has_version_; }
    [[nodiscard]] const std::string& version_column() const noexcept { return version_column_; }
    [[nodiscard]] int64_t get_version(const Entity& entity) const {
        if (version_extractor_) return version_extractor_(entity);
        return 0;
    }
    void set_version(Entity& entity, int64_t val) const {
        if (version_setter_) version_setter_(entity, val);
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

    // --- Smart Cache Configuration ---
    TableDef& cache(TableCacheConfig<Entity> config) {
        cache_config_ = std::move(config);
        cache_config_.enabled = true;
        return *this;
    }

    TableDef& cache_by_id(std::chrono::seconds ttl = std::chrono::seconds(300)) {
        cache_config_.enabled = true;
        cache_config_.by_id = true;
        cache_config_.ttl = ttl;
        return *this;
    }

    template <typename FieldType>
    TableDef& by_unique(FieldType Entity::* ptr) {
        cache_config_.enabled = true;
        cache_config_.unique_column = resolve_column_name(ptr);
        cache_config_.unique_extractor = [ptr](const Entity& e) -> std::string {
            return format_param_value(e.*ptr);
        };
        return *this;
    }

    template <typename FieldType>
    TableDef& partition_by(FieldType Entity::* ptr) {
        cache_config_.enabled = true;
        cache_config_.partition_column = resolve_column_name(ptr);
        cache_config_.partition_extractor = [ptr](const Entity& e) -> std::string {
            return format_param_value(e.*ptr);
        };
        cache_config_.invalidation = InvalidationMode::Partitioned;
        return *this;
    }

    TableDef& invalidation_mode(InvalidationMode mode) {
        cache_config_.invalidation = mode;
        return *this;
    }

    TableDef& mutation_sync(MutationSync sync) {
        cache_config_.mutation_sync = sync;
        return *this;
    }

    [[nodiscard]] const TableCacheConfig<Entity>& cache_config() const noexcept {
        return cache_config_;
    }

    [[nodiscard]] bool is_cached() const noexcept {
        return cache_config_.enabled;
    }

    [[nodiscard]] std::string serialize_entity_json(const Entity& entity) const {
#if __has_include(<glaze/glaze.hpp>)
        if constexpr (requires(const Entity& e, std::string& s) { glz::write_json(e, s); }) {
            std::string out;
            if (glz::write_json(entity, out) == glz::error_code::none) {
                return out;
            }
        }
        std::unordered_map<std::string, std::string> map;
        map.reserve(columns_.size());
        for (size_t i = 0; i < columns_.size(); ++i) {
            map.emplace(columns_[i].column_name, extractors_[i](entity));
        }
        std::string out;
        if (glz::write_json(map, out) == glz::error_code::none) {
            return out;
        }
#endif
        return "{}";
    }

    [[nodiscard]] bool deserialize_entity_json(std::string_view json, Entity& out) const {
#if __has_include(<glaze/glaze.hpp>)
        if constexpr (requires(Entity& e, std::string_view s) { glz::read_json(e, s); }) {
            if (glz::read_json(out, json) == glz::error_code::none) {
                init_relations(out);
                return true;
            }
        }
        glz::generic doc;
        if (glz::read_json(doc, json) == glz::error_code::none) {
            std::vector<std::optional<std::string>> col_vals(columns_.size());
            for (size_t i = 0; i < columns_.size(); ++i) {
                if (doc.contains(columns_[i].column_name)) {
                    const auto& val = doc[columns_[i].column_name];
                    if (val.is_null()) {
                        col_vals[i] = std::nullopt;
                    } else if (val.is_string()) {
                        col_vals[i] = std::string(val.get_string());
                    } else {
                        std::string s;
                        std::ignore = glz::write_json(val, s);
                        col_vals[i] = std::move(s);
                    }
                }
            }
            MockRowView mrow(std::move(col_vals));
            for (size_t i = 0; i < hydrators_.size(); ++i) {
                hydrators_[i](out, mrow, i);
            }
            init_relations(out);
            return true;
        }
#endif
        return false;
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
        init_relations(e);
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

            (e.*rel_ptr).set_mutators(
                [fk_ptr](Connection& conn, const std::string& owner_key, ChildEntity& val) -> core::Task<void> {
                    val.*fk_ptr = parse_field_value<ForeignKeyField>(owner_key);
                    auto dialect = conn.dialect();
                    auto child_schema = ChildEntity::schema();

                    std::string del_sql = "DELETE FROM ";
                    del_sql.append(DialectTraits::quote_identifier(dialect, child_schema.table_name()));
                    del_sql.append(" WHERE ");
                    del_sql.append(DialectTraits::quote_identifier(dialect, child_schema.resolve_column_name(fk_ptr)));
                    del_sql.append(" = ");
                    std::string p;
                    DialectTraits::format_placeholder(dialect, 1, p);
                    del_sql.append(p);
                    del_sql.push_back(';');
                    co_await conn.execute(del_sql, {owner_key});

                    co_await child_schema.insert_entity(val, conn);
                },
                [fk_ptr](Connection& conn, const std::string& owner_key) -> core::Task<void> {
                    auto dialect = conn.dialect();
                    auto child_schema = ChildEntity::schema();

                    std::string del_sql = "DELETE FROM ";
                    del_sql.append(DialectTraits::quote_identifier(dialect, child_schema.table_name()));
                    del_sql.append(" WHERE ");
                    del_sql.append(DialectTraits::quote_identifier(dialect, child_schema.resolve_column_name(fk_ptr)));
                    del_sql.append(" = ");
                    std::string p;
                    DialectTraits::format_placeholder(dialect, 1, p);
                    del_sql.append(p);
                    del_sql.push_back(';');
                    co_await conn.execute(del_sql, {owner_key});
                }
            );
        };

        desc.tree_inserter = [rel_ptr, fk_ptr](Entity& parent, Connection& conn, DatabaseDialect /*dialect*/, const std::string& parent_pk) -> core::Task<void> {
            auto& rel = parent.*rel_ptr;
            if (!rel.has_value()) co_return;
            ChildEntity& child = rel.value();
            child.*fk_ptr = parse_field_value<ForeignKeyField>(parent_pk);
            auto child_schema = ChildEntity::schema();
            co_await child_schema.insert_entity(child, conn);
        };

        desc.exists_builder = [fk_ptr, parent_pk = primary_key_name_](
            DatabaseDialect dialect, bool negate, const std::string& parent_tbl,
            const std::vector<Condition>& child_conditions, size_t& param_idx,
            std::vector<std::string>& out_params) -> std::string {
            auto child_schema = ChildEntity::schema();
            std::string child_tbl = child_schema.table_name();
            std::string fk_col = child_schema.resolve_column_name(fk_ptr);

            std::string sql = negate ? "NOT EXISTS (SELECT 1 FROM " : "EXISTS (SELECT 1 FROM ";
            sql.append(DialectTraits::quote_identifier(dialect, child_tbl));
            sql.append(" WHERE ");
            sql.append(DialectTraits::quote_identifier(dialect, child_tbl + "." + fk_col));
            sql.append(" = ");
            sql.append(DialectTraits::quote_identifier(dialect, parent_tbl + "." + parent_pk));

            if (!child_conditions.empty()) {
                sql.append(" AND (");
                compile_conditions(child_conditions, dialect, param_idx, sql, out_params);
                sql.push_back(')');
            }
            sql.push_back(')');
            return sql;
        };

        desc.scoped_eager_loader = [rel_ptr, fk_ptr, pk_ext = pk_extractor_](
            std::span<Entity> parents, Connection& conn, DatabaseDialect dialect,
            const std::vector<Condition>& child_conditions,
            const std::vector<OrderByClause>& order_bys,
            std::optional<size_t> limit) -> core::Task<void> {
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
            size_t param_idx = 1;
            for (size_t i = 0; i < parent_pks.size(); ++i) {
                std::string p;
                DialectTraits::format_placeholder(dialect, param_idx++, p);
                sql.append(p);
                if (i + 1 < parent_pks.size()) sql.append(", ");
                params.push_back(parent_pks[i]);
            }
            sql.push_back(')');

            if (!child_conditions.empty()) {
                sql.append(" AND (");
                compile_conditions(child_conditions, dialect, param_idx, sql, params);
                sql.push_back(')');
            }

            if (!order_bys.empty()) {
                sql.append(" ORDER BY ");
                for (size_t i = 0; i < order_bys.size(); ++i) {
                    if (i > 0) sql.append(", ");
                    sql.append(DialectTraits::quote_identifier(dialect, order_bys[i].column));
                    sql.push_back(' ');
                    sql.append(order_to_sql(order_bys[i].direction));
                }
            }

            sql.push_back(';');

            auto rows = co_await conn.query(sql, params);
            for (const auto& r : rows) {
                ChildEntity child = child_schema.map_row(r);
                std::string fk_val = format_param_value(child.*fk_ptr);
                auto it = parent_map.find(fk_val);
                if (it != parent_map.end()) {
                    for (auto* parent_ptr : it->second) {
                        if (!limit.has_value() || !(parent_ptr->*rel_ptr).has_value()) {
                            (parent_ptr->*rel_ptr).set_value(child);
                        }
                    }
                }
            }
        };

        desc.eager_loader = [loader = desc.scoped_eager_loader](std::span<Entity> parents, Connection& conn, DatabaseDialect dialect) -> core::Task<void> {
            co_await loader(parents, conn, dialect, {}, {}, std::nullopt);
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

            (e.*rel_ptr).set_one_to_many_mutators(
                [fk_ptr](Connection& conn, const std::string& owner_key, ChildEntity& item) -> core::Task<int64_t> {
                    item.*fk_ptr = parse_field_value<ForeignKeyField>(owner_key);
                    auto child_schema = ChildEntity::schema();
                    co_return co_await child_schema.insert_entity(item, conn);
                },
                [fk_ptr](Connection& conn, const std::string& owner_key, const std::string& item_id, std::vector<ChildEntity>& data) -> core::Task<bool> {
                    auto dialect = conn.dialect();
                    auto child_schema = ChildEntity::schema();
                    std::string fk_col = child_schema.resolve_column_name(fk_ptr);
                    std::string pk_col = child_schema.primary_key_name();

                    std::string del_sql = "DELETE FROM ";
                    del_sql.append(DialectTraits::quote_identifier(dialect, child_schema.table_name()));
                    del_sql.append(" WHERE ");
                    del_sql.append(DialectTraits::quote_identifier(dialect, fk_col));
                    del_sql.append(" = ");
                    std::string p1, p2;
                    DialectTraits::format_placeholder(dialect, 1, p1);
                    DialectTraits::format_placeholder(dialect, 2, p2);
                    del_sql.append(p1);
                    del_sql.append(" AND ");
                    del_sql.append(DialectTraits::quote_identifier(dialect, pk_col));
                    del_sql.append(" = ");
                    del_sql.append(p2);
                    del_sql.push_back(';');

                    size_t n = co_await conn.execute(del_sql, {owner_key, item_id});
                    if (n > 0) {
                        auto it = std::remove_if(data.begin(), data.end(), [&](const ChildEntity& c) {
                            return child_schema.get_primary_key(c) == item_id;
                        });
                        data.erase(it, data.end());
                    }
                    co_return n > 0;
                }
            );
        };

        desc.tree_inserter = [rel_ptr, fk_ptr](Entity& parent, Connection& conn, DatabaseDialect /*dialect*/, const std::string& parent_pk) -> core::Task<void> {
            auto& rel = parent.*rel_ptr;
            auto child_schema = ChildEntity::schema();
            for (auto& item : rel) {
                item.*fk_ptr = parse_field_value<ForeignKeyField>(parent_pk);
                co_await child_schema.insert_entity(item, conn);
            }
        };

        desc.exists_builder = [fk_ptr, parent_pk = primary_key_name_](
            DatabaseDialect dialect, bool negate, const std::string& parent_tbl,
            const std::vector<Condition>& child_conditions, size_t& param_idx,
            std::vector<std::string>& out_params) -> std::string {
            auto child_schema = ChildEntity::schema();
            std::string child_tbl = child_schema.table_name();
            std::string fk_col = child_schema.resolve_column_name(fk_ptr);

            std::string sql = negate ? "NOT EXISTS (SELECT 1 FROM " : "EXISTS (SELECT 1 FROM ";
            sql.append(DialectTraits::quote_identifier(dialect, child_tbl));
            sql.append(" WHERE ");
            sql.append(DialectTraits::quote_identifier(dialect, child_tbl + "." + fk_col));
            sql.append(" = ");
            sql.append(DialectTraits::quote_identifier(dialect, parent_tbl + "." + parent_pk));

            if (!child_conditions.empty()) {
                sql.append(" AND (");
                compile_conditions(child_conditions, dialect, param_idx, sql, out_params);
                sql.push_back(')');
            }
            sql.push_back(')');
            return sql;
        };

        desc.scoped_eager_loader = [rel_ptr, fk_ptr, pk_ext = pk_extractor_](
            std::span<Entity> parents, Connection& conn, DatabaseDialect dialect,
            const std::vector<Condition>& child_conditions,
            const std::vector<OrderByClause>& order_bys,
            std::optional<size_t> limit) -> core::Task<void> {
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
            size_t param_idx = 1;
            for (size_t i = 0; i < parent_pks.size(); ++i) {
                std::string p;
                DialectTraits::format_placeholder(dialect, param_idx++, p);
                sql.append(p);
                if (i + 1 < parent_pks.size()) sql.append(", ");
                params.push_back(parent_pks[i]);
            }
            sql.push_back(')');

            if (!child_conditions.empty()) {
                sql.append(" AND (");
                compile_conditions(child_conditions, dialect, param_idx, sql, params);
                sql.push_back(')');
            }

            if (!order_bys.empty()) {
                sql.append(" ORDER BY ");
                for (size_t i = 0; i < order_bys.size(); ++i) {
                    if (i > 0) sql.append(", ");
                    sql.append(DialectTraits::quote_identifier(dialect, order_bys[i].column));
                    sql.push_back(' ');
                    sql.append(order_to_sql(order_bys[i].direction));
                }
            }

            sql.push_back(';');

            auto rows = co_await conn.query(sql, params);
            for (const auto& r : rows) {
                ChildEntity child = child_schema.map_row(r);
                std::string fk_val = format_param_value(child.*fk_ptr);
                auto it = parent_map.find(fk_val);
                if (it != parent_map.end()) {
                    for (auto* parent_ptr : it->second) {
                        if (!limit.has_value() || (parent_ptr->*rel_ptr).size() < *limit) {
                            (parent_ptr->*rel_ptr).push_back(child);
                        }
                    }
                }
            }
        };

        desc.eager_loader = [loader = desc.scoped_eager_loader](std::span<Entity> parents, Connection& conn, DatabaseDialect dialect) -> core::Task<void> {
            co_await loader(parents, conn, dialect, {}, {}, std::nullopt);
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

            (e.*rel_ptr).set_many_to_many_mutators(
                [parent_fk, child_fk](Connection& conn, const std::string& owner_key, const TargetEntity& item, std::vector<TargetEntity>& data) -> core::Task<void> {
                    auto junction_schema = JunctionEntity::schema();
                    auto target_schema = TargetEntity::schema();
                    auto dialect = conn.dialect();

                    std::string child_pk_val = target_schema.get_primary_key(item);

                    std::string sql = "INSERT INTO ";
                    sql.append(DialectTraits::quote_identifier(dialect, junction_schema.table_name()));
                    sql.append(" (");
                    sql.append(DialectTraits::quote_identifier(dialect, junction_schema.resolve_column_name(parent_fk)));
                    sql.append(", ");
                    sql.append(DialectTraits::quote_identifier(dialect, junction_schema.resolve_column_name(child_fk)));
                    sql.append(") VALUES (");
                    std::string p1, p2;
                    DialectTraits::format_placeholder(dialect, 1, p1);
                    DialectTraits::format_placeholder(dialect, 2, p2);
                    sql.append(p1);
                    sql.append(", ");
                    sql.append(p2);
                    sql.append(");");

                    co_await conn.execute(sql, {owner_key, child_pk_val});
                    data.push_back(item);
                },
                [parent_fk, child_fk](Connection& conn, const std::string& owner_key, const std::string& item_id, std::vector<TargetEntity>& data) -> core::Task<bool> {
                    auto junction_schema = JunctionEntity::schema();
                    auto target_schema = TargetEntity::schema();
                    auto dialect = conn.dialect();

                    std::string sql = "DELETE FROM ";
                    sql.append(DialectTraits::quote_identifier(dialect, junction_schema.table_name()));
                    sql.append(" WHERE ");
                    sql.append(DialectTraits::quote_identifier(dialect, junction_schema.resolve_column_name(parent_fk)));
                    sql.append(" = ");
                    std::string p1, p2;
                    DialectTraits::format_placeholder(dialect, 1, p1);
                    DialectTraits::format_placeholder(dialect, 2, p2);
                    sql.append(p1);
                    sql.append(" AND ");
                    sql.append(DialectTraits::quote_identifier(dialect, junction_schema.resolve_column_name(child_fk)));
                    sql.append(" = ");
                    sql.append(p2);
                    sql.push_back(';');

                    size_t n = co_await conn.execute(sql, {owner_key, item_id});
                    if (n > 0) {
                        auto it = std::remove_if(data.begin(), data.end(), [&](const TargetEntity& t) {
                            return target_schema.get_primary_key(t) == item_id;
                        });
                        data.erase(it, data.end());
                    }
                    co_return n > 0;
                }
            );
        };

        desc.tree_inserter = [rel_ptr, parent_fk, child_fk](Entity& parent, Connection& conn, DatabaseDialect dialect, const std::string& parent_pk) -> core::Task<void> {
            auto& rel = parent.*rel_ptr;
            auto junction_schema = JunctionEntity::schema();
            auto target_schema = TargetEntity::schema();

            for (auto& target : rel) {
                std::string target_pk_val = target_schema.get_primary_key(target);
                if (target_pk_val.empty() || target_pk_val == "0") {
                    co_await target_schema.insert_entity(target, conn);
                    target_pk_val = target_schema.get_primary_key(target);
                }

                std::string sql = "INSERT INTO ";
                sql.append(DialectTraits::quote_identifier(dialect, junction_schema.table_name()));
                sql.append(" (");
                sql.append(DialectTraits::quote_identifier(dialect, junction_schema.resolve_column_name(parent_fk)));
                sql.append(", ");
                sql.append(DialectTraits::quote_identifier(dialect, junction_schema.resolve_column_name(child_fk)));
                sql.append(") VALUES (");
                std::string p1, p2;
                DialectTraits::format_placeholder(dialect, 1, p1);
                DialectTraits::format_placeholder(dialect, 2, p2);
                sql.append(p1);
                sql.append(", ");
                sql.append(p2);
                sql.append(");");

                co_await conn.execute(sql, {parent_pk, target_pk_val});
            }
        };

        desc.exists_builder = [parent_fk, child_fk, parent_pk = primary_key_name_](
            DatabaseDialect dialect, bool negate, const std::string& parent_tbl,
            const std::vector<Condition>& child_conditions, size_t& param_idx,
            std::vector<std::string>& out_params) -> std::string {
            auto junction_schema = JunctionEntity::schema();
            auto target_schema = TargetEntity::schema();

            std::string junction_tbl = junction_schema.table_name();
            std::string target_tbl = target_schema.table_name();
            std::string parent_fk_col = junction_schema.resolve_column_name(parent_fk);
            std::string child_fk_col = junction_schema.resolve_column_name(child_fk);
            std::string target_pk_col = target_schema.primary_key_name();

            std::string sql = negate ? "NOT EXISTS (" : "EXISTS (";
            sql.append("SELECT 1 FROM ");
            sql.append(DialectTraits::quote_identifier(dialect, target_tbl));
            sql.append(" ");
            sql.append(DialectTraits::quote_identifier(dialect, "t"));
            sql.append(" INNER JOIN ");
            sql.append(DialectTraits::quote_identifier(dialect, junction_tbl));
            sql.append(" ");
            sql.append(DialectTraits::quote_identifier(dialect, "j"));
            sql.append(" ON ");
            sql.append(DialectTraits::quote_identifier(dialect, "j." + child_fk_col));
            sql.append(" = ");
            sql.append(DialectTraits::quote_identifier(dialect, "t." + target_pk_col));
            sql.append(" WHERE ");
            sql.append(DialectTraits::quote_identifier(dialect, "j." + parent_fk_col));
            sql.append(" = ");
            sql.append(DialectTraits::quote_identifier(dialect, parent_tbl + "." + parent_pk));

            if (!child_conditions.empty()) {
                sql.append(" AND (");
                std::vector<Condition> qualified = child_conditions;
                for (auto& c : qualified) {
                    if (!c.custom_compiler && c.column.find('.') == std::string::npos) {
                        c.column = "t." + c.column;
                    }
                }
                compile_conditions(qualified, dialect, param_idx, sql, out_params);
                sql.push_back(')');
            }
            sql.push_back(')');
            return sql;
        };

        desc.scoped_eager_loader = [rel_ptr, parent_fk, child_fk, pk_ext = pk_extractor_](
            std::span<Entity> parents, Connection& conn, DatabaseDialect dialect,
            const std::vector<Condition>& child_conditions,
            const std::vector<OrderByClause>& order_bys,
            std::optional<size_t> limit) -> core::Task<void> {
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
            sql.append(DialectTraits::quote_identifier(dialect, "j." + parent_fk_col));

            for (const auto& col : target_schema.columns()) {
                sql.append(", ");
                sql.append(DialectTraits::quote_identifier(dialect, "t." + col.column_name));
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
            sql.append(DialectTraits::quote_identifier(dialect, "j." + child_fk_col));
            sql.append(" = ");
            sql.append(DialectTraits::quote_identifier(dialect, "t." + target_pk_col));

            sql.append(" WHERE ");
            sql.append(DialectTraits::quote_identifier(dialect, "j." + parent_fk_col));
            sql.append(" IN (");

            std::vector<std::string> params;
            size_t param_idx = 1;
            for (size_t i = 0; i < parent_pks.size(); ++i) {
                std::string p;
                DialectTraits::format_placeholder(dialect, param_idx++, p);
                sql.append(p);
                if (i + 1 < parent_pks.size()) sql.append(", ");
                params.push_back(parent_pks[i]);
            }
            sql.push_back(')');

            if (!child_conditions.empty()) {
                sql.append(" AND (");
                std::vector<Condition> qualified = child_conditions;
                for (auto& c : qualified) {
                    if (!c.custom_compiler && c.column.find('.') == std::string::npos) {
                        c.column = "t." + c.column;
                    }
                }
                compile_conditions(qualified, dialect, param_idx, sql, params);
                sql.push_back(')');
            }

            if (!order_bys.empty()) {
                sql.append(" ORDER BY ");
                for (size_t i = 0; i < order_bys.size(); ++i) {
                    if (i > 0) sql.append(", ");
                    std::string col = order_bys[i].column;
                    if (col.find('.') == std::string::npos) {
                        col = "t." + col;
                    }
                    sql.append(DialectTraits::quote_identifier(dialect, col));
                    sql.push_back(' ');
                    sql.append(order_to_sql(order_bys[i].direction));
                }
            }

            sql.push_back(';');

            auto rows = co_await conn.query(sql, params);
            for (const auto& r : rows) {
                std::string parent_id_val = std::string(r.get_raw(0));
                OffsetRowView offset_row(r, 1);
                TargetEntity target = target_schema.map_row(offset_row);
                auto it = parent_map.find(parent_id_val);
                if (it != parent_map.end()) {
                    for (auto* parent_ptr : it->second) {
                        if (!limit.has_value() || (parent_ptr->*rel_ptr).size() < *limit) {
                            (parent_ptr->*rel_ptr).push_back(target);
                        }
                    }
                }
            }
        };

        desc.eager_loader = [loader = desc.scoped_eager_loader](std::span<Entity> parents, Connection& conn, DatabaseDialect dialect) -> core::Task<void> {
            co_await loader(parents, conn, dialect, {}, {}, std::nullopt);
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
