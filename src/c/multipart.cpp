#include <aegon/c/multipart.h>
#include "internal.h"
#include "http/MultipartParser.h"
#include "http/Context.h"
#include <new>

struct aegon_multipart {
    aegon::http::MultipartFormData form;
};

extern "C" {

aegon_multipart_t* aegon_multipart_parse(aegon_ctx_t* ctx) {
    if (!ctx) return nullptr;
    auto* cpp_ctx = aegon::c::to_cpp(ctx);
    auto ct_opt = cpp_ctx->req().headers().get("content-type");
    if (!ct_opt) return nullptr;

    std::string_view ct = *ct_opt;
    std::string_view body = cpp_ctx->req().body();

    auto parsed = aegon::http::MultipartParser::parse(ct, body);
    if (!parsed) return nullptr;

    auto* mp = new (std::nothrow) aegon_multipart();
    if (!mp) return nullptr;
    mp->form = std::move(*parsed);
    return reinterpret_cast<aegon_multipart_t*>(mp);
}

void aegon_multipart_destroy(aegon_multipart_t* mp) {
    if (!mp) return;
    delete reinterpret_cast<aegon_multipart*>(mp);
}

aegon_str_t aegon_multipart_field(aegon_multipart_t* mp, const char* name) {
    if (!mp || !name) return aegon_str_t{nullptr, 0};
    auto* handle = reinterpret_cast<aegon_multipart*>(mp);
    auto val = handle->form.get(name);
    if (!val) return aegon_str_t{nullptr, 0};
    return aegon_str_t{val->data(), val->size()};
}

size_t aegon_multipart_field_count(aegon_multipart_t* mp) {
    if (!mp) return 0;
    auto* handle = reinterpret_cast<aegon_multipart*>(mp);
    return handle->form.fields().size();
}

bool aegon_multipart_field_at(aegon_multipart_t* mp, size_t index,
                              aegon_str_t* out_name,
                              aegon_str_t* out_value) {
    if (!mp) return false;
    auto* handle = reinterpret_cast<aegon_multipart*>(mp);
    const auto& fields = handle->form.fields();
    if (index >= fields.size()) return false;
    if (out_name) {
        out_name->data = fields[index].name.data();
        out_name->len = fields[index].name.size();
    }
    if (out_value) {
        out_value->data = fields[index].value.data();
        out_value->len = fields[index].value.size();
    }
    return true;
}

bool aegon_multipart_file(aegon_multipart_t* mp, const char* name,
                          aegon_multipart_file_info_t* out_info) {
    if (!mp || !name) return false;
    auto* handle = reinterpret_cast<aegon_multipart*>(mp);
    auto f = handle->form.file(name);
    if (!f) return false;
    if (out_info) {
        out_info->name.data = f->name.data();
        out_info->name.len = f->name.size();
        out_info->filename.data = f->filename.data();
        out_info->filename.len = f->filename.size();
        out_info->content_type.data = f->content_type.data();
        out_info->content_type.len = f->content_type.size();
        out_info->data.data = f->data.data();
        out_info->data.len = f->data.size();
    }
    return true;
}

size_t aegon_multipart_file_count(aegon_multipart_t* mp) {
    if (!mp) return 0;
    auto* handle = reinterpret_cast<aegon_multipart*>(mp);
    return handle->form.files().size();
}

bool aegon_multipart_file_at(aegon_multipart_t* mp, size_t index,
                             aegon_multipart_file_info_t* out_info) {
    if (!mp) return false;
    auto* handle = reinterpret_cast<aegon_multipart*>(mp);
    const auto& files = handle->form.files();
    if (index >= files.size()) return false;
    if (out_info) {
        out_info->name.data = files[index].name.data();
        out_info->name.len = files[index].name.size();
        out_info->filename.data = files[index].filename.data();
        out_info->filename.len = files[index].filename.size();
        out_info->content_type.data = files[index].content_type.data();
        out_info->content_type.len = files[index].content_type.size();
        out_info->data.data = files[index].data.data();
        out_info->data.len = files[index].data.size();
    }
    return true;
}

} // extern "C"

