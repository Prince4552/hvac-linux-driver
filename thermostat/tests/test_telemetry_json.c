// SPDX-License-Identifier: MIT
/*
 * Unit tests for telemetry_json (Ext 3): exact JSON for typical values, negative and rounded
 * temperatures, "null" before the first reading, extreme values, and truncation reporting
 * (a buffer one byte too small fails with -ENOSPC; an exact fit succeeds).
 */
#include <errno.h>
#include <string.h>

#include "telemetry_json.h"
#include "unity.h"

static telemetry_t t;
static char buf[TELEMETRY_JSON_MAX];

void setUp(void)
{
	t = (telemetry_t){
		.device = "hvac-01", .seq = 7, .uptime_s = 35, .has_temp = true, .temp_mc = 27062,
		.state = HVAC_STATE_COOLING, .compressor = true, .heater = false, .fan = true,
		.alarms = 0, .fdd = 0, .compressor_starts = 1,
	};
	memset(buf, 0, sizeof(buf));
}

void tearDown(void)
{
}

static void test_typical_sample_gives_exact_json(void)
{
	const char *want = "{\"device\":\"hvac-01\",\"seq\":7,\"uptime_s\":35,\"temp_c\":27.06,"
			   "\"state\":\"COOLING\",\"compressor\":true,\"heater\":false,\"fan\":true,"
			   "\"alarms\":0,\"fdd\":0,\"compressor_starts\":1}";

	TEST_ASSERT_EQUAL_INT((int)strlen(want), telemetry_format_json(&t, buf, sizeof(buf)));
	TEST_ASSERT_EQUAL_STRING(want, buf);
}

/* Same rounding as Modbus register 0: half away from zero, to centidegrees. */
static void test_temperatures_round_like_the_modbus_register(void)
{
	const int32_t mc[] = { 0, 5, 4, -5, -4, -500, -10062, 125000, -40000 };
	const char *text[] = { "\"temp_c\":0.00,", "\"temp_c\":0.01,", "\"temp_c\":0.00,",
			       "\"temp_c\":-0.01,", "\"temp_c\":0.00,", "\"temp_c\":-0.50,",
			       "\"temp_c\":-10.06,", "\"temp_c\":125.00,", "\"temp_c\":-40.00," };

	for (size_t i = 0; i < sizeof(mc) / sizeof(mc[0]); i++) {
		t.temp_mc = mc[i];
		TEST_ASSERT_GREATER_THAN_INT(0, telemetry_format_json(&t, buf, sizeof(buf)));
		TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, text[i]), text[i]);
	}
}

static void test_missing_reading_is_null(void)
{
	t.has_temp = false;
	t.state = HVAC_STATE_FAULT;
	TEST_ASSERT_GREATER_THAN_INT(0, telemetry_format_json(&t, buf, sizeof(buf)));
	TEST_ASSERT_NOT_NULL(strstr(buf, "\"temp_c\":null,\"state\":\"FAULT\""));
}

static void test_bits_and_counters_are_plain_numbers(void)
{
	t.alarms = 3;
	t.fdd = 2;
	t.compressor = false;
	t.heater = true;
	t.fan = false;
	t.state = HVAC_STATE_HEATING;
	TEST_ASSERT_GREATER_THAN_INT(0, telemetry_format_json(&t, buf, sizeof(buf)));
	TEST_ASSERT_NOT_NULL(strstr(buf, "\"state\":\"HEATING\",\"compressor\":false,"
				    "\"heater\":true,\"fan\":false,\"alarms\":3,\"fdd\":2,"));
}

/* The largest possible message still fits the daemon's 512-byte buffer. */
static void test_extreme_values_fit_the_buffer(void)
{
	t.device = "0123456789abcdef0123456789abcdef";	/* 32 characters, the maximum */
	t.seq = UINT32_MAX;
	t.uptime_s = UINT64_MAX;
	t.temp_mc = INT32_MIN;
	t.alarms = UINT16_MAX;
	t.fdd = UINT16_MAX;
	t.compressor_starts = UINT32_MAX;
	TEST_ASSERT_GREATER_THAN_INT(0, telemetry_format_json(&t, buf, sizeof(buf)));
	TEST_ASSERT_NOT_NULL(strstr(buf, "\"seq\":4294967295,\"uptime_s\":18446744073709551615,"
				    "\"temp_c\":-327.67,"));
}

static void test_too_small_buffer_reports_truncation(void)
{
	char small[200] = {0};
	int n = telemetry_format_json(&t, buf, sizeof(buf));

	TEST_ASSERT_GREATER_THAN_INT(0, n);
	/* Exactly the text plus its NUL fits; one byte less does not. */
	TEST_ASSERT_EQUAL_INT(n, telemetry_format_json(&t, small, (size_t)n + 1u));
	TEST_ASSERT_EQUAL_INT(-ENOSPC, telemetry_format_json(&t, small, (size_t)n));
	TEST_ASSERT_EQUAL_INT(-ENOSPC, telemetry_format_json(&t, small, 1u));
}

static void test_missing_arguments_are_rejected(void)
{
	TEST_ASSERT_EQUAL_INT(-EINVAL, telemetry_format_json(NULL, buf, sizeof(buf)));
	TEST_ASSERT_EQUAL_INT(-EINVAL, telemetry_format_json(&t, NULL, sizeof(buf)));
	TEST_ASSERT_EQUAL_INT(-EINVAL, telemetry_format_json(&t, buf, 0u));
	t.device = NULL;
	TEST_ASSERT_EQUAL_INT(-EINVAL, telemetry_format_json(&t, buf, sizeof(buf)));
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_typical_sample_gives_exact_json);
	RUN_TEST(test_temperatures_round_like_the_modbus_register);
	RUN_TEST(test_missing_reading_is_null);
	RUN_TEST(test_bits_and_counters_are_plain_numbers);
	RUN_TEST(test_extreme_values_fit_the_buffer);
	RUN_TEST(test_too_small_buffer_reports_truncation);
	RUN_TEST(test_missing_arguments_are_rejected);
	return UNITY_END();
}
