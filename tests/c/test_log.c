#include <aegon/c/aegon.h>
#include "check.h"
#include <unistd.h>

int main(void) {
    /* 1. Init logger */
    CHECK_INT_EQ(aegon_log_init(AEGON_LOG_INFO, false), AEGON_OK);

    /* 2. Init twice -> AEGON_ERR_STATE */
    CHECK_INT_EQ(aegon_log_init(AEGON_LOG_DEBUG, false), AEGON_ERR_STATE);

    /* 3. Level get/set */
    CHECK_INT_EQ(aegon_log_level(), AEGON_LOG_INFO);
    aegon_log_set_level(AEGON_LOG_DEBUG);
    CHECK_INT_EQ(aegon_log_level(), AEGON_LOG_DEBUG);
    aegon_log_set_level(AEGON_LOG_WARN);
    CHECK_INT_EQ(aegon_log_level(), AEGON_LOG_WARN);

    /* 4. Add file sink */
    const char* log_file = "/tmp/aegon_c_test_log.txt";
    unlink(log_file);

    CHECK_INT_EQ(aegon_log_add_file_sink(log_file), AEGON_OK);

    /* Write log message */
    aegon_log_write(AEGON_LOG_ERROR, AEGON_STR("C ABI error log test entry\n"));
    aegon_log_flush();

    /* Verify file exists and has content */
    FILE* f = fopen(log_file, "r");
    CHECK(f != NULL);
    char buf[256];
    char* line = fgets(buf, sizeof(buf), f);
    CHECK(line != NULL);
    CHECK(strstr(buf, "C ABI error log test entry") != NULL);
    fclose(f);
    unlink(log_file);

    /* 5. Clear sinks & shutdown */
    aegon_log_clear_sinks();
    aegon_log_shutdown();

    return 0;
}
