// SPDX-License-Identifier: MIT
/*
 * sil_fdd: detection matrix (5 fault cases, fault injected at 06:00) and false-alarm matrix
 * (3 healthy scenarios x 5 seeds) for the Ext 2 detectors. A case counts as detected when its
 * expected bit is set after the injection; other bits are reported but not counted against it.
 */
#include "sil_fdd.h"

#include <errno.h>
#include <stdio.h>

#include "hvac_core.h"
#include "hvac_fdd.h"
#include "room_model.h"

#define MS_PER_S 1000u
#define INJECT_AT_S (6u * 3600u)
#define FALSE_ALARM_SEEDS 5u
#define DETECT_SEED 1u

typedef struct {
	const char *scenario;
	room_fault_t fault;
	uint16_t expected_bit;
} fdd_case_t;

static const fdd_case_t CASES[] = {
	{ "hot_day", ROOM_FAULT_CAPACITY_LOSS, FDD_COOLING_INEFFECTIVE },
	{ "hot_day", ROOM_FAULT_COMPRESSOR_FAIL, FDD_COOLING_INEFFECTIVE },
	{ "hot_day", ROOM_FAULT_SENSOR_STUCK, FDD_SENSOR_STUCK },
	{ "cold_day", ROOM_FAULT_SENSOR_STUCK, FDD_SENSOR_STUCK },
	{ "mild_day", ROOM_FAULT_SENSOR_STUCK, FDD_SENSOR_STUCK },
};
#define CASE_COUNT (sizeof(CASES) / sizeof(CASES[0]))

typedef struct {
	uint16_t final_bits;
	bool detected;
	uint32_t detect_after_s;	/* seconds from injection to the expected bit */
} fdd_result_t;

/* One simulated day with the product controller and FDD; fault NONE means a healthy day. */
static int simulate(const room_scenario_t *sc, room_fault_t fault, uint16_t expected_bit,
		    uint32_t seed, fdd_result_t *res)
{
	hvac_config_t core_cfg = {0};
	hvac_fdd_config_t fdd_cfg = {0};
	hvac_ctx_t core = {0};
	hvac_fdd_t fdd = {0};
	room_t room = {0};

	hvac_config_set_defaults(&core_cfg);
	hvac_fdd_config_set_defaults(&fdd_cfg);
	if (hvac_core_init(&core, &core_cfg) != 0 || hvac_fdd_init(&fdd, &fdd_cfg) != 0)
		return -EINVAL;
	room_init(&room, sc, seed);
	*res = (fdd_result_t){0};
	for (uint32_t t = 0; t < ROOM_SECONDS_PER_DAY; t++) {
		hvac_input_t in = { .read_ok = true, .now_ms = (uint64_t)t * MS_PER_S };
		hvac_outputs_t out = {0};
		hvac_fdd_input_t fin = {0};

		if (fault != ROOM_FAULT_NONE && t == INJECT_AT_S)
			room_inject_fault(&room, fault);
		in.temp_mc = room_sensor_read_mc(&room);
		(void)hvac_core_step(&core, &in, &out);
		fin = (hvac_fdd_input_t){ .compressor = out.compressor,
					  .valid = core.last_reading_valid,
					  .temp_mc = in.temp_mc, .now_ms = in.now_ms };
		res->final_bits = hvac_fdd_step(&fdd, &fin);
		if (!res->detected && expected_bit != 0u && (res->final_bits & expected_bit) &&
		    t >= INJECT_AT_S) {
			res->detected = true;
			res->detect_after_s = t - INJECT_AT_S;
		}
		room_step(&room, t, out.compressor, out.heater);
	}
	return 0;
}

static int write_detection(FILE *f, uint32_t *detected, uint64_t *detect_sum_s)
{
	for (size_t i = 0; i < CASE_COUNT; i++) {
		fdd_result_t r = {0};
		const room_scenario_t *sc = room_find_scenario(CASES[i].scenario);

		if (sc == NULL || simulate(sc, CASES[i].fault, CASES[i].expected_bit, DETECT_SEED, &r))
			return -EINVAL;
		if (r.detected) {
			(*detected)++;
			*detect_sum_s += r.detect_after_s;
		}
		if (fprintf(f, "%s{\"case\": \"%s@%s\", \"expected_bit\": %u, \"detected\": %s, "
			    "\"time_to_detect_min\": %u.%u, \"bits\": %u}\n", i == 0u ? "  " : " ,",
			    room_fault_name(CASES[i].fault), CASES[i].scenario, CASES[i].expected_bit,
			    r.detected ? "true" : "false", r.detect_after_s / 60u,
			    (r.detect_after_s % 60u) / 6u, r.final_bits) < 0)
			return -EIO;
	}
	return 0;
}

static int write_false_alarms(FILE *f, uint32_t *false_alarms)
{
	for (unsigned i = 0; i < room_scenario_count(); i++) {
		for (uint32_t seed = 1; seed <= FALSE_ALARM_SEEDS; seed++) {
			fdd_result_t r = {0};
			const room_scenario_t *sc = room_scenario_at(i);

			if (simulate(sc, ROOM_FAULT_NONE, 0u, seed, &r) != 0)
				return -EINVAL;
			if (r.final_bits != 0u)
				(*false_alarms)++;
			if (fprintf(f, "%s{\"scenario\": \"%s\", \"seed\": %u, \"bits\": %u}\n",
				    (i == 0u && seed == 1u) ? "  " : " ,", sc->name, seed,
				    r.final_bits) < 0)
				return -EIO;
		}
	}
	return 0;
}

int sil_fdd_run_all(const char *out_dir)
{
	char path[512] = {0};
	uint32_t detected = 0;
	uint32_t false_alarms = 0;
	uint64_t detect_sum_s = 0;
	uint64_t mean_tenths_min = 0;
	FILE *f = NULL;
	int ret = 0;
	int n = snprintf(path, sizeof(path), "%s/fdd_metrics.json", out_dir);

	if (n < 0 || (size_t)n >= sizeof(path))
		return -ENAMETOOLONG;
	f = fopen(path, "w");
	if (f == NULL)
		return -errno;
	if (fprintf(f, "{\"schema\": 1, \"inject_at_h\": 6, \"detection\": [\n") < 0)
		ret = -EIO;
	if (ret == 0)
		ret = write_detection(f, &detected, &detect_sum_s);
	if (ret == 0 && fprintf(f, "], \"false_alarm_runs\": [\n") < 0)
		ret = -EIO;
	if (ret == 0)
		ret = write_false_alarms(f, &false_alarms);
	/* Mean time to detect in tenths of a minute: sum_s / detected / 6. */
	if (detected > 0u)
		mean_tenths_min = detect_sum_s / detected / 6u;
	if (ret == 0 && fprintf(f, "], \"fdd_detection_rate\": %u.%02u, \"fdd_false_alarms\": %u, "
				"\"fdd_mean_time_to_detect_min\": %llu.%llu}\n",
				detected * 100u / (unsigned)CASE_COUNT,
				(detected * 10000u / (unsigned)CASE_COUNT) % 100u, false_alarms,
				(unsigned long long)(mean_tenths_min / 10u),
				(unsigned long long)(mean_tenths_min % 10u)) < 0)
		ret = -EIO;
	if (fclose(f) != 0 && ret == 0)
		ret = -EIO;
	printf("sil fdd: detected %u/%u, false alarms %u\n", detected, (unsigned)CASE_COUNT,
	       false_alarms);
	return ret;
}
