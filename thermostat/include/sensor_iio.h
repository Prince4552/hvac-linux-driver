/* SPDX-License-Identifier: MIT */
/*
 * sensor_iio: reads the temperature from the hvac_temp IIO device in sysfs
 * (PROJECT_PLAN.md section 4.7). Finds the device by its "name" file, reads in_temp_raw and
 * in_temp_scale, and converts to millidegrees with integer math. After any failure the device
 * is searched for again on the next read (it may have been removed and re-created).
 */
#ifndef SENSOR_IIO_H
#define SENSOR_IIO_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#define SENSOR_IIO_DIR_MAX (CONFIG_PATH_MAX + CONFIG_NAME_MAX)

typedef struct {
	char base_dir[CONFIG_PATH_MAX];		/* e.g. /sys/bus/iio/devices */
	char name[CONFIG_NAME_MAX];		/* e.g. hvac_temp */
	char dev_dir[SENSOR_IIO_DIR_MAX];	/* found device folder, valid when found */
	bool found;
} sensor_iio_t;

/** Stores where and what to look for. Returns 0 or -EINVAL (NULL or too long). */
int sensor_iio_init(sensor_iio_t *s, const char *base_dir, const char *name);

/** Reads one temperature in millidegrees. Returns 0, or a negative errno value on failure. */
int sensor_iio_read_mc(sensor_iio_t *s, int32_t *temp_mc);

/**
 * Parses a fixed-point decimal with up to 6 fractional digits ("62.500000", "0.5") into
 * micro-units (62500000, 500000). Trailing white space allowed. Returns 0 or -EINVAL.
 */
int sensor_iio_parse_scale(const char *text, int64_t *scale_micro);

/** Parses a signed decimal integer (trailing white space allowed). Returns 0 or -EINVAL. */
int sensor_iio_parse_raw(const char *text, int64_t *raw);

/**
 * temp_mc = round(raw x scale_micro / 1000000), ties away from zero, computed in int64_t.
 * Returns 0, or -ERANGE if an input or the result is out of range.
 */
int sensor_iio_to_mc(int64_t raw, int64_t scale_micro, int32_t *temp_mc);

#endif /* SENSOR_IIO_H */
