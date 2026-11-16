/* SPDX-License-Identifier: MIT */
/*
 * telemetry_json: builds the MQTT telemetry message (Ext 3) as one line of JSON text.
 * Pure: no allocation, no I/O, no clock. The caller passes a fixed-size buffer and its size;
 * telemetry_mqtt.c sends the result. Field meanings follow the Modbus register map (D-029).
 */
#ifndef TELEMETRY_JSON_H
#define TELEMETRY_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hvac_core.h"

#define TELEMETRY_JSON_MAX 512u	/* buffer size used by hvacd, including the final NUL */

/* One telemetry sample: a snapshot of what the Modbus registers show. */
typedef struct {
	const char *device;		/* mqtt_device_id (letters, digits and '-') */
	uint32_t seq;			/* message number, +1 every period */
	uint64_t uptime_s;
	bool has_temp;			/* false until the first valid reading: temp_c is null */
	int32_t temp_mc;
	hvac_state_t state;
	bool compressor;
	bool heater;
	bool fan;
	uint16_t alarms;		/* Modbus register 5 bits */
	uint16_t fdd;			/* Modbus register 8 bits */
	uint32_t compressor_starts;
} telemetry_t;

/**
 * telemetry_format_json() - write @t as JSON into @buf (@len bytes, NUL-terminated).
 * Returns the text length, -ENOSPC if it did not fit (the buffer must then not be sent),
 * or -EINVAL for a missing argument.
 */
int telemetry_format_json(const telemetry_t *t, char *buf, size_t len);

#endif /* TELEMETRY_JSON_H */
