// SPDX-License-Identifier: MIT
/*
 * Unit tests for the Modbus register map (PROJECT_PLAN.md section 4.6): unit conversion and
 * rounding, two's complement temperatures, bit fields, saturation and setpoint decoding.
 */
#include <string.h>

#include "modbus_regs.h"
#include "unity.h"

static uint16_t regs[MB_REG_COUNT];
static mb_status_t st;

void setUp(void)
{
	memset(regs, 0xAA, sizeof(regs));
	memset(&st, 0, sizeof(st));
	st.cool_setpoint_mc = 26000;
	st.heat_setpoint_mc = 20000;
}

void tearDown(void)
{
}

static void test_mc_to_cc_rounds_half_away_from_zero_and_clamps(void)
{
	TEST_ASSERT_EQUAL_INT16(2450, mb_mc_to_cc(24500));
	TEST_ASSERT_EQUAL_INT16(2451, mb_mc_to_cc(24505));
	TEST_ASSERT_EQUAL_INT16(2450, mb_mc_to_cc(24504));
	TEST_ASSERT_EQUAL_INT16(-6, mb_mc_to_cc(-63));
	TEST_ASSERT_EQUAL_INT16(-6, mb_mc_to_cc(-55));
	TEST_ASSERT_EQUAL_INT16(-5, mb_mc_to_cc(-54));
	TEST_ASSERT_EQUAL_INT16(INT16_MAX, mb_mc_to_cc(400000));
	TEST_ASSERT_EQUAL_INT16(INT16_MIN + 1, mb_mc_to_cc(-400000));
}

/* Regression (found in Ext 3): negating INT32_MIN overflowed (undefined behavior under UBSan). */
static void test_mc_to_cc_handles_the_int32_extremes(void)
{
	TEST_ASSERT_EQUAL_INT16(INT16_MIN + 1, mb_mc_to_cc(INT32_MIN));
	TEST_ASSERT_EQUAL_INT16(INT16_MIN + 1, mb_mc_to_cc(INT32_MIN + 1));
	TEST_ASSERT_EQUAL_INT16(INT16_MAX, mb_mc_to_cc(INT32_MAX));
}

static void test_no_reading_yet_publishes_minus_32768(void)
{
	mb_regs_publish(regs, &st);
	TEST_ASSERT_EQUAL_HEX16(0x8000, regs[MB_REG_TEMPERATURE]);
}

static void test_temperatures_are_twos_complement_centidegrees(void)
{
	st.has_temp = true;
	st.temp_mc = 24500;
	mb_regs_publish(regs, &st);
	TEST_ASSERT_EQUAL_UINT16(2450, regs[MB_REG_TEMPERATURE]);
	st.temp_mc = -5250;
	mb_regs_publish(regs, &st);
	TEST_ASSERT_EQUAL_HEX16(0xFDF3, regs[MB_REG_TEMPERATURE]);	/* -525 */
	TEST_ASSERT_EQUAL_UINT16(2600, regs[MB_REG_COOL_SETPOINT]);
	TEST_ASSERT_EQUAL_UINT16(2000, regs[MB_REG_HEAT_SETPOINT]);
}

static void test_state_outputs_and_alarm_bits(void)
{
	st.state = HVAC_STATE_COOLING;
	st.outputs.compressor = true;
	st.outputs.fan = true;
	mb_regs_publish(regs, &st);
	TEST_ASSERT_EQUAL_UINT16(1, regs[MB_REG_STATE]);
	TEST_ASSERT_EQUAL_HEX16(0x5, regs[MB_REG_OUTPUTS]);
	TEST_ASSERT_EQUAL_HEX16(0x0, regs[MB_REG_ALARMS]);

	memset(&st.outputs, 0, sizeof(st.outputs));
	st.outputs.heater = true;
	st.state = HVAC_STATE_FAULT;
	st.setpoint_rejected = true;
	mb_regs_publish(regs, &st);
	TEST_ASSERT_EQUAL_UINT16(3, regs[MB_REG_STATE]);
	TEST_ASSERT_EQUAL_HEX16(0x2, regs[MB_REG_OUTPUTS]);
	TEST_ASSERT_EQUAL_HEX16(0x3, regs[MB_REG_ALARMS]);
}

static void test_counters_saturate_and_version_is_one(void)
{
	st.compressor_starts = 70000;
	st.uptime_ms = 3u * 60000u + 59999u;
	st.diagnostics = 0x3;
	mb_regs_publish(regs, &st);
	TEST_ASSERT_EQUAL_UINT16(65535, regs[MB_REG_COMPRESSOR_STARTS]);
	TEST_ASSERT_EQUAL_UINT16(3, regs[MB_REG_UPTIME_MIN]);
	TEST_ASSERT_EQUAL_UINT16(3, regs[MB_REG_DIAGNOSTICS]);
	TEST_ASSERT_EQUAL_UINT16(1, regs[MB_REG_PROTOCOL_VERSION]);
	st.uptime_ms = 70000ull * 60000ull;
	mb_regs_publish(regs, &st);
	TEST_ASSERT_EQUAL_UINT16(65535, regs[MB_REG_UPTIME_MIN]);
}

static void test_setpoint_registers_decode_to_millidegrees(void)
{
	int32_t cool = 0;
	int32_t heat = 0;

	regs[MB_REG_COOL_SETPOINT] = 2450;
	regs[MB_REG_HEAT_SETPOINT] = 0xFFFF;	/* -1 cc */
	mb_regs_setpoints_mc(regs, &cool, &heat);
	TEST_ASSERT_EQUAL_INT32(24500, cool);
	TEST_ASSERT_EQUAL_INT32(-10, heat);
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_mc_to_cc_rounds_half_away_from_zero_and_clamps);
	RUN_TEST(test_mc_to_cc_handles_the_int32_extremes);
	RUN_TEST(test_no_reading_yet_publishes_minus_32768);
	RUN_TEST(test_temperatures_are_twos_complement_centidegrees);
	RUN_TEST(test_state_outputs_and_alarm_bits);
	RUN_TEST(test_counters_saturate_and_version_is_one);
	RUN_TEST(test_setpoint_registers_decode_to_millidegrees);
	return UNITY_END();
}
