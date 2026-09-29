#include "Expression.h"

namespace aegon::data::orm::sql {

void compile_condition(const Condition& cond, DatabaseDialect dialect,
                       size_t& param_idx, std::string& sql,
                       std::vector<SqlParam>& out_params) {
    if (cond.custom_compiler) {
        sql.append(cond.custom_compiler(dialect, param_idx, out_params));
        return;
    }

    sql.append(DialectTraits::quote_identifier(dialect, cond.column));
    sql.push_back(' ');

    if (cond.op == Op::IsNull || cond.op == Op::IsNotNull) {
        sql.append(op_to_sql(cond.op));
    } else if (cond.op == Op::Between) {
        sql.append("BETWEEN ");
        std::string p1, p2;
        DialectTraits::format_placeholder(dialect, param_idx++, p1);
        DialectTraits::format_placeholder(dialect, param_idx++, p2);
        sql.append(p1).append(" AND ").append(p2);
        out_params.push_back(cond.values[0]);
        out_params.push_back(cond.values[1]);
    } else if (cond.op == Op::In || cond.op == Op::NotIn) {
        sql.append(op_to_sql(cond.op));
        sql.append(" (");
        for (size_t j = 0; j < cond.values.size(); ++j) {
            std::string p;
            DialectTraits::format_placeholder(dialect, param_idx++, p);
            sql.append(p);
            if (j + 1 < cond.values.size()) sql.append(", ");
            out_params.push_back(cond.values[j]);
        }
        sql.push_back(')');
    } else {
        sql.append(op_to_sql(cond.op));
        sql.push_back(' ');
        std::string p;
        DialectTraits::format_placeholder(dialect, param_idx++, p);
        sql.append(p);
        out_params.push_back(cond.values[0]);
    }
}

void compile_conditions(std::span<const Condition> conditions, DatabaseDialect dialect,
                        size_t& param_idx, std::string& sql,
                        std::vector<SqlParam>& out_params) {
    for (size_t i = 0; i < conditions.size(); ++i) {
        const auto& cond = conditions[i];
        if (i > 0) {
            sql.append(cond.conj == Conjunction::Or ? " OR " : " AND ");
        }
        compile_condition(cond, dialect, param_idx, sql, out_params);
    }
}

} // namespace aegon::data::orm::sql
