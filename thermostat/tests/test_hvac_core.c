// SPDX-License-Identifier: MIT
/*
 * Unit tests for hvac_core (PROJECT_PLAN.md section 4.4): every transition with exact boundaries
 * (one millidegree either side), minimum run/off timers with fake time (no sleeping), FAULT entry
 * and recovery, implausible jumps, validation, start counting and the outputs mapping.
 */
#include <errno.h>
#include <string.h>

#include "hvac_core.h"
#include "unity.h"

#define MS_PER_S 1000u
#define MIN_RUN_MS (HVAC_DEFAULT_MIN_RUN_S * MS_PER_S)
#define MIN_OFF_MS (HVAC_DEFAULT_MIN_OFF_S * MS_PER_S)
#define COOL_ON_MC 27000	/* 26000 + 1000 */
#define COOL_OFF_MC 25000	/* 26000 - 1000 */
#define HEAT_ON_MC 19000	/* 20000 - 1000 */
#define HEAT_OFF_MC 21000	/* 20000 + 1000 */
#define COMFORT_MC 23000

static hvac_config_t cfg;
static hvac_ctx_t ctx;
static hvac_outputs_t out;

void setUp(void)
{
	hvac_config_set_defaults(&cfg);
	TEST_ASSERT_EQUAL_INT(0, hvac_core_init(&ctx, &cfg));
	memset(&out, 0, sizeof(out));
}

void tearDown(void)
{
}

static hvac_state_t step(int32_t temp_mc, uint64_t now_ms)
{
	hvac_input_t in = { .read_ok = true, .temp_mc = temp_mc, .now_ms = now_ms };

	return hvac_core_step(&ctx, &in, &out);
}

static hvac_state_t step_read_error(uint64_t now_ms)
{
	hvac_input_t in = { .read_ok = false, .temp_mc = 0, .now_ms = now_ms };

	return hvac_core_step(&ctx, &in, &out);
}

/* Moves the controller to COOLING at time t (first start, so no off timer). */
static void start_cooling(uint64_t t)
{
	TEST_ASSERT_EQUAL(HVAC_STATE_COOLING, step(COOL_ON_MC, t));
}

/* Walks the temperature down in plausible steps (<= max_step) and stops cooling at time t. */
static void stop_cooling(uint64_t t)
{
	TEST_ASSERT_EQUAL(HVAC_STATE_COOLING, step(26000, t));
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(COOL_OFF_MC, t));
}

/* ------------------------------------------------------------------ init and validation */

static void test_init_starts_idle_with_all_outputs_off(void)
{
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(COMFORT_MC, 0));
	TEST_ASSERT_FALSE(out.compressor);
	TEST_ASSERT_FALSE(out.heater);
	TEST_ASSERT_FALSE(out.fan);
	TEST_ASSERT_FALSE(out.alarm);
	TEST_ASSERT_EQUAL_UINT32(0, hvac_core_compressor_starts(&ctx));
}

static void test_defaults_match_the_plan(void)
{
	TEST_ASSERT_EQUAL_INT32(26000, cfg.cool_setpoint_mc);
	TEST_ASSERT_EQUAL_INT32(20000, cfg.heat_setpoint_mc);
	TEST_ASSERT_EQUAL_INT32(1000, cfg.deadband_mc);
	TEST_ASSERT_EQUAL_UINT32(180, cfg.min_off_s);
	TEST_ASSERT_EQUAL_UINT32(120, cfg.min_run_s);
	TEST_ASSERT_EQUAL_UINT32(5, cfg.fault_recover_count);
	TEST_ASSERT_EQUAL_INT32(5000, cfg.max_step_mc);
	TEST_ASSERT_EQUAL_INT(0, hvac_config_validate(&cfg));
}

static void test_init_rejects_invalid_config_and_leaves_ctx_unchanged(void)
{
	hvac_ctx_t before = ctx;
	hvac_config_t bad = cfg;

	bad.deadband_mc = 99;
	TEST_ASSERT_EQUAL_INT(-EINVAL, hvac_core_init(&ctx, &bad));
	TEST_ASSERT_EQUAL_MEMORY(&before, &ctx, sizeof(ctx));
}

static void test_null_arguments_are_rejected(void)
{
	TEST_ASSERT_EQUAL_INT(-EINVAL, hvac_config_validate(NULL));
	TEST_ASSERT_EQUAL_INT(-EINVAL, hvac_core_init(NULL, &cfg));
	TEST_ASSERT_EQUAL_INT(-EINVAL, hvac_core_init(&ctx, NULL));
	TEST_ASSERT_EQUAL_INT(-EINVAL, hvac_core_set_setpoints(NULL, 26000, 20000));
	TEST_ASSERT_EQUAL_UINT32(0, hvac_core_compressor_starts(NULL));
	TEST_ASSERT_EQUAL(HVAC_STATE_FAULT, hvac_core_step(NULL, NULL, NULL));
	TEST_ASSERT_EQUAL(HVAC_STATE_FAULT, hvac_core_step(&ctx, NULL, &out));
	hvac_config_set_defaults(NULL);	/* must not crash */
}

/* Checks that field changes are accepted (ok) or rejected, starting from the defaults. */
static void expect_valid(hvac_config_t c, int ok)
{
	TEST_ASSERT_EQUAL_INT(ok ? 0 : -EINVAL, hvac_config_validate(&c));
}

static void test_validate_setpoint_ranges(void)
{
	hvac_config_t c = cfg;

	c.cool_setpoint_mc = 40000;
	expect_valid(c, 1);
	c.cool_setpoint_mc = 40001;
	expect_valid(c, 0);
	c = cfg;
	c.cool_setpoint_mc = 4999;
	expect_valid(c, 0);
	c = cfg;
	c.heat_setpoint_mc = 5000;
	expect_valid(c, 1);
	c.heat_setpoint_mc = 4999;
	expect_valid(c, 0);
	c = cfg;
	c.heat_setpoint_mc = 40001;
	expect_valid(c, 0);
}

static void test_validate_deadband_and_step_ranges(void)
{
	hvac_config_t c = cfg;

	c.deadband_mc = 100;
	expect_valid(c, 1);
	c.deadband_mc = 99;
	expect_valid(c, 0);
	c.heat_setpoint_mc = 10000;
	c.deadband_mc = 5000;
	expect_valid(c, 1);
	c.deadband_mc = 5001;
	expect_valid(c, 0);
	c = cfg;
	c.max_step_mc = 500;
	expect_valid(c, 1);
	c.max_step_mc = 499;
	expect_valid(c, 0);
	c.max_step_mc = 20000;
	expect_valid(c, 1);
	c.max_step_mc = 20001;
	expect_valid(c, 0);
}

static void test_validate_timer_and_recovery_ranges(void)
{
	hvac_config_t c = cfg;

	c.min_off_s = 0;
	c.min_run_s = 0;
	expect_valid(c, 1);
	c.min_off_s = 3600;
	c.min_run_s = 3600;
	expect_valid(c, 1);
	c.min_off_s = 3601;
	expect_valid(c, 0);
	c.min_off_s = 3600;
	c.min_run_s = 3601;
	expect_valid(c, 0);
	c = cfg;
	c.fault_recover_count = 1;
	expect_valid(c, 1);
	c.fault_recover_count = 0;
	expect_valid(c, 0);
	c.fault_recover_count = 100;
	expect_valid(c, 1);
	c.fault_recover_count = 101;
	expect_valid(c, 0);
}

static void test_validate_heating_band_must_be_below_cooling_band(void)
{
	hvac_config_t c = cfg;

	c.heat_setpoint_mc = 23999;	/* 23999 + 1000 = 24999 < 25000 = 26000 - 1000 */
	expect_valid(c, 1);
	c.heat_setpoint_mc = 24000;	/* 25000 == 25000: bands touch, rejected */
	expect_valid(c, 0);
	c.heat_setpoint_mc = 30000;	/* heating above cooling */
	expect_valid(c, 0);
}

/* ------------------------------------------------------------------ IDLE transitions */

static void test_idle_to_cooling_exactly_at_cool_setpoint_plus_deadband(void)
{
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(COOL_ON_MC - 1, 0));
	TEST_ASSERT_EQUAL(HVAC_STATE_COOLING, step(COOL_ON_MC, 1000));
	TEST_ASSERT_EQUAL_UINT32(1, hvac_core_compressor_starts(&ctx));
}

static void test_idle_to_heating_exactly_at_heat_setpoint_minus_deadband(void)
{
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(HEAT_ON_MC + 1, 0));
	TEST_ASSERT_EQUAL(HVAC_STATE_HEATING, step(HEAT_ON_MC, 1000));
	TEST_ASSERT_EQUAL_UINT32(0, hvac_core_compressor_starts(&ctx));
}

static void test_idle_stays_idle_inside_the_bands(void)
{
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(COOL_OFF_MC, 0));
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(HEAT_OFF_MC, 1000));
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(COMFORT_MC, 2000));
}

static void test_first_compressor_start_is_allowed_immediately(void)
{
	start_cooling(0);	/* time 0: no minimum-off wait before the very first start */
	TEST_ASSERT_TRUE(out.compressor);
}

/* ------------------------------------------------------------------ COOLING and timers */

static void test_cooling_stops_exactly_at_cool_setpoint_minus_deadband(void)
{
	start_cooling(0);
	TEST_ASSERT_EQUAL(HVAC_STATE_COOLING, step(26000, MIN_RUN_MS));
	TEST_ASSERT_EQUAL(HVAC_STATE_COOLING, step(COOL_OFF_MC + 1, MIN_RUN_MS));
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(COOL_OFF_MC, MIN_RUN_MS));
}

static void test_cooling_keeps_running_until_min_run_time(void)
{
	start_cooling(1000);
	TEST_ASSERT_EQUAL(HVAC_STATE_COOLING, step(26000, 1000 + MIN_RUN_MS - 1));
	TEST_ASSERT_EQUAL(HVAC_STATE_COOLING, step(COOL_OFF_MC, 1000 + MIN_RUN_MS - 1));
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(COOL_OFF_MC, 1000 + MIN_RUN_MS));
}

static void test_compressor_restart_waits_for_min_off_time(void)
{
	uint64_t off_at = MIN_RUN_MS;

	start_cooling(0);
	stop_cooling(off_at);
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(29000, off_at + 1000));
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(30000, off_at + MIN_OFF_MS - 1));
	TEST_ASSERT_FALSE(out.compressor);
	TEST_ASSERT_EQUAL(HVAC_STATE_COOLING, step(30000, off_at + MIN_OFF_MS));
	TEST_ASSERT_EQUAL_UINT32(2, hvac_core_compressor_starts(&ctx));
}

static void test_clock_going_backwards_counts_as_no_time_elapsed(void)
{
	start_cooling(10 * MIN_RUN_MS);
	TEST_ASSERT_EQUAL(HVAC_STATE_COOLING, step(26000, 500));
	TEST_ASSERT_EQUAL(HVAC_STATE_COOLING, step(COOL_OFF_MC, 500));
}

/* ------------------------------------------------------------------ HEATING */

static void test_heating_stops_exactly_at_heat_setpoint_plus_deadband(void)
{
	TEST_ASSERT_EQUAL(HVAC_STATE_HEATING, step(HEAT_ON_MC, 0));
	TEST_ASSERT_EQUAL(HVAC_STATE_HEATING, step(HEAT_OFF_MC - 1, 1000));
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(HEAT_OFF_MC, 2000));
}

static void test_heating_has_no_timers(void)
{
	TEST_ASSERT_EQUAL(HVAC_STATE_HEATING, step(HEAT_ON_MC, 0));
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(HEAT_OFF_MC, 1));
	TEST_ASSERT_EQUAL(HVAC_STATE_HEATING, step(HEAT_ON_MC, 2));
}

/* ------------------------------------------------------------------ FAULT */

static void test_read_error_from_idle_enters_fault(void)
{
	step(COMFORT_MC, 0);
	TEST_ASSERT_EQUAL(HVAC_STATE_FAULT, step_read_error(1000));
	TEST_ASSERT_TRUE(out.alarm);
	TEST_ASSERT_FALSE(out.fan);
}

static void test_read_error_from_heating_enters_fault_and_stops_heater(void)
{
	step(HEAT_ON_MC, 0);
	TEST_ASSERT_EQUAL(HVAC_STATE_FAULT, step_read_error(1000));
	TEST_ASSERT_FALSE(out.heater);
	TEST_ASSERT_TRUE(out.alarm);
}

static void test_read_error_from_cooling_starts_min_off_timer(void)
{
	uint32_t i = 0;

	start_cooling(0);
	TEST_ASSERT_EQUAL(HVAC_STATE_FAULT, step_read_error(5000));
	TEST_ASSERT_FALSE(out.compressor);
	for (i = 0; i < HVAC_DEFAULT_FAULT_RECOVER_COUNT; i++)
		step(COOL_ON_MC, 6000 + i);
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, ctx.state);
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(COOL_ON_MC, 5000 + MIN_OFF_MS - 1));
	TEST_ASSERT_EQUAL(HVAC_STATE_COOLING, step(COOL_ON_MC, 5000 + MIN_OFF_MS));
}

static void test_out_of_range_readings_are_invalid_at_exact_limits(void)
{
	TEST_ASSERT_EQUAL(HVAC_STATE_FAULT, step(HVAC_TEMP_MAX_MC + 1, 0));
	setUp();
	TEST_ASSERT_EQUAL(HVAC_STATE_FAULT, step(HVAC_TEMP_MIN_MC - 1, 0));
	setUp();
	TEST_ASSERT_NOT_EQUAL(HVAC_STATE_FAULT, step(HVAC_TEMP_MAX_MC, 0));
	setUp();
	TEST_ASSERT_NOT_EQUAL(HVAC_STATE_FAULT, step(HVAC_TEMP_MIN_MC, 0));
}

static void test_implausible_jump_enters_fault(void)
{
	step(22000, 0);
	/* A change of exactly max_step is still valid (and here also starts cooling). */
	TEST_ASSERT_EQUAL(HVAC_STATE_COOLING, step(22000 + 5000, 1000));
	TEST_ASSERT_EQUAL(HVAC_STATE_FAULT, step(27000 + 5001, 2000));
}

static void test_implausible_downward_jump_enters_fault(void)
{
	step(22000, 0);
	TEST_ASSERT_EQUAL(HVAC_STATE_FAULT, step(22000 - 5001, 1000));
}

static void test_fault_recovers_after_exactly_recover_count_good_readings(void)
{
	uint32_t i = 0;

	step_read_error(0);
	for (i = 1; i < HVAC_DEFAULT_FAULT_RECOVER_COUNT; i++)
		TEST_ASSERT_EQUAL(HVAC_STATE_FAULT, step(COMFORT_MC, i * 1000u));
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(COMFORT_MC, 9000));
	TEST_ASSERT_FALSE(out.alarm);
}

static void test_bad_reading_in_fault_restarts_the_recovery_count(void)
{
	uint32_t i = 0;

	step_read_error(0);
	for (i = 1; i < HVAC_DEFAULT_FAULT_RECOVER_COUNT; i++)
		step(COMFORT_MC, i);
	TEST_ASSERT_EQUAL(HVAC_STATE_FAULT, step_read_error(100));
	TEST_ASSERT_EQUAL_UINT32(0, ctx.good_count);
	for (i = 1; i < HVAC_DEFAULT_FAULT_RECOVER_COUNT; i++)
		TEST_ASSERT_EQUAL(HVAC_STATE_FAULT, step(COMFORT_MC, 100 + i));
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(COMFORT_MC, 200));
}

static void test_recovery_goes_to_idle_first_even_when_hot(void)
{
	uint32_t i = 0;

	step_read_error(0);
	for (i = 1; i < HVAC_DEFAULT_FAULT_RECOVER_COUNT; i++)
		step(30000, i);
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(30000, 10));	/* one transition per step */
	TEST_ASSERT_EQUAL(HVAC_STATE_COOLING, step(30000, 11));
}

static void test_valid_readings_in_fault_update_the_jump_reference(void)
{
	step(22000, 0);
	step_read_error(1);
	step(26000, 2);			/* valid: 4000 from 22000 */
	TEST_ASSERT_EQUAL_INT32(26000, ctx.last_valid_mc);
	step(30500, 3);			/* valid only relative to 26000 */
	TEST_ASSERT_EQUAL_INT32(30500, ctx.last_valid_mc);
	TEST_ASSERT_EQUAL_UINT32(2, ctx.good_count);
}

static void test_failed_reads_do_not_change_the_jump_reference(void)
{
	step(22000, 0);
	step_read_error(1);
	TEST_ASSERT_EQUAL_INT32(22000, ctx.last_valid_mc);
	TEST_ASSERT_TRUE(ctx.has_last_valid);
}

/* ------------------------------------------------------------------ setpoints and outputs */

static void test_valid_setpoint_change_moves_the_thresholds(void)
{
	TEST_ASSERT_EQUAL_INT(0, hvac_core_set_setpoints(&ctx, 24000, 18000));
	TEST_ASSERT_EQUAL(HVAC_STATE_IDLE, step(24999, 0));
	TEST_ASSERT_EQUAL(HVAC_STATE_COOLING, step(25000, 1000));
}

static void test_invalid_setpoint_change_is_rejected_without_effect(void)
{
	TEST_ASSERT_EQUAL_INT(-EINVAL, hvac_core_set_setpoints(&ctx, 40001, 20000));
	TEST_ASSERT_EQUAL_INT(-EINVAL, hvac_core_set_setpoints(&ctx, 22000, 21000));
	TEST_ASSERT_EQUAL_INT32(26000, ctx.cfg.cool_setpoint_mc);
	TEST_ASSERT_EQUAL_INT32(20000, ctx.cfg.heat_setpoint_mc);
}

static void test_compressor_starts_count_only_cooling_starts(void)
{
	start_cooling(0);
	stop_cooling(MIN_RUN_MS);
	step(22000, MIN_RUN_MS + 1);
	step(HEAT_ON_MC, MIN_RUN_MS + 2);	/* heating does not count */
	TEST_ASSERT_EQUAL_UINT32(1, hvac_core_compressor_starts(&ctx));
}

static void test_outputs_follow_the_state(void)
{
	start_cooling(0);
	TEST_ASSERT_TRUE(out.compressor && out.fan && !out.heater && !out.alarm);
	setUp();
	step(HEAT_ON_MC, 0);
	TEST_ASSERT_TRUE(out.heater && out.fan && !out.compressor && !out.alarm);
}

static void test_state_names(void)
{
	TEST_ASSERT_EQUAL_STRING("IDLE", hvac_state_name(HVAC_STATE_IDLE));
	TEST_ASSERT_EQUAL_STRING("COOLING", hvac_state_name(HVAC_STATE_COOLING));
	TEST_ASSERT_EQUAL_STRING("HEATING", hvac_state_name(HVAC_STATE_HEATING));
	TEST_ASSERT_EQUAL_STRING("FAULT", hvac_state_name(HVAC_STATE_FAULT));
	TEST_ASSERT_EQUAL_STRING("UNKNOWN", hvac_state_name((hvac_state_t)7));
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_init_starts_idle_with_all_outputs_off);
	RUN_TEST(test_defaults_match_the_plan);
	RUN_TEST(test_init_rejects_invalid_config_and_leaves_ctx_unchanged);
	RUN_TEST(test_null_arguments_are_rejected);
	RUN_TEST(test_validate_setpoint_ranges);
	RUN_TEST(test_validate_deadband_and_step_ranges);
	RUN_TEST(test_validate_timer_and_recovery_ranges);
	RUN_TEST(test_validate_heating_band_must_be_below_cooling_band);
	RUN_TEST(test_idle_to_cooling_exactly_at_cool_setpoint_plus_deadband);
	RUN_TEST(test_idle_to_heating_exactly_at_heat_setpoint_minus_deadband);
	RUN_TEST(test_idle_stays_idle_inside_the_bands);
	RUN_TEST(test_first_compressor_start_is_allowed_immediately);
	RUN_TEST(test_cooling_stops_exactly_at_cool_setpoint_minus_deadband);
	RUN_TEST(test_cooling_keeps_running_until_min_run_time);
	RUN_TEST(test_compressor_restart_waits_for_min_off_time);
	RUN_TEST(test_clock_going_backwards_counts_as_no_time_elapsed);
	RUN_TEST(test_heating_stops_exactly_at_heat_setpoint_plus_deadband);
	RUN_TEST(test_heating_has_no_timers);
	RUN_TEST(test_read_error_from_idle_enters_fault);
	RUN_TEST(test_read_error_from_heating_enters_fault_and_stops_heater);
	RUN_TEST(test_read_error_from_cooling_starts_min_off_timer);
	RUN_TEST(test_out_of_range_readings_are_invalid_at_exact_limits);
	RUN_TEST(test_implausible_jump_enters_fault);
	RUN_TEST(test_implausible_downward_jump_enters_fault);
	RUN_TEST(test_fault_recovers_after_exactly_recover_count_good_readings);
	RUN_TEST(test_bad_reading_in_fault_restarts_the_recovery_count);
	RUN_TEST(test_recovery_goes_to_idle_first_even_when_hot);
	RUN_TEST(test_valid_readings_in_fault_update_the_jump_reference);
	RUN_TEST(test_failed_reads_do_not_change_the_jump_reference);
	RUN_TEST(test_valid_setpoint_change_moves_the_thresholds);
	RUN_TEST(test_invalid_setpoint_change_is_rejected_without_effect);
	RUN_TEST(test_compressor_starts_count_only_cooling_starts);
	RUN_TEST(test_outputs_follow_the_state);
	RUN_TEST(test_state_names);
	return UNITY_END();
}
