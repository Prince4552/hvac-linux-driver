// SPDX-License-Identifier: MIT
/*
 * log: formats and prints hvacd log lines to stderr with a UTC timestamp and level name.
 * One fixed-size buffer per line; an over-long message is cut off rather than overflowing.
 */
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#define LOG_LINE_MAX 512u
#define LOG_TIME_MAX 32u

static config_log_level_t current_level = CONFIG_LOG_INFO;

static const char *const LEVEL_NAMES[] = { "ERROR", "WARN", "INFO", "DEBUG" };

void log_set_level(config_log_level_t level)
{
	current_level = level;
}

static void format_utc_now(char *buf, size_t len)
{
	struct timespec ts = {0};
	struct tm tm_utc = {0};

	if (clock_gettime(CLOCK_REALTIME, &ts) != 0 || gmtime_r(&ts.tv_sec, &tm_utc) == NULL ||
	    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc) == 0u)
		(void)snprintf(buf, len, "%s", "1970-01-01T00:00:00Z");
}

void log_msg(config_log_level_t level, const char *fmt, ...)
{
	char when[LOG_TIME_MAX] = {0};
	char msg[LOG_LINE_MAX] = {0};
	va_list ap;
	int n = 0;

	if (level > current_level || (unsigned)level > (unsigned)CONFIG_LOG_DEBUG)
		return;
	va_start(ap, fmt);
	n = vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	if (n < 0)
		(void)snprintf(msg, sizeof(msg), "%s", "(log formatting error)");
	/* n >= sizeof(msg) means the message was cut short; still print what fits. */
	format_utc_now(when, sizeof(when));
	(void)fprintf(stderr, "%s hvacd %s %s\n", when, LEVEL_NAMES[level], msg);
}
