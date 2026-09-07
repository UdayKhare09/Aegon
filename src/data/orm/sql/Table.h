#pragma once

#include "Dialect.h"
#include "TypeMapper.h"
#include "Column.h"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <stdexcept>
#include <concepts>

#if __has_include(<glaze/glaze.hpp>)
#include <glaze/glaze.hpp>
#endif

namespace aegon::data::orm::sql {

template <typename Entity>
class TableDef {
    std::string table_name_;
    std::string primary_key_name_{"id"};
    std::vector<ColumnMetadata> columns_;

    ColumnMetadata& current_col() {
        if (columns_.empty()) {
            throw std::runtime_error("TableDef: cannot configure column attributes before adding a column.");
        }
        return columns_.back();
    }

public:
    explicit TableDef(std::string name) : table_name_(std::move(name)) {}

    [[nodiscard]] const std::string& table_name() const noexcept { return table_name_; }
    [[nodiscard]] const std::string& primary_key_name() const noexcept { return primary_key_name_; }
    [[nodiscard]] const std::vector<ColumnMetadata>& columns() const noexcept { return columns_; }
    [[nodiscard]] std::vector<ColumnMetadata>& columns() noexcept { return columns_; }

    template <typename FieldType>
    TableDef& id(FieldType Entity::* /*ptr*/, std::string col_name = "id") {
        primary_key_name_ = col_name.empty() ? "id" : col_name;

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

        columns_.push_back(std::move(meta));
        return *this;
    }

    template <typename FieldType>
    TableDef& column(FieldType Entity::* /*ptr*/, std::string col_name = "") {
        ColumnMetadata meta;
        meta.column_name = std::move(col_name);
        meta.is_nullable = TypeMapper<FieldType>::is_nullable;
        meta.sql_type_fn = [](DatabaseDialect d, size_t len) -> std::string {
            return TypeMapper<FieldType>::column_type(d, len);
        };

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
};

template <typename Entity>
inline TableDef<Entity> table(std::string name) {
    return TableDef<Entity>(std::move(name));
}

} // namespace aegon::data::orm::sql
