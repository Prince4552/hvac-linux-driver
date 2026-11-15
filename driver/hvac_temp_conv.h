/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Pure conversion functions between the chip's 16-bit register, signed raw steps and
 * millidegrees Celsius (PROJECT_PLAN.md section 4.3). No I/O and no locking, so the driver and the
 * KUnit test module share exactly the same code. One raw step = 0.0625 C = 62.5 millidegrees.
 */
#ifndef HVAC_TEMP_CONV_H
#define HVAC_TEMP_CONV_H

#include <linux/bitops.h>
#include <linux/math.h>
#include <linux/types.h>

#define HVAC_TEMP_MIN_MC  (-40000)
#define HVAC_TEMP_MAX_MC  125000

/* 16-bit chip register -> signed raw steps (-2048..2047). */
static inline int hvac_temp_reg_to_raw(u16 reg)
{
	return sign_extend32(reg >> 4, 11);
}

/* Signed raw steps -> 16-bit chip register (bits 3..0 zero). */
static inline u16 hvac_temp_raw_to_reg(int raw)
{
	return (u16)((raw & 0xFFF) << 4);
}

/* Raw steps -> millidegrees, rounded to nearest (62.5 m°C per step). */
static inline int hvac_temp_raw_to_mc(int raw)
{
	return DIV_ROUND_CLOSEST(raw * 125, 2);
}

/* Millidegrees -> raw steps, rounded to nearest. Caller range-checks first. */
static inline int hvac_temp_mc_to_raw(int mc)
{
	return DIV_ROUND_CLOSEST(mc * 2, 125);
}

#endif /* HVAC_TEMP_CONV_H */
