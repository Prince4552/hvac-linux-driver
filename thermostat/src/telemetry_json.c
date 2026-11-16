// SPDX-License-Identifier: MIT
/*
 * telemetry_json: formats one telemetry sample as JSON with snprintf() into the caller's buffer.
 * The temperature uses the same rounding as Modbus register 0, so both interfaces always agree.
 */
#include "telemetry_json.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>

#include "modbus_regs.h"

#define TEMP_TEXT_MAX 16u
#define CC_PER_DEGREE 100

static const char *json_bool(bool b)
{
	return b ? "true" : "false";
}

/* "27.06", "-0.50" or "null" (no valid reading yet). */
static void format_temp_c(char *out, size_t len, const telemetry_t *t)
{
	int16_t cc = 0;
	int abs_cc = 0;

	if (!t->has_temp) {
		(void)snprintf(out, len, "%s", "null");
		return;
	}
	cc = mb_mc_to_cc(t->temp_mc);
	abs_cc = cc < 0 ? -(int)cc : (int)cc;
	/* At most 7 characters ("-327.67"); the buffer holds 16, so it cannot be cut short. */
	(void)snprintf(out, len, "%s%d.%02d", cc < 0 ? "-" : "", abs_cc / CC_PER_DEGREE,
		       abs_cc % CC_PER_DEGREE);
}

int telemetry_format_json(const telemetry_t *t, char *buf, size_t len)
{
	char temp[TEMP_TEXT_MAX] = {0};
	int n = 0;

	if (t == NULL || buf == NULL || len == 0u || t->device == NULL)
		return -EINVAL;
	format_temp_c(temp, sizeof(temp), t);
	n = snprintf(buf, len,
		     "{\"device\":\"%s\",\"seq\":%" PRIu32 ",\"uptime_s\":%" PRIu64 ",\"temp_c\":%s,"
		     "\"state\":\"%s\",\"compressor\":%s,\"heater\":%s,\"fan\":%s,"
		     "\"alarms\":%u,\"fdd\":%u,\"compressor_starts\":%" PRIu32 "}",
		     t->device, t->seq, t->uptime_s, temp, hvac_state_name(t->state),
		     json_bool(t->compressor), json_bool(t->heater), json_bool(t->fan),
		     (unsigned)t->alarms, (unsigned)t->fdd, t->compressor_starts);
	if (n < 0)
		return -EINVAL;
	if ((size_t)n >= len)
		return -ENOSPC;
	return n;
}
