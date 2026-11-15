// SPDX-License-Identifier: MIT
/*
 * sensor_iio: discovery of the IIO device by name and temperature reading through sysfs.
 * Text parsing and the fixed-point conversion are separate pure functions so they can be tested
 * on their own; file access uses small fixed buffers and checks every return value.
 */
#include "sensor_iio.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MICRO 1000000LL
#define SCALE_FRAC_DIGITS 6
#define SCALE_INT_DIGITS_MAX 6		/* scale below 1,000,000: keeps raw x scale in int64 */
#define RAW_ABS_MAX 1000000LL		/* far beyond a 12-bit chip; guards the multiplication */
#define TEXT_MAX 64u

int sensor_iio_init(sensor_iio_t *s, const char *base_dir, const char *name)
{
	int n1 = 0;
	int n2 = 0;

	if (s == NULL || base_dir == NULL || name == NULL)
		return -EINVAL;
	*s = (sensor_iio_t){0};
	n1 = snprintf(s->base_dir, sizeof(s->base_dir), "%s", base_dir);
	n2 = snprintf(s->name, sizeof(s->name), "%s", name);
	if (n1 < 0 || (size_t)n1 >= sizeof(s->base_dir) || n2 < 0 || (size_t)n2 >= sizeof(s->name))
		return -EINVAL;
	return 0;
}

/* Reads a small text file into buf (NUL-terminated). Returns 0 or a negative errno value. */
static int read_text_file(const char *path, char *buf, size_t len)
{
	ssize_t n = 0;
	int err = 0;
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd < 0)
		return -errno;
	n = read(fd, buf, len - 1u);
	err = n < 0 ? -errno : 0;
	if (close(fd) != 0 && err == 0)
		err = -errno;
	if (err != 0)
		return err;
	buf[n] = '\0';
	return 0;
}

static int join_path(char *buf, size_t len, const char *a, const char *b)
{
	int n = snprintf(buf, len, "%s/%s", a, b);

	return (n < 0 || (size_t)n >= len) ? -ENAMETOOLONG : 0;
}

/* True if base_dir/entry/name holds exactly the wanted name (plus a newline). */
static bool entry_matches(const sensor_iio_t *s, const char *entry)
{
	char dir[SENSOR_IIO_DIR_MAX] = {0};
	char path[SENSOR_IIO_DIR_MAX + 8u] = {0};
	char text[TEXT_MAX] = {0};
	size_t len = 0;

	if (join_path(dir, sizeof(dir), s->base_dir, entry) != 0 ||
	    join_path(path, sizeof(path), dir, "name") != 0 ||
	    read_text_file(path, text, sizeof(text)) != 0)
		return false;
	len = strlen(text);
	while (len > 0u && isspace((unsigned char)text[len - 1u]))
		text[--len] = '\0';
	return strcmp(text, s->name) == 0;
}

/* Finds the matching device folder; picks the alphabetically first one for repeatability. */
static int discover(sensor_iio_t *s)
{
	char best[CONFIG_NAME_MAX] = {0};
	const struct dirent *e = NULL;
	DIR *d = opendir(s->base_dir);

	s->found = false;
	if (d == NULL)
		return -errno;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof(best))
			continue;	/* hidden entry, or a name too long to be an IIO device */
		if (entry_matches(s, e->d_name) && (best[0] == '\0' || strcmp(e->d_name, best) < 0)) {
			int n = snprintf(best, sizeof(best), "%s", e->d_name);

			if (n < 0 || (size_t)n >= sizeof(best))
				best[0] = '\0';	/* cannot happen: length checked above */
		}
	}
	if (closedir(d) != 0)
		return -errno;
	if (best[0] == '\0')
		return -ENODEV;
	if (join_path(s->dev_dir, sizeof(s->dev_dir), s->base_dir, best) != 0)
		return -ENAMETOOLONG;
	s->found = true;
	return 0;
}

static const char *skip_trailing_space_check(const char *p)
{
	while (isspace((unsigned char)*p))
		p++;
	return p;
}

int sensor_iio_parse_raw(const char *text, int64_t *raw)
{
	int64_t v = 0;
	bool neg = false;
	int digits = 0;
	const char *p = text;

	if (text == NULL || raw == NULL)
		return -EINVAL;
	if (*p == '-' || *p == '+')
		neg = (*p++ == '-');
	for (; isdigit((unsigned char)*p); p++, digits++) {
		if (v > RAW_ABS_MAX)
			return -EINVAL;
		v = v * 10 + (*p - '0');
	}
	if (digits == 0 || *skip_trailing_space_check(p) != '\0' || v > RAW_ABS_MAX)
		return -EINVAL;
	*raw = neg ? -v : v;
	return 0;
}

int sensor_iio_parse_scale(const char *text, int64_t *scale_micro)
{
	int64_t whole = 0;
	int64_t frac = 0;
	int int_digits = 0;
	int frac_digits = 0;
	const char *p = text;

	if (text == NULL || scale_micro == NULL)
		return -EINVAL;
	for (; isdigit((unsigned char)*p); p++, int_digits++) {
		if (int_digits == SCALE_INT_DIGITS_MAX)
			return -EINVAL;
		whole = whole * 10 + (*p - '0');
	}
	if (*p == '.') {
		for (p++; isdigit((unsigned char)*p); p++, frac_digits++) {
			if (frac_digits == SCALE_FRAC_DIGITS)
				return -EINVAL;
			frac = frac * 10 + (*p - '0');
		}
	}
	if (int_digits + frac_digits == 0 || *skip_trailing_space_check(p) != '\0')
		return -EINVAL;
	for (int i = frac_digits; i < SCALE_FRAC_DIGITS; i++)
		frac *= 10;
	*scale_micro = whole * MICRO + frac;
	return 0;
}

int sensor_iio_to_mc(int64_t raw, int64_t scale_micro, int32_t *temp_mc)
{
	int64_t product = 0;
	int64_t mc = 0;

	if (temp_mc == NULL || raw > RAW_ABS_MAX || raw < -RAW_ABS_MAX || scale_micro < 0 ||
	    scale_micro >= MICRO * MICRO)
		return -ERANGE;
	product = raw * scale_micro;	/* at most 1e6 x 1e12 = 1e18 < 9.2e18 */
	mc = product >= 0 ? (product + MICRO / 2) / MICRO : -((-product + MICRO / 2) / MICRO);
	if (mc > INT32_MAX || mc < INT32_MIN)
		return -ERANGE;
	*temp_mc = (int32_t)mc;
	return 0;
}

static int read_attr(const sensor_iio_t *s, const char *attr, char *buf, size_t len)
{
	char path[SENSOR_IIO_DIR_MAX + 32u] = {0};
	int ret = join_path(path, sizeof(path), s->dev_dir, attr);

	return ret != 0 ? ret : read_text_file(path, buf, len);
}

int sensor_iio_read_mc(sensor_iio_t *s, int32_t *temp_mc)
{
	char raw_text[TEXT_MAX] = {0};
	char scale_text[TEXT_MAX] = {0};
	int64_t raw = 0;
	int64_t scale = 0;
	int ret = 0;

	if (s == NULL || temp_mc == NULL)
		return -EINVAL;
	if (!s->found)
		ret = discover(s);
	if (ret == 0)
		ret = read_attr(s, "in_temp_raw", raw_text, sizeof(raw_text));
	if (ret == 0)
		ret = read_attr(s, "in_temp_scale", scale_text, sizeof(scale_text));
	if (ret == 0)
		ret = sensor_iio_parse_raw(raw_text, &raw);
	if (ret == 0)
		ret = sensor_iio_parse_scale(scale_text, &scale);
	if (ret == 0)
		ret = sensor_iio_to_mc(raw, scale, temp_mc);
	if (ret != 0)
		s->found = false;	/* search again on the next read */
	return ret;
}
