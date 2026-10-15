#pragma once

#include "Dialect.h"
#include "Table.h"
#include "QueryResult.h"
#include "SqlParam.h"
#include <string>
#include <vector>

namespace aegon::data::orm::sql {

template <typename Entity>
class InsertBuilder {
    TableDef<Entity> schema_;
    std::vector<Entity> entities_;

public:
    InsertBuilder() : schema_(Entity::schema()) {}
    explicit InsertBuilder(TableDef<Entity> schema) : schema_(std::move(schema)) {}

    InsertBuilder& values(Entity entity) {
        entities_.push_back(std::move(entity));
        return *this;
    }

    InsertBuilder& values(std::vector<Entity> entities) {
        for (auto& e : entities) {
            entities_.push_back(std::move(e));
        }
        return *this;
    }

    [[nodiscard]] QueryResult to_sql(DatabaseDialect dialect) const {
        return schema_.build_insert_sql(entities_, dialect);
    }
};

template <typename Entity>
inline InsertBuilder<Entity> insert_into() {
    return InsertBuilder<Entity>();
}

} // namespace aegon::data::orm::sql
