#pragma once

#include "Dialect.h"
#include "TypeMapper.h"
#include <string>
#include <string_view>
#include <optional>
#include <functional>

namespace aegon::data::orm::sql {

enum class OnDeleteAction : uint8_t {
    NoAction,
    Cascade,
    SetNull,
    Restrict
};

inline constexpr std::string_view on_delete_action_to_sql(OnDeleteAction action) noexcept {
    switch (action) {
        case OnDeleteAction::Cascade:   return "CASCADE";
        case OnDeleteAction::SetNull:   return "SET NULL";
        case OnDeleteAction::Restrict:  return "RESTRICT";
        case OnDeleteAction::NoAction:  return "NO ACTION";
    }
    return "NO ACTION";
}

struct ForeignKeyMetadata {
    std::string target_table;
    std::string target_column;
    OnDeleteAction on_delete{OnDeleteAction::NoAction};
    OnDeleteAction on_update{OnDeleteAction::NoAction};
};

struct ColumnMetadata {
    std::string column_name;
    std::string (*sql_type_fn)(DatabaseDialect, size_t length){nullptr};
    bool is_primary_key{false};
    bool is_auto_increment{false};
    bool is_nullable{false};
    bool is_unique{false};
    bool is_indexed{false};
    size_t length{0};
    std::string default_value;
    bool is_created_at{false};
    bool is_updated_at{false};
    size_t member_offset{0};
    std::optional<ForeignKeyMetadata> foreign_key;
};

template <typename Entity, typename FieldType>
class ColumnDef {
public:
    FieldType Entity::* member_ptr_{nullptr};
    ColumnMetadata meta_;

    ColumnDef(FieldType Entity::* ptr, std::string col_name)
        : member_ptr_(ptr) {
        meta_.column_name = std::move(col_name);
        meta_.sql_type_fn = [](DatabaseDialect d, size_t len) -> std::string {
            return TypeMapper<FieldType>::column_type(d, len);
        };
        meta_.is_nullable = TypeMapper<FieldType>::is_nullable;
    }

    ColumnDef& column_name(std::string name) {
        meta_.column_name = std::move(name);
        return *this;
    }

    ColumnDef& unique(bool val = true) {
        meta_.is_unique = val;
        return *this;
    }

    ColumnDef& indexed(bool val = true) {
        meta_.is_indexed = val;
        return *this;
    }

    ColumnDef& length(size_t len) {
        meta_.length = len;
        return *this;
    }

    ColumnDef& default_value(std::string val) {
        meta_.default_value = std::move(val);
        return *this;
    }

    ColumnDef& default_value(auto val) {
        meta_.default_value = std::to_string(val);
        return *this;
    }

    ColumnDef& nullable(bool val = true) {
        meta_.is_nullable = val;
        return *this;
    }

    ColumnDef& not_null() {
        meta_.is_nullable = false;
        return *this;
    }

    template <typename TargetEntity, typename TargetField>
    ColumnDef& references(TargetField TargetEntity::* /*target_ptr*/) {
        ForeignKeyMetadata fk;
        fk.target_table = std::string(TargetEntity::schema().table_name());
        fk.target_column = std::string(TargetEntity::schema().primary_key_name());
        meta_.foreign_key = std::move(fk);
        return *this;
    }

    ColumnDef& on_delete(OnDeleteAction action) {
        if (meta_.foreign_key) {
            meta_.foreign_key->on_delete = action;
        }
        return *this;
    }

    ColumnDef& on_delete_cascade() {
        return on_delete(OnDeleteAction::Cascade);
    }

    ColumnDef& on_delete_set_null() {
        return on_delete(OnDeleteAction::SetNull);
    }

    ColumnDef& on_delete_restrict() {
        return on_delete(OnDeleteAction::Restrict);
    }

    ColumnDef& on_update_cascade() {
        if (meta_.foreign_key) {
            meta_.foreign_key->on_update = OnDeleteAction::Cascade;
        }
        return *this;
    }
};

} // namespace aegon::data::orm::sql
