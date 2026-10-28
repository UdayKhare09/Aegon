#include <aegon/c/aegon.h>
#include "check.h"

int main(void) {
    /* 1. Version getters */
    CHECK_INT_EQ(aegon_version(), AEGON_VERSION_HEX);
    CHECK_STR_EQ(aegon_version_string(), AEGON_VERSION_STRING);
    CHECK_INT_EQ(aegon_abi_version(), AEGON_ABI_VERSION);

    /* 2. String slices */
    aegon_str_t s1 = AEGON_STR("hello world");
    CHECK_INT_EQ(s1.len, 11);
    CHECK(memcmp(s1.data, "hello world", 11) == 0);

    aegon_str_t s2 = aegon_str("foobar");
    CHECK_INT_EQ(s2.len, 6);
    CHECK(memcmp(s2.data, "foobar", 6) == 0);

    aegon_str_t s_null = aegon_str(NULL);
    CHECK(s_null.data == NULL);
    CHECK_INT_EQ(s_null.len, 0);

    /* 3. String dup & free */
    aegon_str_t s_dup = aegon_str_dup(s1);
    CHECK(s_dup.data != NULL);
    CHECK_INT_EQ(s_dup.len, 11);
    CHECK_STR_EQ(s_dup.data, "hello world");
    aegon_mem_free((void*)s_dup.data);

    aegon_str_t s_dup_null = aegon_str_dup(s_null);
    CHECK(s_dup_null.data == NULL);
    CHECK_INT_EQ(s_dup_null.len, 0);

    /* NULL free is safe */
    aegon_mem_free(NULL);

    /* 4. Error strings */
    CHECK_STR_EQ(aegon_err_str(AEGON_OK), "AEGON_OK");
    CHECK_STR_EQ(aegon_err_str(AEGON_ERR_INVALID_ARG), "AEGON_ERR_INVALID_ARG");
    CHECK_STR_EQ(aegon_err_str(AEGON_ERR_NOMEM), "AEGON_ERR_NOMEM");
    CHECK_STR_EQ(aegon_err_str(AEGON_ERR_IO), "AEGON_ERR_IO");
    CHECK_STR_EQ(aegon_err_str(AEGON_ERR_TLS), "AEGON_ERR_TLS");
    CHECK_STR_EQ(aegon_err_str(AEGON_ERR_NOT_FOUND), "AEGON_ERR_NOT_FOUND");
    CHECK_STR_EQ(aegon_err_str(AEGON_ERR_STATE), "AEGON_ERR_STATE");
    CHECK_STR_EQ(aegon_err_str(AEGON_ERR_UNSUPPORTED), "AEGON_ERR_UNSUPPORTED");
    CHECK_STR_EQ(aegon_err_str(AEGON_ERR_BUFFER_TOO_SMALL), "AEGON_ERR_BUFFER_TOO_SMALL");
    CHECK_STR_EQ(aegon_err_str(AEGON_ERR_INTERNAL), "AEGON_ERR_INTERNAL");
    CHECK_STR_EQ(aegon_err_str((aegon_err_t)999), "AEGON_ERR_UNKNOWN");

    /* 5. Last error */
    aegon_last_error_clear();
    CHECK_STR_EQ(aegon_last_error(), "");

    /* 6. Status phrase */
    CHECK_STR_EQ(aegon_status_phrase(AEGON_STATUS_OK), "OK");
    CHECK_STR_EQ(aegon_status_phrase(AEGON_STATUS_CREATED), "Created");
    CHECK_STR_EQ(aegon_status_phrase(AEGON_STATUS_NOT_FOUND), "Not Found");
    CHECK_STR_EQ(aegon_status_phrase(AEGON_STATUS_INTERNAL_SERVER_ERROR), "Internal Server Error");
    CHECK_STR_EQ(aegon_status_phrase(999), "Unknown");

    /* 7. HTTP method strings & parsing */
    CHECK_STR_EQ(aegon_method_cstr(AEGON_METHOD_GET), "GET");
    CHECK_STR_EQ(aegon_method_cstr(AEGON_METHOD_POST), "POST");
    CHECK_STR_EQ(aegon_method_cstr(AEGON_METHOD_PUT), "PUT");
    CHECK_STR_EQ(aegon_method_cstr(AEGON_METHOD_DELETE), "DELETE");
    CHECK_STR_EQ(aegon_method_cstr(AEGON_METHOD_PATCH), "PATCH");
    CHECK_STR_EQ(aegon_method_cstr(AEGON_METHOD_HEAD), "HEAD");
    CHECK_STR_EQ(aegon_method_cstr(AEGON_METHOD_OPTIONS), "OPTIONS");
    CHECK_STR_EQ(aegon_method_cstr(AEGON_METHOD_CONNECT), "CONNECT");
    CHECK_STR_EQ(aegon_method_cstr(AEGON_METHOD_TRACE), "TRACE");
    CHECK_STR_EQ(aegon_method_cstr(AEGON_METHOD_UNKNOWN), "UNKNOWN");

    CHECK_INT_EQ(aegon_method_from_str(AEGON_STR("GET")), AEGON_METHOD_GET);
    CHECK_INT_EQ(aegon_method_from_str(AEGON_STR("POST")), AEGON_METHOD_POST);
    CHECK_INT_EQ(aegon_method_from_str(AEGON_STR("PUT")), AEGON_METHOD_PUT);
    CHECK_INT_EQ(aegon_method_from_str(AEGON_STR("DELETE")), AEGON_METHOD_DELETE);
    CHECK_INT_EQ(aegon_method_from_str(AEGON_STR("PATCH")), AEGON_METHOD_PATCH);
    CHECK_INT_EQ(aegon_method_from_str(AEGON_STR("HEAD")), AEGON_METHOD_HEAD);
    CHECK_INT_EQ(aegon_method_from_str(AEGON_STR("OPTIONS")), AEGON_METHOD_OPTIONS);
    CHECK_INT_EQ(aegon_method_from_str(AEGON_STR("CONNECT")), AEGON_METHOD_CONNECT);
    CHECK_INT_EQ(aegon_method_from_str(AEGON_STR("TRACE")), AEGON_METHOD_TRACE);
    CHECK_INT_EQ(aegon_method_from_str(AEGON_STR("INVALID")), AEGON_METHOD_UNKNOWN);
    CHECK_INT_EQ(aegon_method_from_str(s_null), AEGON_METHOD_UNKNOWN);

    /* 8. HTTP version strings */
    CHECK_STR_EQ(aegon_http_version_cstr(AEGON_HTTP_1_0), "HTTP/1.0");
    CHECK_STR_EQ(aegon_http_version_cstr(AEGON_HTTP_1_1), "HTTP/1.1");
    CHECK_STR_EQ(aegon_http_version_cstr(AEGON_HTTP_2), "HTTP/2.0");
    CHECK_STR_EQ(aegon_http_version_cstr(AEGON_HTTP_3), "HTTP/3.0");

    printf("All test_core assertions passed.\n");
    return 0;
}

