#include <aegon/c/aegon.h>
#include "check.h"
#include "fixture_server.h"
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(d) _mkdir(d)
#define RMDIR(d) _rmdir(d)
#else
#include <unistd.h>
#define MKDIR(d) mkdir(d, 0755)
#define RMDIR(d) rmdir(d)
#endif

static size_t test_cache_control(aegon_str_t path, bool is_fallback,
                                 char* buf, size_t buf_cap, void* ud) {
    (void)path;
    (void)is_fallback;
    (void)ud;
    const char* cc = "public, max-age=3600";
    size_t len = strlen(cc);
    if (len + 1 > buf_cap) return 0;
    memcpy(buf, cc, len + 1);
    return len;
}

static void write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "wb");
    CHECK(f != NULL);
    size_t len = strlen(content);
    CHECK(fwrite(content, 1, len, f) == len);
    fclose(f);
}

int main(void) {
    /* 1. Config init checks */
    aegon_static_files_config_t cfg;
    aegon_static_files_config_init(&cfg);

    CHECK_INT_EQ(cfg.struct_size, sizeof(aegon_static_files_config_t));
    CHECK(cfg.precompressed == true);
    CHECK(cfg.cache_in_memory == true);
    CHECK_STR_EQ(cfg.index_file, "index.html");
    CHECK(cfg.spa == false);
    CHECK_STR_EQ(cfg.fallback_file, "index.html");
    CHECK(cfg.etag == true);
    CHECK(cfg.last_modified == true);
    CHECK(cfg.cache_control == NULL);

    /* 2. Error checks */
    CHECK_INT_EQ(aegon_router_static_files(NULL, "/static", ".", &cfg), AEGON_ERR_INVALID_ARG);
    CHECK_INT_EQ(aegon_group_static_files(NULL, "/static", ".", &cfg), AEGON_ERR_INVALID_ARG);

    aegon_router_t* r_err = aegon_router_create();
    CHECK_INT_EQ(aegon_router_static_files(r_err, NULL, ".", &cfg), AEGON_ERR_INVALID_ARG);
    CHECK_INT_EQ(aegon_router_static_files(r_err, "/static", NULL, &cfg), AEGON_ERR_INVALID_ARG);
    /* Non-existent directory -> AEGON_ERR_IO */
    CHECK_INT_EQ(aegon_router_static_files(r_err, "/static", "path/that/does/not/exist_xyz_123", &cfg), AEGON_ERR_IO);
    aegon_router_destroy(r_err);

    /* 3. Setup temporary fixture directory */
    const char* test_dir = "build/tmp_test_static_dir";
    MKDIR("build");
    MKDIR(test_dir);

    char idx_path[256];
    char css_path[256];
    snprintf(idx_path, sizeof(idx_path), "%s/index.html", test_dir);
    snprintf(css_path, sizeof(css_path), "%s/style.css", test_dir);

    write_file(idx_path, "<h1>Hello Aegon Static</h1>");
    write_file(css_path, "body { background: #121212; }");

    /* 4. Setup server with static files */
    uint16_t port = get_ephemeral_port();
    aegon_router_t* router = aegon_router_create();
    CHECK(router != NULL);

    cfg.cache_control = test_cache_control;
    cfg.spa = true; /* Enable SPA fallback */
    CHECK_INT_EQ(aegon_router_static_files(router, "/static", test_dir, &cfg), AEGON_OK);

    /* Group static files */
    aegon_group_t* grp = aegon_router_group(router, "/site");
    CHECK(grp != NULL);
    aegon_static_files_config_t grp_cfg;
    aegon_static_files_config_init(&grp_cfg);
    CHECK_INT_EQ(aegon_group_static_files(grp, "/assets", test_dir, &grp_cfg), AEGON_OK);
    aegon_group_destroy(grp);

    /* Start server */
    aegon_server_config_t scfg;
    aegon_server_config_init(&scfg);
    aegon_server_t* server = aegon_server_create(&router, &scfg);
    CHECK(server != NULL);
    CHECK_INT_EQ(aegon_server_listen(server, port, "127.0.0.1"), AEGON_OK);
    CHECK_INT_EQ(aegon_server_run_async(server, 2), AEGON_OK);
    fixture_sleep_ms(50);

    char resp[2048];

    /* Test 1: GET index file via directory path */
    CHECK(http_exchange(port, "GET /static/ HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "<h1>Hello Aegon Static</h1>") != NULL);
    CHECK(strstr(resp, "Cache-Control: public, max-age=3600") != NULL ||
          strstr(resp, "cache-control: public, max-age=3600") != NULL);

    /* Extract ETag */
    char* etag_hdr = strstr(resp, "ETag: ");
    if (!etag_hdr) etag_hdr = strstr(resp, "etag: ");
    CHECK(etag_hdr != NULL);
    char etag_val[128];
    char* etag_end = strstr(etag_hdr, "\r\n");
    CHECK(etag_end != NULL);
    size_t etag_len = (size_t)(etag_end - (etag_hdr + 6));
    CHECK(etag_len < sizeof(etag_val));
    memcpy(etag_val, etag_hdr + 6, etag_len);
    etag_val[etag_len] = '\0';

    /* Test 2: Conditional GET with If-None-Match (304 Not Modified) */
    char cond_req[512];
    snprintf(cond_req, sizeof(cond_req),
             "GET /static/ HTTP/1.1\r\nHost: 127.0.0.1\r\nIf-None-Match: %s\r\n\r\n", etag_val);
    CHECK(http_exchange(port, cond_req, resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "304") != NULL);

    /* Test 3: GET css asset */
    CHECK(http_exchange(port, "GET /static/style.css HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "body { background: #121212; }") != NULL);

    /* Test 4: SPA Fallback (non-existent route serves fallback_file index.html) */
    CHECK(http_exchange(port, "GET /static/app/dashboard/profile HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "<h1>Hello Aegon Static</h1>") != NULL);

    /* Test 5: Group static files */
    CHECK(http_exchange(port, "GET /site/assets/style.css HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", resp, sizeof(resp)) > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "body { background: #121212; }") != NULL);

    /* Cleanup server */
    aegon_server_stop(server);
    CHECK_INT_EQ(aegon_server_join(server), AEGON_OK);
    aegon_server_destroy(server);

    /* Cleanup temporary files */
    remove(idx_path);
    remove(css_path);
    RMDIR(test_dir);

    return 0;
}

