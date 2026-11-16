/* SPDX-License-Identifier: MIT */
/*
 * telemetry_mqtt: sends hvacd's telemetry to an MQTT broker (Ext 3) without ever blocking the
 * control loop. Single-threaded: hvacd calls telemetry_mqtt_service() once per loop iteration,
 * which runs one non-blocking libmosquitto step and reconnects (at most every 5 s) when needed.
 * Topics: hvac/<device>/telemetry (JSON, QoS 1) and hvac/<device>/status (retained online/offline).
 */
#ifndef TELEMETRY_MQTT_H
#define TELEMETRY_MQTT_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/select.h>

#include "telemetry_json.h"

#define TELEMETRY_TOPIC_MAX 80u		/* "hvac/" + 32-character id + "/telemetry" fits */
#define TELEMETRY_RECONNECT_MS 5000u	/* fixed back-off between connection attempts */

struct mosquitto;	/* libmosquitto's client handle (opaque) */

typedef struct {
	struct mosquitto *mosq;		/* NULL when closed */
	bool connected;			/* set and cleared by libmosquitto callbacks */
	bool offline_sent;		/* clean shutdown: the "offline" message was acknowledged */
	int offline_mid;		/* message id of that "offline" message */
	uint64_t next_attempt_ms;	/* earliest time of the next connection attempt */
	uint64_t next_publish_ms;
	uint64_t period_ms;
	uint32_t seq;
	const char *device;		/* points into the caller's configuration */
	const char *host;		/* for log messages */
	uint16_t port;
	char topic_telemetry[TELEMETRY_TOPIC_MAX];
	char topic_status[TELEMETRY_TOPIC_MAX];
} telemetry_mqtt_t;

/**
 * telemetry_mqtt_open() - create the client and start connecting (never waits for the broker).
 * @device and @host must stay valid until telemetry_mqtt_close(). A broker that is down is not an
 * error: hvacd keeps controlling and keeps retrying. Returns 0, -EINVAL or -ENOMEM.
 */
int telemetry_mqtt_open(telemetry_mqtt_t *t, const char *host, uint16_t port, const char *device,
			uint32_t period_s, uint64_t now_ms);

/** Adds the MQTT socket (if any) to a select() read set so replies wake the loop. */
void telemetry_mqtt_add_fds(const telemetry_mqtt_t *t, fd_set *readable, int *max_fd);

/** One non-blocking network step; starts a new connection attempt when the back-off allows it. */
void telemetry_mqtt_service(telemetry_mqtt_t *t, uint64_t now_ms);

/**
 * telemetry_mqtt_publish_due() - publish @sample if a period has passed. The message number advances
 * every period; while disconnected the message is skipped (a subscriber sees a gap in "seq").
 */
void telemetry_mqtt_publish_due(telemetry_mqtt_t *t, const telemetry_t *sample, uint64_t now_ms);

/** Clean shutdown: retained "offline" (waits at most 1 s), disconnect, free everything. */
void telemetry_mqtt_close(telemetry_mqtt_t *t);

#endif /* TELEMETRY_MQTT_H */
