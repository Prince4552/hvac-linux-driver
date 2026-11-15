// SPDX-License-Identifier: MIT
/*
 * config: parser for hvacd's "key = value" configuration (PROJECT_PLAN.md section 4.5).
 * Each known key is one row in the KEYS table (name, kind, range). Unknown keys, duplicates,
 * malformed numbers and out-of-range values are errors reported as "config:<line>: <message>".
 */
#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "watchdog.h"

#define CONFIG_LINE_MAX 256u

typedef enum { KIND_INT, KIND_TEXT, KIND_LOG_LEVEL } value_kind_t;

typedef enum {
	KEY_COOL_SETPOINT, KEY_HEAT_SETPOINT, KEY_DEADBAND, KEY_MIN_OFF, KEY_MIN_RUN,
	KEY_FAULT_RECOVER, KEY_MAX_STEP, KEY_LOOP_PERIOD, KEY_IIO_BASE_DIR, KEY_IIO_DEVICE_NAME,
	KEY_MODBUS_PORT, KEY_MODBUS_MAX_CLIENTS, KEY_LOG_LEVEL,
	KEY_WATCHDOG_ENABLED, KEY_WATCHDOG_DEVICE, KEY_WATCHDOG_TIMEOUT,
	KEY_COUNT
} key_id_t;

typedef struct {
	const char *name;
	value_kind_t kind;
	int64_t min;	/* integer keys only */
	int64_t max;
} key_spec_t;

static const key_spec_t KEYS[KEY_COUNT] = {
	[KEY_COOL_SETPOINT] = { "cool_setpoint_mc", KIND_INT, HVAC_SETPOINT_MIN_MC, HVAC_SETPOINT_MAX_MC },
	[KEY_HEAT_SETPOINT] = { "heat_setpoint_mc", KIND_INT, HVAC_SETPOINT_MIN_MC, HVAC_SETPOINT_MAX_MC },
	[KEY_DEADBAND] = { "deadband_mc", KIND_INT, HVAC_DEADBAND_MIN_MC, HVAC_DEADBAND_MAX_MC },
	[KEY_MIN_OFF] = { "min_off_s", KIND_INT, 0, HVAC_TIMER_MAX_S },
	[KEY_MIN_RUN] = { "min_run_s", KIND_INT, 0, HVAC_TIMER_MAX_S },
	[KEY_FAULT_RECOVER] = { "fault_recover_count", KIND_INT, HVAC_FAULT_RECOVER_MIN,
				HVAC_FAULT_RECOVER_MAX },
	[KEY_MAX_STEP] = { "max_step_mc", KIND_INT, HVAC_MAX_STEP_MIN_MC, HVAC_MAX_STEP_MAX_MC },
	[KEY_LOOP_PERIOD] = { "loop_period_ms", KIND_INT, 100, 10000 },
	[KEY_IIO_BASE_DIR] = { "iio_base_dir", KIND_TEXT, 0, 0 },
	[KEY_IIO_DEVICE_NAME] = { "iio_device_name", KIND_TEXT, 0, 0 },
	[KEY_MODBUS_PORT] = { "modbus_port", KIND_INT, 1, 65535 },
	[KEY_MODBUS_MAX_CLIENTS] = { "modbus_max_clients", KIND_INT, 1, 8 },
	[KEY_LOG_LEVEL] = { "log_level", KIND_LOG_LEVEL, 0, 0 },
	[KEY_WATCHDOG_ENABLED] = { "watchdog_enabled", KIND_INT, 0, 1 },
	[KEY_WATCHDOG_DEVICE] = { "watchdog_device", KIND_TEXT, 0, 0 },
	[KEY_WATCHDOG_TIMEOUT] = { "watchdog_timeout_s", KIND_INT, WDT_TIMEOUT_MIN_S,
				   WDT_TIMEOUT_MAX_S },
};

static const char *const LOG_LEVEL_NAMES[] = { "error", "warn", "info", "debug" };
#define LOG_LEVEL_COUNT (sizeof(LOG_LEVEL_NAMES) / sizeof(LOG_LEVEL_NAMES[0]))

/* Parser bookkeeping for one call of config_parse_string(). */
typedef struct {
	bool seen[KEY_COUNT];
	unsigned band_line;	/* last line that set a setpoint or the deadband */
	unsigned line;
	char *err;
	size_t err_len;
} parse_state_t;

__attribute__((format(printf, 2, 3)))
static int fail(parse_state_t *ps, const char *fmt, ...)
{
	char msg[CONFIG_LINE_MAX] = {0};
	va_list ap;
	int n = 0;

	va_start(ap, fmt);
	n = vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	if (n < 0)
		msg[0] = '\0';	/* encoding error: report the line number alone */
	/* A truncated message is still useful; truncation is acceptable here. */
	if (ps->err != NULL && ps->err_len > 0)
		(void)snprintf(ps->err, ps->err_len, "config:%u: %s", ps->line, msg);
	return -EINVAL;
}

void config_set_defaults(hvacd_config_t *cfg)
{
	if (cfg == NULL)
		return;
	*cfg = (hvacd_config_t){0};
	hvac_config_set_defaults(&cfg->core);
	cfg->loop_period_ms = 1000u;
	(void)snprintf(cfg->iio_base_dir, sizeof(cfg->iio_base_dir), "%s", "/sys/bus/iio/devices");
	(void)snprintf(cfg->iio_device_name, sizeof(cfg->iio_device_name), "%s", "hvac_temp");
	cfg->modbus_port = 502u;
	cfg->modbus_max_clients = 4u;
	cfg->log_level = CONFIG_LOG_INFO;
	cfg->watchdog_enabled = 0u;
	(void)snprintf(cfg->watchdog_device, sizeof(cfg->watchdog_device), "%s", "/dev/watchdog");
	cfg->watchdog_timeout_s = 10u;
}

const char *config_log_level_name(config_log_level_t level)
{
	return (size_t)level < LOG_LEVEL_COUNT ? LOG_LEVEL_NAMES[level] : "unknown";
}

/* Removes leading and trailing white space in place; returns the new start. */
static char *trim(char *s)
{
	char *end = NULL;

	while (isspace((unsigned char)*s))
		s++;
	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		end--;
	*end = '\0';
	return s;
}

static int find_key(const char *name)
{
	for (int i = 0; i < KEY_COUNT; i++) {
		if (strcmp(KEYS[i].name, name) == 0)
			return i;
	}
	return -1;
}

/* Stores an already range-checked integer; the casts cannot lose information. */
static void store_int(hvacd_config_t *cfg, key_id_t id, int64_t v)
{
	switch (id) {
	case KEY_COOL_SETPOINT: cfg->core.cool_setpoint_mc = (int32_t)v; break;
	case KEY_HEAT_SETPOINT: cfg->core.heat_setpoint_mc = (int32_t)v; break;
	case KEY_DEADBAND: cfg->core.deadband_mc = (int32_t)v; break;
	case KEY_MIN_OFF: cfg->core.min_off_s = (uint32_t)v; break;
	case KEY_MIN_RUN: cfg->core.min_run_s = (uint32_t)v; break;
	case KEY_FAULT_RECOVER: cfg->core.fault_recover_count = (uint32_t)v; break;
	case KEY_MAX_STEP: cfg->core.max_step_mc = (int32_t)v; break;
	case KEY_LOOP_PERIOD: cfg->loop_period_ms = (uint32_t)v; break;
	case KEY_MODBUS_PORT: cfg->modbus_port = (uint16_t)v; break;
	case KEY_MODBUS_MAX_CLIENTS: cfg->modbus_max_clients = (uint32_t)v; break;
	case KEY_WATCHDOG_ENABLED: cfg->watchdog_enabled = (uint32_t)v; break;
	case KEY_WATCHDOG_TIMEOUT: cfg->watchdog_timeout_s = (uint32_t)v; break;
	default: break;	/* not an integer key */
	}
}

static int apply_int(parse_state_t *ps, hvacd_config_t *cfg, key_id_t id, const char *value)
{
	const key_spec_t *k = &KEYS[id];
	char *end = NULL;
	long long v = 0;

	errno = 0;
	v = strtoll(value, &end, 10);
	if (errno != 0 || end == value || *end != '\0')
		return fail(ps, "value '%s' for '%s' is not an integer", value, k->name);
	if (v < k->min || v > k->max)
		return fail(ps, "value %lld for '%s' is out of range %lld..%lld", v, k->name,
			    (long long)k->min, (long long)k->max);
	store_int(cfg, id, (int64_t)v);
	return 0;
}

static int apply_text(parse_state_t *ps, hvacd_config_t *cfg, key_id_t id, const char *value)
{
	char *dest = cfg->iio_device_name;
	size_t size = sizeof(cfg->iio_device_name);
	int n = 0;

	if (id == KEY_IIO_BASE_DIR) {
		dest = cfg->iio_base_dir;
		size = sizeof(cfg->iio_base_dir);
	} else if (id == KEY_WATCHDOG_DEVICE) {
		dest = cfg->watchdog_device;
		size = sizeof(cfg->watchdog_device);
	}
	n = snprintf(dest, size, "%s", value);

	if (n < 0 || (size_t)n >= size)
		return fail(ps, "value for '%s' is longer than %zu characters", KEYS[id].name, size - 1);
	return 0;
}

static int apply_log_level(parse_state_t *ps, hvacd_config_t *cfg, const char *value)
{
	for (size_t i = 0; i < LOG_LEVEL_COUNT; i++) {
		if (strcmp(value, LOG_LEVEL_NAMES[i]) == 0) {
			cfg->log_level = (config_log_level_t)i;
			return 0;
		}
	}
	return fail(ps, "log_level must be error, warn, info or debug, not '%s'", value);
}

static int apply_value(parse_state_t *ps, hvacd_config_t *cfg, key_id_t id, const char *value)
{
	if (id == KEY_COOL_SETPOINT || id == KEY_HEAT_SETPOINT || id == KEY_DEADBAND)
		ps->band_line = ps->line;
	switch (KEYS[id].kind) {
	case KIND_INT:
		return apply_int(ps, cfg, id, value);
	case KIND_TEXT:
		return apply_text(ps, cfg, id, value);
	default:
		return apply_log_level(ps, cfg, value);
	}
}

/* Handles one line (len bytes at p, no newline). Blank and comment-only lines are ignored. */
static int parse_line(parse_state_t *ps, hvacd_config_t *cfg, const char *p, size_t len)
{
	char buf[CONFIG_LINE_MAX] = {0};
	char *line = NULL;
	char *eq = NULL;
	char *key = NULL;
	const char *value = NULL;
	char *hash = NULL;
	int id = 0;

	if (len >= sizeof(buf))
		return fail(ps, "line longer than %u characters", CONFIG_LINE_MAX - 1u);
	memcpy(buf, p, len);
	hash = strchr(buf, '#');
	if (hash != NULL)
		*hash = '\0';
	line = trim(buf);
	if (*line == '\0')
		return 0;
	eq = strchr(line, '=');
	if (eq == NULL)
		return fail(ps, "expected 'key = value'");
	*eq = '\0';
	key = trim(line);
	value = trim(eq + 1);
	id = find_key(key);
	if (id < 0)
		return fail(ps, "unknown key '%s'", key);
	if (ps->seen[id])
		return fail(ps, "duplicate key '%s'", key);
	ps->seen[id] = true;
	if (*value == '\0')
		return fail(ps, "missing value for '%s'", key);
	return apply_value(ps, cfg, (key_id_t)id, value);
}

int config_parse_string(const char *text, hvacd_config_t *cfg, char *err, size_t err_len)
{
	parse_state_t ps = { .err = err, .err_len = err_len };
	const char *p = text;

	if (text == NULL || cfg == NULL)
		return fail(&ps, "internal error: missing argument");
	config_set_defaults(cfg);
	while (*p != '\0') {
		const char *eol = strchr(p, '\n');
		size_t len = eol != NULL ? (size_t)(eol - p) : strlen(p);

		int ret = 0;

		ps.line++;
		ret = parse_line(&ps, cfg, p, len);
		if (ret != 0)
			return ret;
		p += len;
		if (*p == '\n')
			p++;
	}
	/* Every single value is in range by now, so a failure here is the band-ordering rule. */
	if (hvac_config_validate(&cfg->core) != 0) {
		ps.line = ps.band_line;
		return fail(&ps, "heat_setpoint_mc + deadband_mc must be below "
			    "cool_setpoint_mc - deadband_mc");
	}
	return 0;
}

int config_load_file(const char *path, hvacd_config_t *cfg, char *err, size_t err_len)
{
	parse_state_t ps = { .err = err, .err_len = err_len };
	char buf[CONFIG_FILE_MAX_BYTES + 1u] = {0};
	FILE *f = NULL;
	size_t n = 0;
	bool read_error = false;

	if (path == NULL || cfg == NULL)
		return fail(&ps, "internal error: missing argument");
	f = fopen(path, "r");
	if (f == NULL)
		return fail(&ps, "cannot open %s: %s", path, strerror(errno));
	n = fread(buf, 1, sizeof(buf), f);
	read_error = ferror(f) != 0;
	if (fclose(f) != 0 || read_error)
		return fail(&ps, "cannot read %s", path);
	if (n > CONFIG_FILE_MAX_BYTES)
		return fail(&ps, "%s is larger than %u bytes", path, CONFIG_FILE_MAX_BYTES);
	if (memchr(buf, '\0', n) != NULL)
		return fail(&ps, "%s contains a NUL byte", path);
	return config_parse_string(buf, cfg, err, err_len);
}
