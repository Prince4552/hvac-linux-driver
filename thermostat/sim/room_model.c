// SPDX-License-Identifier: MIT
/*
 * room_model: one-room thermal model and simulated sensor (integer micro-degrees).
 * dT per second = (T_out - T) x leak_ppm / 1e6 + equipment + sun + people.
 * The sensor adds seeded noise and quantizes to 0.0625 C steps, like the TMP102-style chip.
 */
#include "room_model.h"

#include <stddef.h>
#include <string.h>

#define UC_PER_C 1000000LL
#define ROOM_LEAK_PPM 278LL		/* heat leak per second: about a 1-hour time constant */
#define PPM_DIVISOR 1000000LL
#define COOLING_UC_PER_S (-6000LL)	/* compressor on */
#define HEATING_UC_PER_S 6000LL		/* heater on */
#define SOLAR_PEAK_UC_PER_S 1500LL	/* at noon on sunny days */
#define OCCUPANCY_UC_PER_S 300LL	/* people and equipment, 09:00..18:00 */
#define SOLAR_START_S (6u * 3600u)
#define SOLAR_LENGTH_S (12u * 3600u)
#define OCCUPANCY_START_S (9u * 3600u)
#define OCCUPANCY_END_S (18u * 3600u)
#define SENSOR_NOISE_UC 100000LL	/* uniform noise within +-0.1 C */
#define SENSOR_STEP_UC 62500LL		/* one chip step: 0.0625 C */
#define PERCENT 100LL
#define CAPACITY_LOSS_STRENGTH_PCT 40u

static const room_scenario_t SCENARIOS[] = {
	{ "hot_day", 35 * UC_PER_C, 30 * UC_PER_C, true },
	{ "cold_day", 8 * UC_PER_C, 14 * UC_PER_C, false },
	{ "mild_day", 24 * UC_PER_C, 24 * UC_PER_C, true },
};
#define SCENARIO_COUNT (sizeof(SCENARIOS) / sizeof(SCENARIOS[0]))

const room_scenario_t *room_find_scenario(const char *name)
{
	for (size_t i = 0; i < SCENARIO_COUNT; i++) {
		if (name != NULL && strcmp(SCENARIOS[i].name, name) == 0)
			return &SCENARIOS[i];
	}
	return NULL;
}

unsigned room_scenario_count(void)
{
	return (unsigned)SCENARIO_COUNT;
}

const room_scenario_t *room_scenario_at(unsigned i)
{
	return i < SCENARIO_COUNT ? &SCENARIOS[i] : NULL;
}

void room_init(room_t *room, const room_scenario_t *scenario, uint32_t seed)
{
	*room = (room_t){0};
	room->scenario = *scenario;
	room->temp_uc = scenario->start_uc;
	room->rng_state = seed != 0u ? seed : 1u;	/* xorshift must not start at 0 */
	room->cooling_strength_pct = 100u;
}

/*
 * Half-sine without floating point: Bhaskara I's approximation
 * sin(pi x) ~= 16 x (1 - x) / (5 - 4 x (1 - x)), with x = s / L (max error about 0.2 %).
 */
int64_t room_solar_gain_uc(const room_t *room, uint32_t t_s)
{
	int64_t s = 0;
	int64_t len = SOLAR_LENGTH_S;
	int64_t u = 0;
	uint32_t tod = t_s % ROOM_SECONDS_PER_DAY;

	if (!room->scenario.solar || tod < SOLAR_START_S || tod >= SOLAR_START_S + SOLAR_LENGTH_S)
		return 0;
	s = (int64_t)(tod - SOLAR_START_S);
	u = s * (len - s);
	return SOLAR_PEAK_UC_PER_S * 16 * u / (5 * len * len - 4 * u);
}

void room_step(room_t *room, uint32_t t_s, bool compressor, bool heater)
{
	uint32_t tod = t_s % ROOM_SECONDS_PER_DAY;
	int64_t d_uc = (room->scenario.outside_uc - room->temp_uc) * ROOM_LEAK_PPM / PPM_DIVISOR;

	if (compressor)
		d_uc += COOLING_UC_PER_S * (int64_t)room->cooling_strength_pct / PERCENT;
	if (heater)
		d_uc += HEATING_UC_PER_S;
	d_uc += room_solar_gain_uc(room, t_s);
	if (tod >= OCCUPANCY_START_S && tod < OCCUPANCY_END_S)
		d_uc += OCCUPANCY_UC_PER_S;
	room->temp_uc += d_uc;
}

static uint32_t xorshift32(uint32_t *state)
{
	uint32_t x = *state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

/* Floor division for a positive divisor (C division rounds toward zero). */
static int64_t floor_div(int64_t a, int64_t b)
{
	int64_t q = a / b;

	return (a % b != 0 && a < 0) ? q - 1 : q;
}

int32_t room_sensor_read_mc(room_t *room)
{
	uint32_t r = xorshift32(&room->rng_state);	/* drawn even when stuck: same sequence */
	int64_t noise_uc = (int64_t)(r % (uint32_t)(2 * SENSOR_NOISE_UC + 1)) - SENSOR_NOISE_UC;
	int64_t raw = floor_div(room->temp_uc + noise_uc, SENSOR_STEP_UC);
	int64_t x = raw * 125;	/* raw -> millidegrees: x / 2, rounded half away from zero */

	if (room->sensor_stuck && room->has_last_reading)
		return room->last_reading_mc;
	room->last_reading_mc = (int32_t)(x >= 0 ? (x + 1) / 2 : (x - 1) / 2);
	room->has_last_reading = true;
	return room->last_reading_mc;
}

void room_inject_fault(room_t *room, room_fault_t fault)
{
	switch (fault) {
	case ROOM_FAULT_CAPACITY_LOSS:
		room->cooling_strength_pct = CAPACITY_LOSS_STRENGTH_PCT;
		break;
	case ROOM_FAULT_COMPRESSOR_FAIL:
		room->cooling_strength_pct = 0u;
		break;
	case ROOM_FAULT_SENSOR_STUCK:
		room->sensor_stuck = true;
		break;
	default:
		break;
	}
}

const char *room_fault_name(room_fault_t fault)
{
	switch (fault) {
	case ROOM_FAULT_CAPACITY_LOSS:
		return "capacity_loss";
	case ROOM_FAULT_COMPRESSOR_FAIL:
		return "compressor_fail";
	case ROOM_FAULT_SENSOR_STUCK:
		return "sensor_stuck";
	default:
		return "none";
	}
}

int32_t room_true_mc(const room_t *room)
{
	return (int32_t)floor_div(room->temp_uc, 1000);
}
