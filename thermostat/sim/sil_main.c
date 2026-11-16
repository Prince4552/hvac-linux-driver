// SPDX-License-Identifier: MIT
/*
 * sil: software-in-the-loop simulator. Runs the real hvac_core (and the naive baseline) against
 * the room model for 24 simulated hours per scenario, one-second steps, and writes a CSV every
 * 10 s plus control-quality metrics. Usage: sil OUT_DIR [SEED]
 * Writes OUT_DIR/<scenario>_<controller>.csv and OUT_DIR/sil_metrics.json.
 * "sil fdd OUT_DIR" runs the fault-detection experiments instead (Ext 2, sil_fdd.c).
 * "sil trace OUT.csv" writes the per-second trace that the FreeRTOS port must match (Ext 4).
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "baseline_controller.h"
#include "hvac_core.h"
#include "room_model.h"
#include "sil_fdd.h"
#include "sil_metrics.h"
#include "sil_trace.h"

#define CSV_EVERY_S 10u
#define MS_PER_S 1000u
#define PATH_LEN 512u
#define JSON_LEN 1024u
#define DEFAULT_SEED 1u

typedef enum { CTRL_PRODUCT, CTRL_BASELINE } controller_kind_t;

static const char *const CONTROLLER_NAMES[] = { "product", "baseline" };

/* Either controller behind one small interface, so the run loop is the same for both. */
typedef struct {
	controller_kind_t kind;
	hvac_ctx_t core;
	baseline_t base;
	hvac_outputs_t out;
	const char *state_name;
} controller_t;

static int controller_init(controller_t *c, controller_kind_t kind)
{
	hvac_config_t cfg = {0};

	*c = (controller_t){0};
	c->kind = kind;
	c->state_name = "IDLE";
	hvac_config_set_defaults(&cfg);
	return hvac_core_init(&c->core, &cfg);
}

static void controller_step(controller_t *c, int32_t meas_mc, uint32_t t_s)
{
	hvac_input_t in = { .read_ok = true, .temp_mc = meas_mc, .now_ms = (uint64_t)t_s * MS_PER_S };

	if (c->kind == CTRL_PRODUCT) {
		c->state_name = hvac_state_name(hvac_core_step(&c->core, &in, &c->out));
		return;
	}
	baseline_step(&c->base, meas_mc);
	c->out.compressor = c->base.compressor;
	c->out.heater = c->base.heater;
	c->state_name = c->base.compressor ? "COOLING" : (c->base.heater ? "HEATING" : "IDLE");
}

static int write_csv_row(FILE *f, uint32_t t_s, const room_t *room, int32_t meas_mc,
			 const controller_t *c)
{
	int n = fprintf(f, "%u,%d,%d,%s,%d,%d\n", t_s, room_true_mc(room), meas_mc, c->state_name,
			c->out.compressor ? 1 : 0, c->out.heater ? 1 : 0);

	return n < 0 ? -EIO : 0;
}

/* Simulates one scenario with one controller for 24 h; fills metrics and writes the CSV. */
static int run_one(const room_scenario_t *sc, controller_kind_t kind, uint32_t seed,
		   const char *csv_path, sil_metrics_t *m)
{
	room_t room = {0};
	controller_t ctrl = {0};
	FILE *csv = NULL;
	int ret = 0;

	if (controller_init(&ctrl, kind) != 0)
		return -EINVAL;
	room_init(&room, sc, seed);
	sil_metrics_init(m, HVAC_DEFAULT_MIN_OFF_S);
	csv = fopen(csv_path, "w");
	if (csv == NULL)
		return -errno;
	if (fprintf(csv, "t_s,true_mc,meas_mc,state,compressor,heater\n") < 0)
		ret = -EIO;
	for (uint32_t t = 0; t < ROOM_SECONDS_PER_DAY && ret == 0; t++) {
		int32_t meas_mc = room_sensor_read_mc(&room);

		controller_step(&ctrl, meas_mc, t);
		sil_metrics_update(m, t, room_true_mc(&room), ctrl.out.compressor);
		if (t % CSV_EVERY_S == 0u)
			ret = write_csv_row(csv, t, &room, meas_mc, &ctrl);
		room_step(&room, t, ctrl.out.compressor, ctrl.out.heater);
	}
	if (fclose(csv) != 0 && ret == 0)
		ret = -EIO;
	return ret;
}

static int make_path(char *buf, size_t len, const char *dir, const char *name, const char *suffix)
{
	int n = snprintf(buf, len, "%s/%s%s", dir, name, suffix);

	return (n < 0 || (size_t)n >= len) ? -ENAMETOOLONG : 0;
}

/* Runs every scenario with both controllers and writes the JSON metrics file. */
static int run_all(const char *out_dir, uint32_t seed)
{
	char path[PATH_LEN] = {0};
	char json[JSON_LEN] = {0};
	char run_name[64] = {0};
	sil_metrics_t m = {0};
	FILE *out = NULL;
	int ret = make_path(path, sizeof(path), out_dir, "sil_metrics", ".json");

	if (ret != 0)
		return ret;
	out = fopen(path, "w");
	if (out == NULL)
		return -errno;
	if (fprintf(out, "{\"schema\": 1, \"seed\": %u, \"runs\": [\n", seed) < 0)
		ret = -EIO;
	for (unsigned i = 0; i < room_scenario_count() && ret == 0; i++) {
		const room_scenario_t *sc = room_scenario_at(i);

		for (unsigned k = 0; k < 2u && ret == 0; k++) {
			int n = snprintf(run_name, sizeof(run_name), "%s_%s", sc->name, CONTROLLER_NAMES[k]);

			if (n < 0 || (size_t)n >= sizeof(run_name))
				ret = -ENAMETOOLONG;
			if (ret == 0)
				ret = make_path(path, sizeof(path), out_dir, run_name, ".csv");
			if (ret == 0)
				ret = run_one(sc, (controller_kind_t)k, seed, path, &m);
			if (ret == 0)
				ret = sil_metrics_format_json(&m, sc->name, CONTROLLER_NAMES[k], seed,
							      json, sizeof(json));
			if (ret == 0 && fprintf(out, "%s %s\n", (i + k) == 0u ? " " : ",", json) < 0)
				ret = -EIO;
			if (ret == 0)
				printf("sil: %s\n", json);
		}
	}
	if (ret == 0 && fprintf(out, "]}\n") < 0)
		ret = -EIO;
	if (fclose(out) != 0 && ret == 0)
		ret = -EIO;
	return ret;
}

/* Ext 4: the reference trace for the FreeRTOS equivalence test (settings in sil_trace.h). */
static int run_trace(const char *path)
{
	const room_scenario_t *sc = room_find_scenario(SIL_TRACE_SCENARIO);
	hvac_config_t cfg = {0};
	hvac_ctx_t core = {0};
	hvac_outputs_t out = {0};
	room_t room = {0};
	FILE *f = NULL;
	int ret = 0;

	hvac_config_set_defaults(&cfg);
	if (sc == NULL || hvac_core_init(&core, &cfg) != 0)
		return -EINVAL;
	room_init(&room, sc, SIL_TRACE_SEED);
	f = fopen(path, "w");
	if (f == NULL)
		return -errno;
	ret = sil_trace_header(f);
	for (uint32_t t = 0; t < SIL_TRACE_SECONDS && ret == 0; t++) {
		int32_t meas_mc = room_sensor_read_mc(&room);
		hvac_input_t in = { .read_ok = true, .temp_mc = meas_mc,
				    .now_ms = (uint64_t)t * MS_PER_S };
		hvac_state_t state = hvac_core_step(&core, &in, &out);

		ret = sil_trace_row(f, t, meas_mc, state, &out);
		room_step(&room, t, out.compressor, out.heater);
	}
	if (fclose(f) != 0 && ret == 0)
		ret = -EIO;
	return ret;
}

static int parse_seed(const char *text, uint32_t *seed)
{
	char *end = NULL;
	unsigned long v = 0;

	errno = 0;
	v = strtoul(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || v == 0u || v > UINT32_MAX)
		return -EINVAL;
	*seed = (uint32_t)v;
	return 0;
}

int main(int argc, char **argv)
{
	uint32_t seed = DEFAULT_SEED;
	int ret = 0;

	if (argc == 3 && strcmp(argv[1], "fdd") == 0) {
		ret = sil_fdd_run_all(argv[2]);
		if (ret != 0)
			fprintf(stderr, "sil fdd: failed: %s\n", strerror(-ret));
		return ret == 0 ? 0 : 1;
	}
	if (argc == 3 && strcmp(argv[1], "trace") == 0) {
		ret = run_trace(argv[2]);
		if (ret != 0)
			fprintf(stderr, "sil trace: failed: %s\n", strerror(-ret));
		return ret == 0 ? 0 : 1;
	}
	if (argc < 2 || argc > 3 || (argc == 3 && parse_seed(argv[2], &seed) != 0)) {
		fprintf(stderr, "usage: %s OUT_DIR [SEED>0]\n", argv[0]);
		return 2;
	}
	ret = run_all(argv[1], seed);
	if (ret != 0) {
		fprintf(stderr, "sil: failed: %s\n", strerror(-ret));
		return 1;
	}
	return 0;
}
