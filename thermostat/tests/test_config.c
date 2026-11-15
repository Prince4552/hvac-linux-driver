// SPDX-License-Identifier: MIT
/*
 * Unit tests for the configuration parser (PROJECT_PLAN.md section 4.5): valid files, comments and
 * blank lines, unknown and duplicate keys, bad numbers, ranges, defaults, the cross-field band rule,
 * line numbers in error messages, and loading from a real file.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "unity.h"

#define ERR_LEN 160u

static hvacd_config_t cfg;
static char err[ERR_LEN];

void setUp(void)
{
	memset(&cfg, 0, sizeof(cfg));
	memset(err, 0, sizeof(err));
}

void tearDown(void)
{
}

static int parse(const char *text)
{
	return config_parse_string(text, &cfg, err, sizeof(err));
}

static void expect_error(const char *text, const char *expected_message)
{
	TEST_ASSERT_EQUAL_INT(-EINVAL, parse(text));
	TEST_ASSERT_EQUAL_STRING(expected_message, err);
}

static void test_empty_text_gives_all_defaults(void)
{
	TEST_ASSERT_EQUAL_INT(0, parse(""));
	TEST_ASSERT_EQUAL_INT32(26000, cfg.core.cool_setpoint_mc);
	TEST_ASSERT_EQUAL_INT32(20000, cfg.core.heat_setpoint_mc);
	TEST_ASSERT_EQUAL_INT32(1000, cfg.core.deadband_mc);
	TEST_ASSERT_EQUAL_UINT32(180, cfg.core.min_off_s);
	TEST_ASSERT_EQUAL_UINT32(120, cfg.core.min_run_s);
	TEST_ASSERT_EQUAL_UINT32(5, cfg.core.fault_recover_count);
	TEST_ASSERT_EQUAL_INT32(5000, cfg.core.max_step_mc);
	TEST_ASSERT_EQUAL_UINT32(1000, cfg.loop_period_ms);
	TEST_ASSERT_EQUAL_STRING("/sys/bus/iio/devices", cfg.iio_base_dir);
	TEST_ASSERT_EQUAL_STRING("hvac_temp", cfg.iio_device_name);
	TEST_ASSERT_EQUAL_UINT16(502, cfg.modbus_port);
	TEST_ASSERT_EQUAL_UINT32(4, cfg.modbus_max_clients);
	TEST_ASSERT_EQUAL(CONFIG_LOG_INFO, cfg.log_level);
}

static void test_full_valid_file_sets_every_key(void)
{
	const char *text =
		"cool_setpoint_mc = 25000\n"
		"heat_setpoint_mc = 19000\n"
		"deadband_mc = 500\n"
		"min_off_s = 2\n"
		"min_run_s = 3\n"
		"fault_recover_count = 3\n"
		"max_step_mc = 4000\n"
		"loop_period_ms = 200\n"
		"iio_base_dir = /tmp/fake iio\n"
		"iio_device_name = other_sensor\n"
		"modbus_port = 15020\n"
		"modbus_max_clients = 8\n"
		"log_level = debug\n";

	TEST_ASSERT_EQUAL_INT(0, parse(text));
	TEST_ASSERT_EQUAL_INT32(25000, cfg.core.cool_setpoint_mc);
	TEST_ASSERT_EQUAL_INT32(19000, cfg.core.heat_setpoint_mc);
	TEST_ASSERT_EQUAL_INT32(500, cfg.core.deadband_mc);
	TEST_ASSERT_EQUAL_UINT32(2, cfg.core.min_off_s);
	TEST_ASSERT_EQUAL_UINT32(3, cfg.core.min_run_s);
	TEST_ASSERT_EQUAL_UINT32(3, cfg.core.fault_recover_count);
	TEST_ASSERT_EQUAL_INT32(4000, cfg.core.max_step_mc);
	TEST_ASSERT_EQUAL_UINT32(200, cfg.loop_period_ms);
	TEST_ASSERT_EQUAL_STRING("/tmp/fake iio", cfg.iio_base_dir);
	TEST_ASSERT_EQUAL_STRING("other_sensor", cfg.iio_device_name);
	TEST_ASSERT_EQUAL_UINT16(15020, cfg.modbus_port);
	TEST_ASSERT_EQUAL_UINT32(8, cfg.modbus_max_clients);
	TEST_ASSERT_EQUAL(CONFIG_LOG_DEBUG, cfg.log_level);
}

static void test_comments_blank_lines_and_whitespace_are_ignored(void)
{
	const char *text =
		"# full-line comment\n"
		"\n"
		"   \t  \n"
		"  min_off_s\t=   60   # trailing comment\r\n"
		"log_level=warn";	/* no spaces, no final newline */

	TEST_ASSERT_EQUAL_INT(0, parse(text));
	TEST_ASSERT_EQUAL_UINT32(60, cfg.core.min_off_s);
	TEST_ASSERT_EQUAL(CONFIG_LOG_WARN, cfg.log_level);
}

static void test_unknown_key_is_an_error_with_its_line_number(void)
{
	expect_error("min_off_s = 5\n\ncool_setpont_mc = 25000\n",
		     "config:3: unknown key 'cool_setpont_mc'");
}

static void test_line_without_equals_sign_is_an_error(void)
{
	expect_error("# ok\nmin_off_s 5\n", "config:2: expected 'key = value'");
}

static void test_missing_value_is_an_error(void)
{
	expect_error("min_off_s =  \n", "config:1: missing value for 'min_off_s'");
}

static void test_duplicate_key_is_an_error(void)
{
	expect_error("min_off_s = 5\nmin_off_s = 6\n", "config:2: duplicate key 'min_off_s'");
}

static void test_bad_numbers_are_errors(void)
{
	expect_error("min_off_s = abc\n", "config:1: value 'abc' for 'min_off_s' is not an integer");
	expect_error("min_off_s = 12x\n", "config:1: value '12x' for 'min_off_s' is not an integer");
	expect_error("min_off_s = 1.5\n", "config:1: value '1.5' for 'min_off_s' is not an integer");
	expect_error("min_off_s = 99999999999999999999\n",
		     "config:1: value '99999999999999999999' for 'min_off_s' is not an integer");
}

static void test_out_of_range_values_are_errors(void)
{
	expect_error("modbus_port = 0\n", "config:1: value 0 for 'modbus_port' is out of range 1..65535");
	expect_error("modbus_port = 65536\n",
		     "config:1: value 65536 for 'modbus_port' is out of range 1..65535");
	expect_error("loop_period_ms = 99\n",
		     "config:1: value 99 for 'loop_period_ms' is out of range 100..10000");
	expect_error("modbus_max_clients = 9\n",
		     "config:1: value 9 for 'modbus_max_clients' is out of range 1..8");
	expect_error("cool_setpoint_mc = -1\n",
		     "config:1: value -1 for 'cool_setpoint_mc' is out of range 5000..40000");
	expect_error("min_run_s = 3601\n",
		     "config:1: value 3601 for 'min_run_s' is out of range 0..3600");
}

static void test_range_limits_themselves_are_accepted(void)
{
	TEST_ASSERT_EQUAL_INT(0, parse("modbus_port = 1\nloop_period_ms = 10000\nmin_run_s = 0\n"));
	TEST_ASSERT_EQUAL_INT(0, parse("modbus_port = 65535\nloop_period_ms = 100\n"));
	TEST_ASSERT_EQUAL_UINT16(65535, cfg.modbus_port);
}

static void test_missing_keys_keep_their_defaults(void)
{
	TEST_ASSERT_EQUAL_INT(0, parse("modbus_port = 1502\n"));
	TEST_ASSERT_EQUAL_UINT16(1502, cfg.modbus_port);
	TEST_ASSERT_EQUAL_INT32(26000, cfg.core.cool_setpoint_mc);
	TEST_ASSERT_EQUAL_STRING("hvac_temp", cfg.iio_device_name);
}

static void test_heating_band_must_be_below_cooling_band(void)
{
	expect_error("# bands\nheat_setpoint_mc = 24000\nmodbus_port = 1502\n",
		     "config:2: heat_setpoint_mc + deadband_mc must be below cool_setpoint_mc - deadband_mc");
	expect_error("deadband_mc = 3000\ncool_setpoint_mc = 25000\n",
		     "config:2: heat_setpoint_mc + deadband_mc must be below cool_setpoint_mc - deadband_mc");
	TEST_ASSERT_EQUAL_INT(0, parse("heat_setpoint_mc = 23999\n"));
}

static void test_bad_log_level_is_an_error(void)
{
	expect_error("log_level = verbose\n",
		     "config:1: log_level must be error, warn, info or debug, not 'verbose'");
}

static void test_too_long_values_and_lines_are_errors(void)
{
	char text[400] = {0};
	char name[80] = {0};

	memset(name, 'a', 64);
	(void)snprintf(text, sizeof(text), "iio_device_name = %s\n", name);
	expect_error(text, "config:1: value for 'iio_device_name' is longer than 63 characters");
	memset(name, 'a', 63);
	name[63] = '\0';
	(void)snprintf(text, sizeof(text), "iio_device_name = %s\n", name);
	TEST_ASSERT_EQUAL_INT(0, parse(text));
	memset(text, ' ', 300);
	text[300] = '\0';
	expect_error(text, "config:1: line longer than 255 characters");
}

static void test_null_arguments_are_errors(void)
{
	TEST_ASSERT_EQUAL_INT(-EINVAL, config_parse_string(NULL, &cfg, err, sizeof(err)));
	TEST_ASSERT_EQUAL_INT(-EINVAL, config_parse_string("", NULL, NULL, 0));
	TEST_ASSERT_EQUAL_INT(-EINVAL, config_load_file(NULL, &cfg, err, sizeof(err)));
	config_set_defaults(NULL);	/* must not crash */
}

static void test_log_level_names(void)
{
	TEST_ASSERT_EQUAL_STRING("error", config_log_level_name(CONFIG_LOG_ERROR));
	TEST_ASSERT_EQUAL_STRING("debug", config_log_level_name(CONFIG_LOG_DEBUG));
	TEST_ASSERT_EQUAL_STRING("unknown", config_log_level_name((config_log_level_t)9));
}

/* Writes content to a fresh temporary file and returns its path in path_buf. */
static void write_temp_file(char *path_buf, size_t len, const char *content, size_t content_len)
{
	FILE *f = NULL;
	int fd = 0;

	(void)snprintf(path_buf, len, "/tmp/hvac_test_config_XXXXXX");
	fd = mkstemp(path_buf);
	TEST_ASSERT_TRUE(fd >= 0);
	f = fdopen(fd, "w");
	TEST_ASSERT_NOT_NULL(f);
	size_t written = fwrite(content, 1, content_len, f);
	int closed = fclose(f);

	TEST_ASSERT_EQUAL_size_t(content_len, written);
	TEST_ASSERT_EQUAL_INT(0, closed);
}

static void test_load_file_reads_and_parses(void)
{
	char path[64] = {0};
	const char *text = "min_off_s = 7\n";

	write_temp_file(path, sizeof(path), text, strlen(text));
	TEST_ASSERT_EQUAL_INT(0, config_load_file(path, &cfg, err, sizeof(err)));
	TEST_ASSERT_EQUAL_UINT32(7, cfg.core.min_off_s);
	TEST_ASSERT_EQUAL_INT(0, unlink(path));
}

static void test_load_file_errors(void)
{
	char path[64] = {0};
	static char big[CONFIG_FILE_MAX_BYTES + 2u];

	TEST_ASSERT_EQUAL_INT(-EINVAL, config_load_file("/nonexistent/hvacd.conf", &cfg, err,
							sizeof(err)));
	TEST_ASSERT_EQUAL_STRING("config:0: cannot open /nonexistent/hvacd.conf: "
				 "No such file or directory", err);

	memset(big, '#', sizeof(big));
	write_temp_file(path, sizeof(path), big, sizeof(big));
	TEST_ASSERT_EQUAL_INT(-EINVAL, config_load_file(path, &cfg, err, sizeof(err)));
	TEST_ASSERT_NOT_NULL(strstr(err, "is larger than 8192 bytes"));
	TEST_ASSERT_EQUAL_INT(0, unlink(path));

	write_temp_file(path, sizeof(path), "min_off_s = 7\n\0", 15);
	TEST_ASSERT_EQUAL_INT(-EINVAL, config_load_file(path, &cfg, err, sizeof(err)));
	TEST_ASSERT_NOT_NULL(strstr(err, "contains a NUL byte"));
	TEST_ASSERT_EQUAL_INT(0, unlink(path));
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_empty_text_gives_all_defaults);
	RUN_TEST(test_full_valid_file_sets_every_key);
	RUN_TEST(test_comments_blank_lines_and_whitespace_are_ignored);
	RUN_TEST(test_unknown_key_is_an_error_with_its_line_number);
	RUN_TEST(test_line_without_equals_sign_is_an_error);
	RUN_TEST(test_missing_value_is_an_error);
	RUN_TEST(test_duplicate_key_is_an_error);
	RUN_TEST(test_bad_numbers_are_errors);
	RUN_TEST(test_out_of_range_values_are_errors);
	RUN_TEST(test_range_limits_themselves_are_accepted);
	RUN_TEST(test_missing_keys_keep_their_defaults);
	RUN_TEST(test_heating_band_must_be_below_cooling_band);
	RUN_TEST(test_bad_log_level_is_an_error);
	RUN_TEST(test_too_long_values_and_lines_are_errors);
	RUN_TEST(test_null_arguments_are_errors);
	RUN_TEST(test_log_level_names);
	RUN_TEST(test_load_file_reads_and_parses);
	RUN_TEST(test_load_file_errors);
	return UNITY_END();
}
