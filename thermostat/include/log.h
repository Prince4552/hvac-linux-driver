/* SPDX-License-Identifier: MIT */
/*
 * log: hvacd's log lines on stderr, "<ISO-8601 UTC time> hvacd <LEVEL> <message>"
 * (PROJECT_PLAN.md section 4.7). Messages below the configured level are dropped.
 */
#ifndef LOG_H
#define LOG_H

#include "config.h"

/** Sets the lowest level that is printed (default: info). */
void log_set_level(config_log_level_t level);

/** Prints one log line if level is enabled. printf-style format. */
__attribute__((format(printf, 2, 3)))
void log_msg(config_log_level_t level, const char *fmt, ...);

#define LOG_ERROR CONFIG_LOG_ERROR
#define LOG_WARN CONFIG_LOG_WARN
#define LOG_INFO CONFIG_LOG_INFO
#define LOG_DEBUG CONFIG_LOG_DEBUG

#endif /* LOG_H */
