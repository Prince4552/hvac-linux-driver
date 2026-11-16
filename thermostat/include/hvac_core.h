/* SPDX-License-Identifier: MIT */
/*
 * hvac_core: the thermostat's decision logic as a pure state machine (PROJECT_PLAN.md section 4.4).
 * Input: one temperature reading and the current time. Output: compressor/heater/fan/alarm.
 * No memory allocation, no input/output, no clock: the same inputs always give the same outputs,
 * so it is unit-testable in milliseconds and runs unchanged in the daemon, simulator and FreeRTOS.
 */
#ifndef HVAC_CORE_H
#define HVAC_CORE_H

#include <stdbool.h>
#include <stdint.h>

/* Physical range of the sensor (same values as driver/hvac_temp_conv.h). */
#define HVAC_TEMP_MIN_MC (-40000)
#define HVAC_TEMP_MAX_MC 125000

/* Defaults (section 4.5). */
#define HVAC_DEFAULT_COOL_SETPOINT_MC 26000
#define HVAC_DEFAULT_HEAT_SETPOINT_MC 20000
#define HVAC_DEFAULT_DEADBAND_MC 1000
#define HVAC_DEFAULT_MIN_OFF_S 180u
#define HVAC_DEFAULT_MIN_RUN_S 120u
#define HVAC_DEFAULT_FAULT_RECOVER_COUNT 5u
#define HVAC_DEFAULT_MAX_STEP_MC 5000

/* Validation ranges (section 4.4), shared with the configuration parser. */
#define HVAC_SETPOINT_MIN_MC 5000
#define HVAC_SETPOINT_MAX_MC 40000
#define HVAC_DEADBAND_MIN_MC 100
#define HVAC_DEADBAND_MAX_MC 5000
#define HVAC_TIMER_MAX_S 3600u
#define HVAC_FAULT_RECOVER_MIN 1u
#define HVAC_FAULT_RECOVER_MAX 100u
#define HVAC_MAX_STEP_MIN_MC 500
#define HVAC_MAX_STEP_MAX_MC 20000

typedef enum {
	HVAC_STATE_IDLE = 0,
	HVAC_STATE_COOLING = 1,
	HVAC_STATE_HEATING = 2,
	HVAC_STATE_FAULT = 3
} hvac_state_t;

typedef struct {
	int32_t cool_setpoint_mc;	/* default 26000 */
	int32_t heat_setpoint_mc;	/* default 20000 */
	int32_t deadband_mc;		/* default 1000: half-width of the switching band */
	uint32_t min_off_s;		/* default 180: compressor minimum off time */
	uint32_t min_run_s;		/* default 120: compressor minimum run time */
	uint32_t fault_recover_count;	/* default 5: good readings needed to leave FAULT */
	int32_t max_step_mc;		/* default 5000: largest believable change between readings */
} hvac_config_t;

typedef struct {
	bool read_ok;		/* false if the sensor could not be read */
	int32_t temp_mc;	/* reading in millidegrees Celsius (ignored if !read_ok) */
	uint64_t now_ms;	/* monotonic time in milliseconds */
} hvac_input_t;

typedef struct {
	bool compressor;
	bool heater;
	bool fan;
	bool alarm;
} hvac_outputs_t;

/* The whole controller state. A plain struct so callers can allocate it statically. */
typedef struct {
	hvac_config_t cfg;
	hvac_state_t state;
	int32_t last_valid_mc;		/* last reading that passed the validity checks */
	bool has_last_valid;
	bool last_reading_valid;	/* did the most recent step's reading pass? (used by FDD) */
	uint32_t good_count;		/* consecutive valid readings while in FAULT */
	uint64_t compressor_on_since_ms;
	uint64_t compressor_off_since_ms;
	bool compressor_has_run;	/* false until the first compressor start */
	uint32_t compressor_starts;
} hvac_ctx_t;

/** Fills cfg with the default values from section 4.5. */
void hvac_config_set_defaults(hvac_config_t *cfg);

/** Checks every field range and the band ordering. Returns 0 or -EINVAL. */
int hvac_config_validate(const hvac_config_t *cfg);

/** Validates cfg and starts the controller in IDLE. Returns 0 or -EINVAL (ctx unchanged). */
int hvac_core_init(hvac_ctx_t *ctx, const hvac_config_t *cfg);

/** Runs one control step (at most one state transition) and fills out. Returns the new state. */
hvac_state_t hvac_core_step(hvac_ctx_t *ctx, const hvac_input_t *in, hvac_outputs_t *out);

/** Changes both setpoints if the resulting config is valid. Returns 0 or -EINVAL (no change). */
int hvac_core_set_setpoints(hvac_ctx_t *ctx, int32_t cool_mc, int32_t heat_mc);

/** Number of compressor starts since hvac_core_init(). */
uint32_t hvac_core_compressor_starts(const hvac_ctx_t *ctx);

/** "IDLE", "COOLING", "HEATING", "FAULT", or "UNKNOWN" for an out-of-range value. */
const char *hvac_state_name(hvac_state_t state);

#endif /* HVAC_CORE_H */
