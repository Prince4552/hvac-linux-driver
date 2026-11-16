// SPDX-License-Identifier: MIT
/*
 * Unit tests for hvac_fdd (Ext 2): each detector at and around its thresholds (window length and
 * minimum drop), reset when the compressor stops, invalid readings ignored, bits latch, disabled
 * detector, and configuration validation.
 */
#include <errno.h>

#include "hvac_fdd.h"
#include "unity.h"

#define MS_PER_S 1000u
#define WINDOW_MS (FDD_DEFAULT_INEFFECTIVE_WINDOW_S * MS_PER_S)
#define STUCK_MS (FDD_DEFAULT_STUCK_WINDOW_S * MS_PER_S)

static hvac_fdd_config_t cfg;
static hvac_fdd_t fdd;

void setUp(void)
{
	hvac_fdd_config_set_defaults(&cfg);
	TEST_ASSERT_EQUAL_INT(0, hvac_fdd_init(&fdd, &cfg));
}

void tearDown(void)
{
}

static uint16_t feed(bool compressor, bool valid, int32_t temp_mc, uint64_t now_ms)
{
	hvac_fdd_input_t in = { .compressor = compressor, .valid = valid, .temp_mc = temp_mc,
				.now_ms = now_ms };

	return hvac_fdd_step(&fdd, &in);
}

static void test_defaults_and_validation(void)
{
	hvac_fdd_config_t c = cfg;

	TEST_ASSERT_TRUE(cfg.enabled);
	TEST_ASSERT_EQUAL_UINT32(1200, cfg.ineffective_window_s);
	TEST_ASSERT_EQUAL_INT32(500, cfg.ineffective_min_drop_mc);
	TEST_ASSERT_EQUAL_UINT32(1800, cfg.stuck_window_s);
	TEST_ASSERT_EQUAL_INT(0, hvac_fdd_config_validate(&c));
	c.ineffective_window_s = FDD_WINDOW_MIN_S - 1u;
	TEST_ASSERT_EQUAL_INT(-EINVAL, hvac_fdd_config_validate(&c));
	c = cfg;
	c.stuck_window_s = FDD_WINDOW_MAX_S + 1u;
	TEST_ASSERT_EQUAL_INT(-EINVAL, hvac_fdd_config_validate(&c));
	c = cfg;
	c.ineffective_min_drop_mc = FDD_MIN_DROP_MIN_MC - 1;
	TEST_ASSERT_EQUAL_INT(-EINVAL, hvac_fdd_config_validate(&c));
	c.ineffective_min_drop_mc = FDD_MIN_DROP_MAX_MC + 1;
	TEST_ASSERT_EQUAL_INT(-EINVAL, hvac_fdd_init(&fdd, &c));
	TEST_ASSERT_EQUAL_INT(-EINVAL, hvac_fdd_config_validate(NULL));
	TEST_ASSERT_EQUAL_INT(-EINVAL, hvac_fdd_init(NULL, &cfg));
	hvac_fdd_config_set_defaults(NULL);	/* must not crash */
	TEST_ASSERT_EQUAL_UINT16(0, hvac_fdd_step(NULL, NULL));
}

static void test_drop_smaller_than_minimum_sets_cooling_ineffective(void)
{
	TEST_ASSERT_EQUAL_UINT16(0, feed(true, true, 27000, 0));
	TEST_ASSERT_EQUAL_UINT16(0, feed(true, true, 26600, WINDOW_MS - 1));	/* window not over */
	TEST_ASSERT_EQUAL_UINT16(FDD_COOLING_INEFFECTIVE, feed(true, true, 26501, WINDOW_MS));
}

static void test_drop_of_exactly_minimum_is_healthy(void)
{
	feed(true, true, 27000, 0);
	TEST_ASSERT_EQUAL_UINT16(0, feed(true, true, 26500, WINDOW_MS));	/* 500 m°C: enough */
}

static void test_each_window_has_a_new_reference(void)
{
	feed(true, true, 27000, 0);
	TEST_ASSERT_EQUAL_UINT16(0, feed(true, true, 25000, WINDOW_MS));	/* good window */
	TEST_ASSERT_EQUAL_UINT16(0, feed(true, true, 24600, 2 * WINDOW_MS - 1));
	TEST_ASSERT_EQUAL_UINT16(FDD_COOLING_INEFFECTIVE, feed(true, true, 24600, 2 * WINDOW_MS));
}

static void test_reference_resets_when_compressor_turns_off(void)
{
	feed(true, true, 27000, 0);
	feed(false, true, 26900, WINDOW_MS / 2);		/* compressor off: window ends */
	feed(true, true, 27000, WINDOW_MS / 2 + 1);		/* new window starts here */
	TEST_ASSERT_EQUAL_UINT16(0, feed(true, true, 26900, WINDOW_MS));	/* only half a window */
	TEST_ASSERT_EQUAL_UINT16(FDD_COOLING_INEFFECTIVE,
				 feed(true, true, 26900, WINDOW_MS / 2 + 1 + WINDOW_MS));
}

static void test_invalid_readings_are_ignored(void)
{
	feed(true, false, 0, 0);				/* no reference from a bad reading */
	feed(true, true, 27000, 1000);
	TEST_ASSERT_EQUAL_UINT16(0, feed(true, false, 99999, WINDOW_MS + 1000));
	TEST_ASSERT_EQUAL_UINT16(0, feed(true, true, 26000, WINDOW_MS + 1000));
}

static void test_unchanged_reading_for_the_window_sets_sensor_stuck(void)
{
	TEST_ASSERT_EQUAL_UINT16(0, feed(false, true, 23000, 0));
	TEST_ASSERT_EQUAL_UINT16(0, feed(false, true, 23000, STUCK_MS - 1));
	TEST_ASSERT_EQUAL_UINT16(FDD_SENSOR_STUCK, feed(false, true, 23000, STUCK_MS));
}

static void test_any_change_restarts_the_stuck_timer(void)
{
	feed(false, true, 23000, 0);
	feed(false, true, 23063, STUCK_MS - 1);		/* one step changed: timer restarts */
	TEST_ASSERT_EQUAL_UINT16(0, feed(false, true, 23063, 2 * STUCK_MS - 2));
	TEST_ASSERT_EQUAL_UINT16(FDD_SENSOR_STUCK, feed(false, true, 23063, 2 * STUCK_MS - 1));
}

static void test_bits_latch_and_combine(void)
{
	feed(true, true, 27000, 0);
	feed(true, true, 27000, WINDOW_MS);			/* no drop: bit0 */
	TEST_ASSERT_EQUAL_UINT16(FDD_COOLING_INEFFECTIVE | FDD_SENSOR_STUCK,
				 feed(true, true, 27000, STUCK_MS));
	TEST_ASSERT_EQUAL_UINT16(FDD_COOLING_INEFFECTIVE | FDD_SENSOR_STUCK,
				 feed(false, true, 20000, STUCK_MS + 1000));	/* stays set */
}

static void test_disabled_detector_reports_nothing(void)
{
	cfg.enabled = false;
	TEST_ASSERT_EQUAL_INT(0, hvac_fdd_init(&fdd, &cfg));
	feed(true, true, 27000, 0);
	TEST_ASSERT_EQUAL_UINT16(0, feed(true, true, 27000, 10 * STUCK_MS));
}

static void test_clock_going_backwards_counts_as_no_time(void)
{
	feed(false, true, 23000, STUCK_MS * 3);
	TEST_ASSERT_EQUAL_UINT16(0, feed(false, true, 23000, 10));
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_defaults_and_validation);
	RUN_TEST(test_drop_smaller_than_minimum_sets_cooling_ineffective);
	RUN_TEST(test_drop_of_exactly_minimum_is_healthy);
	RUN_TEST(test_each_window_has_a_new_reference);
	RUN_TEST(test_reference_resets_when_compressor_turns_off);
	RUN_TEST(test_invalid_readings_are_ignored);
	RUN_TEST(test_unchanged_reading_for_the_window_sets_sensor_stuck);
	RUN_TEST(test_any_change_restarts_the_stuck_timer);
	RUN_TEST(test_bits_latch_and_combine);
	RUN_TEST(test_disabled_detector_reports_nothing);
	RUN_TEST(test_clock_going_backwards_counts_as_no_time);
	return UNITY_END();
}
