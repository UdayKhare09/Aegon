#pragma once

#include "Dialect.h"
#include "TypeMapper.h"
#include "RowView.h"
#include "Expression.h"
#include "SqlParam.h"
#include "Relations.h"
#include "Connection.h"
#include "QueryResult.h"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <stdexcept>
#include <functional>
#include <unordered_map>
#include <span>
#include <algorithm>

namespace aegon::data::orm::sql::detail {

template <typename Entity, typename ChildEntity, typename ForeignKeyField, typename Descriptor>
Descriptor make_has_one_descriptor(
    HasOne<ChildEntity> Entity::* rel_ptr,
    ForeignKeyField ChildEntity::* fk_ptr,
    std::string rel_name,
    size_t member_offset,
    std::string parent_pk_name,
    std::function<std::string(const Entity&)> pk_extractor
) {
    Descriptor desc;
    desc.kind = RelationKind::OneToOne;
    desc.member_offset = member_offset;
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

            auto rows = co_await conn.query(sql, std::array{SqlParam::text(pk_val)});
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
                co_await conn.execute(del_sql, std::array{SqlParam::text(owner_key)});

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
                co_await conn.execute(del_sql, std::array{SqlParam::text(owner_key)});
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

    desc.exists_builder = [fk_ptr, parent_pk = parent_pk_name](
        DatabaseDialect dialect, bool negate, const std::string& parent_tbl,
        const std::vector<Condition>& child_conditions, size_t& param_idx,
        std::vector<SqlParam>& out_params) -> std::string {
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

    desc.scoped_eager_loader = [rel_ptr, fk_ptr, pk_ext = pk_extractor](
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

        std::vector<SqlParam> params;
        size_t param_idx = 1;
        for (size_t i = 0; i < parent_pks.size(); ++i) {
            std::string p;
            DialectTraits::format_placeholder(dialect, param_idx++, p);
            sql.append(p);
            if (i + 1 < parent_pks.size()) sql.append(", ");
            params.push_back(SqlParam::text(parent_pks[i]));
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
            std::string fk_val = make_sql_param(child.*fk_ptr).to_debug_string();
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

    return desc;
}

template <typename Entity, typename ChildEntity, typename ForeignKeyField, typename Descriptor>
Descriptor make_has_many_descriptor(
    HasMany<ChildEntity> Entity::* rel_ptr,
    ForeignKeyField ChildEntity::* fk_ptr,
    std::string rel_name,
    size_t member_offset,
    std::string parent_pk_name,
    std::function<std::string(const Entity&)> pk_extractor
) {
    Descriptor desc;
    desc.kind = RelationKind::OneToMany;
    desc.member_offset = member_offset;
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

            auto rows = co_await conn.query(sql, std::array{SqlParam::text(pk_val)});
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

                size_t n = co_await conn.execute(del_sql, std::array{SqlParam::text(owner_key), SqlParam::text(item_id)});
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

    desc.exists_builder = [fk_ptr, parent_pk = parent_pk_name](
        DatabaseDialect dialect, bool negate, const std::string& parent_tbl,
        const std::vector<Condition>& child_conditions, size_t& param_idx,
        std::vector<SqlParam>& out_params) -> std::string {
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

    desc.scoped_eager_loader = [rel_ptr, fk_ptr, pk_ext = pk_extractor](
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

        std::vector<SqlParam> params;
        size_t param_idx = 1;
        for (size_t i = 0; i < parent_pks.size(); ++i) {
            std::string p;
            DialectTraits::format_placeholder(dialect, param_idx++, p);
            sql.append(p);
            if (i + 1 < parent_pks.size()) sql.append(", ");
            params.push_back(SqlParam::text(parent_pks[i]));
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
            std::string fk_val = make_sql_param(child.*fk_ptr).to_debug_string();
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

    return desc;
}

template <typename Entity, typename TargetEntity, typename JunctionEntity, typename ParentFkField, typename ChildFkField, typename Descriptor>
Descriptor make_many_to_many_descriptor(
    HasMany<TargetEntity> Entity::* rel_ptr,
    ParentFkField JunctionEntity::* parent_fk,
    ChildFkField JunctionEntity::* child_fk,
    std::string rel_name,
    size_t member_offset,
    std::string parent_pk_name,
    std::function<std::string(const Entity&)> pk_extractor
) {
    Descriptor desc;
    desc.kind = RelationKind::ManyToMany;
    desc.member_offset = member_offset;
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

            auto rows = co_await conn.query(sql, std::array{SqlParam::text(pk_val)});
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

                co_await conn.execute(sql, std::array{SqlParam::text(owner_key), SqlParam::text(child_pk_val)});
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

                size_t n = co_await conn.execute(sql, std::array{SqlParam::text(owner_key), SqlParam::text(item_id)});
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

            co_await conn.execute(sql, std::array{SqlParam::text(parent_pk), SqlParam::text(target_pk_val)});
        }
    };

    desc.exists_builder = [parent_fk, child_fk, parent_pk = parent_pk_name](
        DatabaseDialect dialect, bool negate, const std::string& parent_tbl,
        const std::vector<Condition>& child_conditions, size_t& param_idx,
        std::vector<SqlParam>& out_params) -> std::string {
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

    desc.scoped_eager_loader = [rel_ptr, parent_fk, child_fk, pk_ext = pk_extractor](
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

        std::vector<SqlParam> params;
        size_t param_idx = 1;
        for (size_t i = 0; i < parent_pks.size(); ++i) {
            std::string p;
            DialectTraits::format_placeholder(dialect, param_idx++, p);
            sql.append(p);
            if (i + 1 < parent_pks.size()) sql.append(", ");
            params.push_back(SqlParam::text(parent_pks[i]));
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

    return desc;
}

} // namespace aegon::data::orm::sql::detail
