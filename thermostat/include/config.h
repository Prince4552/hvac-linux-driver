/* SPDX-License-Identifier: MIT */
/*
 * config: reads hvacd's "key = value" configuration file (PROJECT_PLAN.md section 4.5).
 * config_parse_string() is pure (text in, struct out) so it can be unit-tested without files;
 * config_load_file() only reads the file into a fixed 8 KB buffer and then calls the parser.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "hvac_core.h"

#define CONFIG_FILE_MAX_BYTES 8192u	/* largest accepted configuration file */
#define CONFIG_PATH_MAX 256u		/* buffer size for path values, including the final NUL */
#define CONFIG_NAME_MAX 64u		/* buffer size for name values, including the final NUL */

typedef enum {
	CONFIG_LOG_ERROR = 0,
	CONFIG_LOG_WARN = 1,
	CONFIG_LOG_INFO = 2,
	CONFIG_LOG_DEBUG = 3
} config_log_level_t;

/* Everything hvacd can be configured with. The control part is passed to hvac_core unchanged. */
typedef struct {
	hvac_config_t core;
	uint32_t loop_period_ms;		/* 100..10000, default 1000 */
	char iio_base_dir[CONFIG_PATH_MAX];	/* default /sys/bus/iio/devices */
	char iio_device_name[CONFIG_NAME_MAX];	/* default hvac_temp */
	uint16_t modbus_port;			/* 1..65535, default 502 */
	uint32_t modbus_max_clients;		/* 1..8, default 4 */
	config_log_level_t log_level;		/* default info */
	/* Ext 1: watchdog supervision */
	uint32_t watchdog_enabled;		/* 0 or 1, default 0 */
	char watchdog_device[CONFIG_PATH_MAX];	/* default /dev/watchdog */
	uint32_t watchdog_timeout_s;		/* 2..60, default 10 */
} hvacd_config_t;

/** Fills cfg with every default from section 4.5. */
void config_set_defaults(hvacd_config_t *cfg);

/**
 * Parses configuration text (NUL-terminated). Starts from the defaults, applies each line, then
 * checks cross-field rules. On error returns -EINVAL and writes "config:<line>: <message>" to err.
 */
int config_parse_string(const char *text, hvacd_config_t *cfg, char *err, size_t err_len);

/** Reads a file of at most CONFIG_FILE_MAX_BYTES and parses it. Errors as above (line 0 = file). */
int config_load_file(const char *path, hvacd_config_t *cfg, char *err, size_t err_len);

/** "error", "warn", "info", "debug". */
const char *config_log_level_name(config_log_level_t level);

#endif /* CONFIG_H */
