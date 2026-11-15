// SPDX-License-Identifier: MIT
/*
 * Unit tests for sensor_iio: scale and raw parsing, the fixed-point conversion with rounding of
 * negative values, and device discovery and reading against a fake sysfs tree in a temp folder.
 */
#define _XOPEN_SOURCE 700	/* nftw() for removing the temp tree */
#include <errno.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sensor_iio.h"
#include "unity.h"

#define PATH_LEN 512u

static char base[PATH_LEN];

static void write_file(const char *dir, const char *name, const char *content)
{
	char path[PATH_LEN] = {0};
	FILE *f = NULL;

	TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/%s", dir, name) < (int)sizeof(path));
	f = fopen(path, "w");
	TEST_ASSERT_NOT_NULL(f);
	int put = fputs(content, f);
	int closed = fclose(f);

	TEST_ASSERT_TRUE(put >= 0);
	TEST_ASSERT_EQUAL_INT(0, closed);
}

/* Creates base/<entry>/ with name, in_temp_raw and in_temp_scale (NULL = file left out). */
static void make_device(const char *entry, const char *name, const char *raw, const char *scale)
{
	char dir[PATH_LEN] = {0};

	TEST_ASSERT_TRUE(snprintf(dir, sizeof(dir), "%s/%s", base, entry) < (int)sizeof(dir));
	TEST_ASSERT_EQUAL_INT(0, mkdir(dir, 0755));
	if (name != NULL)
		write_file(dir, "name", name);
	if (raw != NULL)
		write_file(dir, "in_temp_raw", raw);
	if (scale != NULL)
		write_file(dir, "in_temp_scale", scale);
}

void setUp(void)
{
	(void)snprintf(base, sizeof(base), "/tmp/hvac_test_iio_XXXXXX");
	TEST_ASSERT_NOT_NULL(mkdtemp(base));
}

static int remove_entry(const char *path, const struct stat *sb, int flag, struct FTW *ftw)
{
	(void)sb;
	(void)flag;
	(void)ftw;
	return remove(path);
}

void tearDown(void)
{
	/* Delete the private temp tree, children first (FTW_DEPTH). */
	TEST_ASSERT_EQUAL_INT(0, nftw(base, remove_entry, 8, FTW_DEPTH | FTW_PHYS));
}

static void test_scale_parsing_valid_values(void)
{
	int64_t s = 0;

	TEST_ASSERT_EQUAL_INT(0, sensor_iio_parse_scale("62.500000\n", &s));
	TEST_ASSERT_EQUAL_INT64(62500000, s);
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_parse_scale("1.000000", &s));
	TEST_ASSERT_EQUAL_INT64(1000000, s);
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_parse_scale("0.5", &s));
	TEST_ASSERT_EQUAL_INT64(500000, s);
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_parse_scale("3", &s));
	TEST_ASSERT_EQUAL_INT64(3000000, s);
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_parse_scale(".25", &s));
	TEST_ASSERT_EQUAL_INT64(250000, s);
}

static void test_scale_parsing_rejects_malformed_values(void)
{
	const char *bad[] = { "", "\n", "abc", "1.2.3", "62.5000001", "-1.0", "1,5", "1.5x",
			      "1000000.0", ".", NULL };
	int64_t s = 0;

	for (int i = 0; bad[i] != NULL; i++)
		TEST_ASSERT_EQUAL_INT_MESSAGE(-EINVAL, sensor_iio_parse_scale(bad[i], &s), bad[i]);
	TEST_ASSERT_EQUAL_INT(-EINVAL, sensor_iio_parse_scale(NULL, &s));
}

static void test_raw_parsing(void)
{
	int64_t r = 0;

	TEST_ASSERT_EQUAL_INT(0, sensor_iio_parse_raw("400\n", &r));
	TEST_ASSERT_EQUAL_INT64(400, r);
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_parse_raw("-2048", &r));
	TEST_ASSERT_EQUAL_INT64(-2048, r);
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_parse_raw("+7", &r));
	TEST_ASSERT_EQUAL_INT64(7, r);
	TEST_ASSERT_EQUAL_INT(-EINVAL, sensor_iio_parse_raw("", &r));
	TEST_ASSERT_EQUAL_INT(-EINVAL, sensor_iio_parse_raw("-", &r));
	TEST_ASSERT_EQUAL_INT(-EINVAL, sensor_iio_parse_raw("12a", &r));
	TEST_ASSERT_EQUAL_INT(-EINVAL, sensor_iio_parse_raw("99999999999999999999", &r));
	TEST_ASSERT_EQUAL_INT(-EINVAL, sensor_iio_parse_raw(NULL, &r));
}

static void test_conversion_rounds_half_away_from_zero(void)
{
	int32_t mc = 0;

	TEST_ASSERT_EQUAL_INT(0, sensor_iio_to_mc(400, 62500000, &mc));
	TEST_ASSERT_EQUAL_INT32(25000, mc);
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_to_mc(1, 62500000, &mc));
	TEST_ASSERT_EQUAL_INT32(63, mc);	/* 62.5 rounds up */
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_to_mc(-1, 62500000, &mc));
	TEST_ASSERT_EQUAL_INT32(-63, mc);	/* -62.5 rounds away from zero */
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_to_mc(-3, 62500000, &mc));
	TEST_ASSERT_EQUAL_INT32(-188, mc);	/* -187.5 */
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_to_mc(-640, 62500000, &mc));
	TEST_ASSERT_EQUAL_INT32(-40000, mc);
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_to_mc(-7, 400000, &mc));
	TEST_ASSERT_EQUAL_INT32(-3, mc);	/* -2.8 rounds to nearest */
	TEST_ASSERT_EQUAL_INT(-ERANGE, sensor_iio_to_mc(2000000, 62500000, &mc));
	TEST_ASSERT_EQUAL_INT(-ERANGE, sensor_iio_to_mc(1, -1, &mc));
	TEST_ASSERT_EQUAL_INT(-ERANGE, sensor_iio_to_mc(1000000, 999999999999, &mc));
}

static void test_reads_device_found_by_name(void)
{
	sensor_iio_t s;
	int32_t mc = 0;

	make_device("iio:device0", "other_sensor\n", "1\n", "1.000000\n");
	make_device("iio:device1", "hvac_temp\n", "-400\n", "62.500000\n");
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_init(&s, base, "hvac_temp"));
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_read_mc(&s, &mc));
	TEST_ASSERT_EQUAL_INT32(-25000, mc);
	TEST_ASSERT_TRUE(s.found);
	TEST_ASSERT_NOT_NULL(strstr(s.dev_dir, "iio:device1"));
}

static void test_missing_device_and_missing_files_fail(void)
{
	sensor_iio_t s;
	int32_t mc = 0;

	TEST_ASSERT_EQUAL_INT(0, sensor_iio_init(&s, base, "hvac_temp"));
	TEST_ASSERT_EQUAL_INT(-ENODEV, sensor_iio_read_mc(&s, &mc));
	make_device("iio:device0", "hvac_temp\n", NULL, "62.500000\n");
	TEST_ASSERT_EQUAL_INT(-ENOENT, sensor_iio_read_mc(&s, &mc));
	TEST_ASSERT_FALSE(s.found);
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_init(&s, "/nonexistent/iio", "hvac_temp"));
	TEST_ASSERT_EQUAL_INT(-ENOENT, sensor_iio_read_mc(&s, &mc));
}

static void test_malformed_files_fail(void)
{
	sensor_iio_t s;
	int32_t mc = 0;

	make_device("iio:device0", "hvac_temp\n", "abc\n", "62.500000\n");
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_init(&s, base, "hvac_temp"));
	TEST_ASSERT_EQUAL_INT(-EINVAL, sensor_iio_read_mc(&s, &mc));
}

static void test_device_is_rediscovered_after_failure(void)
{
	sensor_iio_t s;
	int32_t mc = 0;
	char dir[PATH_LEN] = {0};

	make_device("iio:device0", "hvac_temp\n", "400\n", "62.500000\n");
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_init(&s, base, "hvac_temp"));
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_read_mc(&s, &mc));
	/* The device disappears completely, as it does in sysfs when it is deleted. */
	TEST_ASSERT_TRUE(snprintf(dir, sizeof(dir), "%s/iio:device0", base) < (int)sizeof(dir));
	TEST_ASSERT_EQUAL_INT(0, nftw(dir, remove_entry, 8, FTW_DEPTH | FTW_PHYS));
	TEST_ASSERT_NOT_EQUAL(0, sensor_iio_read_mc(&s, &mc));
	make_device("iio:device3", "hvac_temp\n", "16\n", "62.500000\n");
	TEST_ASSERT_EQUAL_INT(0, sensor_iio_read_mc(&s, &mc));	/* found again, new number */
	TEST_ASSERT_EQUAL_INT32(1000, mc);
}

static void test_init_rejects_bad_arguments(void)
{
	sensor_iio_t s;
	char long_name[CONFIG_NAME_MAX + 4u] = {0};
	int32_t mc = 0;

	memset(long_name, 'x', CONFIG_NAME_MAX);
	TEST_ASSERT_EQUAL_INT(-EINVAL, sensor_iio_init(NULL, base, "hvac_temp"));
	TEST_ASSERT_EQUAL_INT(-EINVAL, sensor_iio_init(&s, NULL, "hvac_temp"));
	TEST_ASSERT_EQUAL_INT(-EINVAL, sensor_iio_init(&s, base, long_name));
	TEST_ASSERT_EQUAL_INT(-EINVAL, sensor_iio_read_mc(NULL, &mc));
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_scale_parsing_valid_values);
	RUN_TEST(test_scale_parsing_rejects_malformed_values);
	RUN_TEST(test_raw_parsing);
	RUN_TEST(test_conversion_rounds_half_away_from_zero);
	RUN_TEST(test_reads_device_found_by_name);
	RUN_TEST(test_missing_device_and_missing_files_fail);
	RUN_TEST(test_malformed_files_fail);
	RUN_TEST(test_device_is_rediscovered_after_failure);
	RUN_TEST(test_init_rejects_bad_arguments);
	return UNITY_END();
}
