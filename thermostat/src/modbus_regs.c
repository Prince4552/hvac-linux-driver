// SPDX-License-Identifier: MIT
/*
 * modbus_regs: pure conversion between hvacd's status and its Modbus holding registers.
 * Registers carry 16-bit words; signed temperatures use two's complement (int16).
 */
#include "modbus_regs.h"

#include <stddef.h>

#define MC_PER_CC 10
#define MS_PER_MIN 60000u

int16_t mb_mc_to_cc(int32_t mc)
{
	/* 64-bit arithmetic: negating or adding to an int32 extreme would overflow. */
	int64_t m = mc;
	int64_t cc = m >= 0 ? (m + MC_PER_CC / 2) / MC_PER_CC : -((-m + MC_PER_CC / 2) / MC_PER_CC);

	if (cc > INT16_MAX)
		return INT16_MAX;
	if (cc < INT16_MIN + 1)
		return INT16_MIN + 1;	/* INT16_MIN is reserved for "no reading yet" */
	return (int16_t)cc;
}

static uint16_t saturate_u16(uint64_t v)
{
	return v > UINT16_MAX ? (uint16_t)UINT16_MAX : (uint16_t)v;
}

/* int16 -> register word: the bit pattern of two's complement (well defined for unsigned). */
static uint16_t s16_word(int16_t v)
{
	return (uint16_t)(v < 0 ? (int32_t)v + 0x10000 : (int32_t)v);
}

void mb_regs_publish(uint16_t regs[MB_REG_COUNT], const mb_status_t *st)
{
	uint16_t outputs = 0;
	uint16_t alarms = 0;

	if (st->outputs.compressor)
		outputs |= MB_OUT_COMPRESSOR;
	if (st->outputs.heater)
		outputs |= MB_OUT_HEATER;
	if (st->outputs.fan)
		outputs |= MB_OUT_FAN;
	if (st->state == HVAC_STATE_FAULT)
		alarms |= MB_ALARM_SENSOR_FAULT;
	if (st->setpoint_rejected)
		alarms |= MB_ALARM_SETPOINT_REJECTED;

	regs[MB_REG_TEMPERATURE] = s16_word(st->has_temp ? mb_mc_to_cc(st->temp_mc) : MB_TEMP_NONE);
	regs[MB_REG_STATE] = (uint16_t)st->state;
	regs[MB_REG_COOL_SETPOINT] = s16_word(mb_mc_to_cc(st->cool_setpoint_mc));
	regs[MB_REG_HEAT_SETPOINT] = s16_word(mb_mc_to_cc(st->heat_setpoint_mc));
	regs[MB_REG_OUTPUTS] = outputs;
	regs[MB_REG_ALARMS] = alarms;
	regs[MB_REG_COMPRESSOR_STARTS] = saturate_u16(st->compressor_starts);
	regs[MB_REG_UPTIME_MIN] = saturate_u16(st->uptime_ms / MS_PER_MIN);
	regs[MB_REG_DIAGNOSTICS] = st->diagnostics;
	regs[MB_REG_PROTOCOL_VERSION] = MB_PROTOCOL_VERSION;
}

/* Register word -> signed value (two's complement), without implementation-defined casts. */
static int32_t word_to_s16(uint16_t w)
{
	return w >= 0x8000u ? (int32_t)w - 0x10000 : (int32_t)w;
}

void mb_regs_setpoints_mc(const uint16_t regs[MB_REG_COUNT], int32_t *cool_mc, int32_t *heat_mc)
{
	*cool_mc = word_to_s16(regs[MB_REG_COOL_SETPOINT]) * MC_PER_CC;
	*heat_mc = word_to_s16(regs[MB_REG_HEAT_SETPOINT]) * MC_PER_CC;
}
