/* SPDX-License-Identifier: MIT */
/*
 * hvac_fdd: fault detection and diagnostics (Ext 2). Two simple, explainable rules that watch the
 * thermostat's own data and flag equipment problems before a person would notice:
 *   bit0 FDD_COOLING_INEFFECTIVE: the compressor ran a whole window but the room barely cooled;
 *   bit1 FDD_SENSOR_STUCK: the valid reading has not changed at all for a long time.
 * Pure like hvac_core: no allocation, no I/O, no clock. Bits latch until restart.
 */
#ifndef HVAC_FDD_H
#define HVAC_FDD_H

#include <stdbool.h>
#include <stdint.h>

#define FDD_COOLING_INEFFECTIVE 0x0001u
#define FDD_SENSOR_STUCK 0x0002u

#define FDD_DEFAULT_INEFFECTIVE_WINDOW_S 1200u
#define FDD_DEFAULT_INEFFECTIVE_MIN_DROP_MC 500
#define FDD_DEFAULT_STUCK_WINDOW_S 1800u
#define FDD_WINDOW_MIN_S 60u
#define FDD_WINDOW_MAX_S 86400u
#define FDD_MIN_DROP_MIN_MC 100
#define FDD_MIN_DROP_MAX_MC 10000

typedef struct {
	bool enabled;
	uint32_t ineffective_window_s;
	int32_t ineffective_min_drop_mc;
	uint32_t stuck_window_s;
} hvac_fdd_config_t;

typedef struct {
	bool compressor;	/* compressor commanded on during this step */
	bool valid;		/* the reading passed hvac_core's validity checks */
	int32_t temp_mc;	/* the reading (ignored if !valid) */
	uint64_t now_ms;
} hvac_fdd_input_t;

typedef struct {
	hvac_fdd_config_t cfg;
	uint16_t bits;			/* latched diagnostic bits */
	bool window_active;		/* compressor on and a cooling window started */
	uint64_t window_start_ms;
	int32_t window_ref_mc;
	bool has_stuck_ref;
	int32_t stuck_ref_mc;
	uint64_t stuck_since_ms;
} hvac_fdd_t;

/** Fills the defaults (enabled, 1200 s, 500 m°C, 1800 s). */
void hvac_fdd_config_set_defaults(hvac_fdd_config_t *cfg);

/** Checks ranges. Returns 0 or -EINVAL. */
int hvac_fdd_config_validate(const hvac_fdd_config_t *cfg);

/** Starts with no bits set. Returns 0 or -EINVAL. */
int hvac_fdd_init(hvac_fdd_t *fdd, const hvac_fdd_config_t *cfg);

/** Feeds one control step; returns the latched bits (0 when disabled). */
uint16_t hvac_fdd_step(hvac_fdd_t *fdd, const hvac_fdd_input_t *in);

#endif /* HVAC_FDD_H */
