// SPDX-License-Identifier: MIT
/*
 * hvacd: the thermostat daemon (PROJECT_PLAN.md section 4.7). Single-threaded: one select() loop
 * serves Modbus clients between control ticks. Each tick: apply Modbus setpoint writes, read the
 * sensor, run hvac_core_step(), apply (log) the outputs, refresh the Modbus register table.
 * Exit codes: 0 clean shutdown (SIGINT/SIGTERM), 1 configuration or start-up error, 2 fatal error.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>

#include "config.h"
#include "hvac_core.h"
#include "log.h"
#include "modbus_regs.h"
#include "modbus_server.h"
#include "sensor_iio.h"

#define HVACD_VERSION "1.0.0"
#define EXIT_OK 0
#define EXIT_STARTUP 1
#define EXIT_FATAL 2
#define ERR_LEN 256u
#define TEMP_TEXT_LEN 16u
#define MS_PER_S 1000u
#define NS_PER_MS 1000000L
#define US_PER_MS 1000u

static volatile sig_atomic_t stop_requested;

typedef struct {
	hvacd_config_t cfg;
	hvac_ctx_t core;
	sensor_iio_t sensor;
	mb_server_t mb;
	mb_status_t status;
	bool sensor_ok;		/* result of the previous read, to log changes only */
	uint64_t start_ms;
} app_t;

static app_t app;	/* static: no allocation, and zero-initialized */

static void on_signal(int sig)
{
	(void)sig;
	stop_requested = 1;	/* only set a flag; all cleanup happens in normal code */
}

static int install_signals(void)
{
	struct sigaction sa = {0};
	struct sigaction ignore = {0};

	sa.sa_handler = on_signal;
	if (sigemptyset(&sa.sa_mask) != 0 || sigaction(SIGINT, &sa, NULL) != 0 ||
	    sigaction(SIGTERM, &sa, NULL) != 0)
		return -errno;
	ignore.sa_handler = SIG_IGN;	/* a client hanging up must not kill the daemon */
	if (sigemptyset(&ignore.sa_mask) != 0 || sigaction(SIGPIPE, &ignore, NULL) != 0)
		return -errno;
	return 0;
}

static uint64_t mono_ms(void)
{
	struct timespec ts = {0};

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);	/* cannot fail for CLOCK_MONOTONIC */
	return (uint64_t)ts.tv_sec * MS_PER_S + (uint64_t)(ts.tv_nsec / NS_PER_MS);
}

/* "27.06C" from millidegrees (rounded to centidegrees). */
static void format_temp(char *buf, size_t len, int32_t mc)
{
	int16_t cc = mb_mc_to_cc(mc);
	int abs_cc = cc < 0 ? -cc : cc;

	(void)snprintf(buf, len, "%s%d.%02dC", cc < 0 ? "-" : "", abs_cc / 100, abs_cc % 100);
}

static void usage(FILE *out)
{
	(void)fprintf(out, "usage: hvacd --config PATH | --version | --help\n"
			   "Thermostat daemon: reads the hvac_temp IIO sensor, controls cooling and\n"
			   "heating, and serves a Modbus TCP register map.\n");
}

static int app_start(const char *config_path)
{
	char err[ERR_LEN] = {0};
	int ret = config_load_file(config_path, &app.cfg, err, sizeof(err));

	if (ret != 0) {
		(void)fprintf(stderr, "%s\n", err);
		return ret;
	}
	log_set_level(app.cfg.log_level);
	if (hvac_core_init(&app.core, &app.cfg.core) != 0 ||
	    sensor_iio_init(&app.sensor, app.cfg.iio_base_dir, app.cfg.iio_device_name) != 0) {
		log_msg(LOG_ERROR, "invalid configuration");
		return -EINVAL;
	}
	ret = mb_server_open(&app.mb, app.cfg.modbus_port, app.cfg.modbus_max_clients);
	if (ret != 0) {
		log_msg(LOG_ERROR, "cannot listen on Modbus TCP port %u: %s", app.cfg.modbus_port,
			strerror(-ret));
		return ret;
	}
	app.start_ms = mono_ms();
	app.sensor_ok = true;
	app.status.state = HVAC_STATE_IDLE;
	app.status.cool_setpoint_mc = app.cfg.core.cool_setpoint_mc;
	app.status.heat_setpoint_mc = app.cfg.core.heat_setpoint_mc;
	mb_regs_publish(mb_server_regs(&app.mb), &app.status);
	log_msg(LOG_INFO, "hvacd %s started: modbus port %u, loop %u ms, sensor '%s' in %s",
		HVACD_VERSION, app.cfg.modbus_port, app.cfg.loop_period_ms, app.cfg.iio_device_name,
		app.cfg.iio_base_dir);
	return 0;
}

/* Section 4.6: validate setpoints written over Modbus; apply them or restore the old values. */
static void apply_setpoint_writes(void)
{
	const uint16_t *regs = mb_server_regs(&app.mb);
	uint16_t published[MB_REG_COUNT] = {0};
	int32_t cool_mc = 0;
	int32_t heat_mc = 0;

	mb_regs_publish(published, &app.status);
	if (regs[MB_REG_COOL_SETPOINT] == published[MB_REG_COOL_SETPOINT] &&
	    regs[MB_REG_HEAT_SETPOINT] == published[MB_REG_HEAT_SETPOINT])
		return;
	mb_regs_setpoints_mc(regs, &cool_mc, &heat_mc);
	if (hvac_core_set_setpoints(&app.core, cool_mc, heat_mc) == 0) {
		app.status.cool_setpoint_mc = cool_mc;
		app.status.heat_setpoint_mc = heat_mc;
		app.status.setpoint_rejected = false;
		log_msg(LOG_INFO, "setpoints changed over Modbus: cool=%d heat=%d (m°C)", cool_mc,
			heat_mc);
	} else {
		app.status.setpoint_rejected = true;
		log_msg(LOG_WARN, "rejected Modbus setpoints cool=%d heat=%d (m°C)", cool_mc, heat_mc);
	}
	/* The publish at the end of the tick writes back the (possibly restored) values. */
}

static hvac_input_t read_sensor(uint64_t now_ms)
{
	hvac_input_t in = { .read_ok = false, .temp_mc = 0, .now_ms = now_ms };
	int ret = sensor_iio_read_mc(&app.sensor, &in.temp_mc);

	in.read_ok = (ret == 0);
	if (!in.read_ok && app.sensor_ok)
		log_msg(LOG_WARN, "sensor read failed: %s", strerror(-ret));
	else if (in.read_ok && !app.sensor_ok)
		log_msg(LOG_INFO, "sensor available again at %s", app.sensor.dev_dir);
	app.sensor_ok = in.read_ok;
	return in;
}

static void log_changes(hvac_state_t old_state, const hvac_outputs_t *old_out,
			const hvac_input_t *in, const hvac_outputs_t *out)
{
	if (app.status.state != old_state) {
		char temp[TEMP_TEXT_LEN] = "n/a";

		if (in->read_ok)
			format_temp(temp, sizeof(temp), in->temp_mc);
		log_msg(LOG_INFO, "state %s -> %s temp=%s", hvac_state_name(old_state),
			hvac_state_name(app.status.state), temp);
	}
	if (out->compressor != old_out->compressor || out->heater != old_out->heater ||
	    out->fan != old_out->fan)
		log_msg(LOG_INFO, "outputs compressor=%s heater=%s fan=%s",
			out->compressor ? "on" : "off", out->heater ? "on" : "off",
			out->fan ? "on" : "off");
}

static void control_tick(uint64_t now_ms)
{
	hvac_state_t old_state = app.status.state;
	hvac_outputs_t old_out = app.status.outputs;
	hvac_input_t in = {0};

	apply_setpoint_writes();
	in = read_sensor(now_ms);
	app.status.state = hvac_core_step(&app.core, &in, &app.status.outputs);
	log_changes(old_state, &old_out, &in, &app.status.outputs);

	app.status.has_temp = app.core.has_last_valid;
	app.status.temp_mc = app.core.last_valid_mc;
	app.status.compressor_starts = hvac_core_compressor_starts(&app.core);
	app.status.uptime_ms = now_ms - app.start_ms;
	mb_regs_publish(mb_server_regs(&app.mb), &app.status);
}

/* Waits up to wait_ms for Modbus traffic and serves it. Returns 0 or a negative errno value. */
static int serve_until(uint64_t wait_ms)
{
	fd_set readable;
	int max_fd = -1;
	int n = 0;
	struct timeval tv = {
		.tv_sec = (time_t)(wait_ms / MS_PER_S),
		.tv_usec = (suseconds_t)((wait_ms % MS_PER_S) * US_PER_MS),
	};

	FD_ZERO(&readable);
	mb_server_add_fds(&app.mb, &readable, &max_fd);
	n = select(max_fd + 1, &readable, NULL, NULL, &tv);
	if (n < 0)
		return errno == EINTR ? 0 : -errno;
	if (n > 0)
		mb_server_handle(&app.mb, &readable);
	return 0;
}

static int run_loop(void)
{
	uint64_t period = app.cfg.loop_period_ms;
	uint64_t next_tick = mono_ms();
	int ret = 0;

	while (!stop_requested) {
		uint64_t now = mono_ms();

		if (now >= next_tick) {
			control_tick(now);
			next_tick += period;
			if (next_tick <= now)
				next_tick = now + period;	/* fell behind: skip, never burst */
			continue;
		}
		ret = serve_until(next_tick - now);
		if (ret != 0) {
			log_msg(LOG_ERROR, "select failed: %s", strerror(-ret));
			return EXIT_FATAL;
		}
	}
	log_msg(LOG_INFO, "stopping on signal");
	return EXIT_OK;
}

int main(int argc, char **argv)
{
	int code = EXIT_OK;

	if (argc == 2 && strcmp(argv[1], "--version") == 0) {
		(void)printf("hvacd %s\n", HVACD_VERSION);
		return EXIT_OK;
	}
	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		usage(stdout);
		return EXIT_OK;
	}
	if (argc != 3 || strcmp(argv[1], "--config") != 0) {
		usage(stderr);
		return EXIT_STARTUP;
	}
	if (install_signals() != 0) {
		(void)fprintf(stderr, "hvacd: cannot install signal handlers\n");
		return EXIT_STARTUP;
	}
	/* On failure app_start() has nothing open: the Modbus server is its last step and
	 * mb_server_open() cleans up after itself. */
	if (app_start(argv[2]) != 0)
		return EXIT_STARTUP;
	code = run_loop();
	mb_server_close(&app.mb);
	return code;
}
