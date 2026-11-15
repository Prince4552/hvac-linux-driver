/* SPDX-License-Identifier: MIT */
/*
 * modbus_regs: the Modbus holding-register map of hvacd (PROJECT_PLAN.md section 4.6) as pure
 * functions: daemon status -> register values, and register values -> requested setpoints.
 * Kept apart from the network code (modbus_server.c) so the mapping is unit-testable.
 */
#ifndef MODBUS_REGS_H
#define MODBUS_REGS_H

#include <stdbool.h>
#include <stdint.h>

#include "hvac_core.h"

enum {
	MB_REG_TEMPERATURE = 0,		/* centidegrees, int16 */
	MB_REG_STATE = 1,		/* 0 IDLE, 1 COOLING, 2 HEATING, 3 FAULT */
	MB_REG_COOL_SETPOINT = 2,	/* centidegrees, read/write */
	MB_REG_HEAT_SETPOINT = 3,	/* centidegrees, read/write */
	MB_REG_OUTPUTS = 4,		/* bit0 compressor, bit1 heater, bit2 fan */
	MB_REG_ALARMS = 5,		/* bit0 sensor fault, bit1 rejected setpoint write */
	MB_REG_COMPRESSOR_STARTS = 6,	/* saturates at 65535 */
	MB_REG_UPTIME_MIN = 7,		/* minutes, saturates at 65535 */
	MB_REG_DIAGNOSTICS = 8,		/* fault-detection bits (Ext 2) */
	MB_REG_PROTOCOL_VERSION = 9,
	MB_REG_COUNT = 10
};

#define MB_PROTOCOL_VERSION 1u
#define MB_TEMP_NONE INT16_MIN		/* no valid reading yet */
#define MB_OUT_COMPRESSOR 0x1u
#define MB_OUT_HEATER 0x2u
#define MB_OUT_FAN 0x4u
#define MB_ALARM_SENSOR_FAULT 0x1u
#define MB_ALARM_SETPOINT_REJECTED 0x2u

/* Everything the register table shows. */
typedef struct {
	bool has_temp;			/* false until the first valid reading */
	int32_t temp_mc;		/* last valid reading */
	hvac_state_t state;
	int32_t cool_setpoint_mc;
	int32_t heat_setpoint_mc;
	hvac_outputs_t outputs;
	bool setpoint_rejected;
	uint32_t compressor_starts;
	uint64_t uptime_ms;
	uint16_t diagnostics;
} mb_status_t;

/** Millidegrees -> centidegrees, rounded half away from zero, clamped to int16. */
int16_t mb_mc_to_cc(int32_t mc);

/** Fills all MB_REG_COUNT registers from the status. */
void mb_regs_publish(uint16_t regs[MB_REG_COUNT], const mb_status_t *st);

/** Reads registers 2 and 3 (int16 centidegrees) as millidegree setpoints. */
void mb_regs_setpoints_mc(const uint16_t regs[MB_REG_COUNT], int32_t *cool_mc, int32_t *heat_mc);

#endif /* MODBUS_REGS_H */
