#include "SelectBuilder.h"

namespace aegon::data::orm::sql {

QueryResult compile_select_query_impl(
    DatabaseDialect dialect,
    std::string_view table_name,
    std::string_view select_all_prefix,
    std::span<const std::string> projected_columns,
    std::span<const Condition> conditions,
    std::span<const std::string> group_bys,
    std::span<const Condition> havings,
    std::span<const OrderByClause> order_bys,
    std::optional<size_t> limit,
    std::optional<size_t> offset
) {
    QueryResult result;
    std::string& sql = result.sql;
    sql.reserve(256);

    if (projected_columns.empty()) {
        sql.append(select_all_prefix);
    } else {
        sql.append("SELECT ");
        for (size_t i = 0; i < projected_columns.size(); ++i) {
            sql.append(DialectTraits::quote_identifier(dialect, projected_columns[i]));
            if (i + 1 < projected_columns.size()) sql.append(", ");
        }
        sql.append(" FROM ");
        sql.append(DialectTraits::quote_identifier(dialect, table_name));
    }

    size_t param_idx = 1;

    if (!conditions.empty()) {
        sql.append(" WHERE ");
        compile_conditions(conditions, dialect, param_idx, sql, result.params);
    }

    if (!group_bys.empty()) {
        sql.append(" GROUP BY ");
        for (size_t i = 0; i < group_bys.size(); ++i) {
            if (i > 0) sql.append(", ");
            sql.append(DialectTraits::quote_identifier(dialect, group_bys[i]));
        }
    }

    if (!havings.empty()) {
        sql.append(" HAVING ");
        for (size_t i = 0; i < havings.size(); ++i) {
            if (i > 0) {
                sql.append(havings[i].conj == Conjunction::Or ? " OR " : " AND ");
            }
            compile_condition(havings[i], dialect, param_idx, sql, result.params);
        }
    }

    if (!order_bys.empty()) {
        sql.append(" ORDER BY ");
        for (size_t i = 0; i < order_bys.size(); ++i) {
            sql.append(DialectTraits::quote_identifier(dialect, order_bys[i].column));
            sql.push_back(' ');
            sql.append(order_to_sql(order_bys[i].direction));
            if (i + 1 < order_bys.size()) sql.append(", ");
        }
    }

    if (limit.has_value()) {
        sql.append(" LIMIT ");
        sql.append(std::to_string(*limit));
    }

    if (offset.has_value()) {
        sql.append(" OFFSET ");
        sql.append(std::to_string(*offset));
    }

    sql.push_back(';');
    return result;
}

QueryResult compile_count_query_impl(
    DatabaseDialect dialect,
    std::string_view table_name,
    std::span<const Condition> conditions,
    std::span<const std::string> group_bys,
    std::span<const Condition> havings
) {
    QueryResult result;
    std::string& sql = result.sql;
    sql.reserve(128);

    sql.append("SELECT COUNT(*) FROM ");
    sql.append(DialectTraits::quote_identifier(dialect, table_name));

    size_t param_idx = 1;

    if (!conditions.empty()) {
        sql.append(" WHERE ");
        compile_conditions(conditions, dialect, param_idx, sql, result.params);
    }

    if (!group_bys.empty()) {
        sql.append(" GROUP BY ");
        for (size_t i = 0; i < group_bys.size(); ++i) {
            if (i > 0) sql.append(", ");
            sql.append(DialectTraits::quote_identifier(dialect, group_bys[i]));
        }
    }

    if (!havings.empty()) {
        sql.append(" HAVING ");
        for (size_t i = 0; i < havings.size(); ++i) {
            if (i > 0) sql.append(havings[i].conj == Conjunction::Or ? " OR " : " AND ");
            compile_condition(havings[i], dialect, param_idx, sql, result.params);
        }
    }

    sql.push_back(';');
    return result;
}

QueryResult compile_aggregate_query_impl(
    DatabaseDialect dialect,
    std::string_view table_name,
    std::string_view agg_func,
    std::string_view col_expr,
    std::span<const Condition> conditions,
    std::span<const std::string> group_bys,
    std::span<const Condition> havings
) {
    QueryResult result;
    std::string& sql = result.sql;
    sql.reserve(128);

    sql.append(agg_func);
    sql.push_back('(');
    if (col_expr == "*") {
        sql.push_back('*');
    } else {
        sql.append(DialectTraits::quote_identifier(dialect, col_expr));
    }
    sql.append(") FROM ");
    sql.append(DialectTraits::quote_identifier(dialect, table_name));

    size_t param_idx = 1;

    if (!conditions.empty()) {
        sql.append(" WHERE ");
        compile_conditions(conditions, dialect, param_idx, sql, result.params);
    }

    if (!group_bys.empty()) {
        sql.append(" GROUP BY ");
        for (size_t i = 0; i < group_bys.size(); ++i) {
            if (i > 0) sql.append(", ");
            sql.append(DialectTraits::quote_identifier(dialect, group_bys[i]));
        }
    }

    if (!havings.empty()) {
        sql.append(" HAVING ");
        for (size_t i = 0; i < havings.size(); ++i) {
            if (i > 0) sql.append(havings[i].conj == Conjunction::Or ? " OR " : " AND ");
            compile_condition(havings[i], dialect, param_idx, sql, result.params);
        }
    }

    sql.push_back(';');

    // Prepend SELECT
    result.sql = "SELECT " + result.sql;
    return result;
}

} // namespace aegon::data::orm::sql
