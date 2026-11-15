/* SPDX-License-Identifier: MIT */
/*
 * room_model: a simple one-room thermal model and a simulated temperature sensor for the
 * software-in-the-loop simulator (PROJECT_PLAN.md section 7, Phase 5, task 5).
 * Integer arithmetic only. Temperatures are in micro-degrees Celsius (suffix _uc), time in seconds.
 */
#ifndef ROOM_MODEL_H
#define ROOM_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#define ROOM_SECONDS_PER_DAY 86400u

typedef struct {
	const char *name;
	int64_t outside_uc;	/* constant outside temperature */
	int64_t start_uc;	/* room temperature at 00:00 */
	bool solar;		/* sun heats the room on this day */
} room_scenario_t;

typedef struct {
	room_scenario_t scenario;
	int64_t temp_uc;		/* true room temperature */
	uint32_t rng_state;		/* xorshift32 state for sensor noise */
	uint32_t cooling_strength_pct;	/* 100 = healthy; lowered by fault injection (Ext 2) */
} room_t;

/** Looks up "hot_day", "cold_day" or "mild_day". Returns NULL for an unknown name. */
const room_scenario_t *room_find_scenario(const char *name);

/** Number of built-in scenarios, and scenario i (0-based) for iteration. */
unsigned room_scenario_count(void);
const room_scenario_t *room_scenario_at(unsigned i);

/** Starts the room at the scenario's start temperature with a noise seed (0 is replaced by 1). */
void room_init(room_t *room, const room_scenario_t *scenario, uint32_t seed);

/** Advances the room by one second at time t_s with the given equipment on or off. */
void room_step(room_t *room, uint32_t t_s, bool compressor, bool heater);

/** Heat gain from the sun at time t_s (half-sine 06:00..18:00), micro-degrees per second. */
int64_t room_solar_gain_uc(const room_t *room, uint32_t t_s);

/** One sensor reading: true temperature + noise, quantized like the chip; millidegrees. */
int32_t room_sensor_read_mc(room_t *room);

/** True room temperature rounded down to millidegrees. */
int32_t room_true_mc(const room_t *room);

#endif /* ROOM_MODEL_H */
