// SPDX-License-Identifier: MIT
/*
 * telemetry_mqtt: libmosquitto client for hvacd's telemetry (Ext 3, decision D-029).
 * Everything is non-blocking: connections start with *_connect_async(), and each loop iteration
 * runs mosquitto_loop(mosq, 0, 1), which returns at once. Network trouble only means skipped
 * messages and a reconnect attempt every 5 s; it can never delay the control loop.
 */
#include "telemetry_mqtt.h"

#include <errno.h>
#include <mosquitto.h>
#include <stdio.h>
#include <string.h>

#include "log.h"

#define MQTT_QOS 1
#define MQTT_KEEPALIVE_S 30
#define MS_PER_S 1000u
#define SHUTDOWN_STEPS 10	/* clean shutdown waits at most 10 x 100 ms for "offline" */
#define SHUTDOWN_STEP_MS 100
#define STATUS_ONLINE "online"
#define STATUS_OFFLINE "offline"

static int publish_status(telemetry_mqtt_t *t, const char *text, int *mid)
{
	return mosquitto_publish(t->mosq, mid, t->topic_status, (int)strlen(text), text, MQTT_QOS,
				 true);
}

/* libmosquitto callbacks: they run inside mosquitto_loop(), in hvacd's only thread. */
static void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
	telemetry_mqtt_t *t = obj;

	(void)mosq;
	if (rc != 0) {
		log_msg(LOG_WARN, "MQTT broker %s:%u refused the connection: %s", t->host, t->port,
			mosquitto_connack_string(rc));
		return;
	}
	t->connected = true;
	log_msg(LOG_INFO, "MQTT connected to %s:%u", t->host, t->port);
	if (publish_status(t, STATUS_ONLINE, NULL) != MOSQ_ERR_SUCCESS)
		log_msg(LOG_WARN, "MQTT could not publish the online status");
}

static void on_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
	telemetry_mqtt_t *t = obj;

	(void)mosq;
	if (t->connected && rc != 0)
		log_msg(LOG_WARN, "MQTT connection to %s:%u lost; retrying every %u s", t->host,
			t->port, TELEMETRY_RECONNECT_MS / MS_PER_S);
	t->connected = false;
}

static void on_publish(struct mosquitto *mosq, void *obj, int mid)
{
	telemetry_mqtt_t *t = obj;

	(void)mosq;
	if (mid == t->offline_mid)
		t->offline_sent = true;
}

static int make_topics(telemetry_mqtt_t *t)
{
	int a = snprintf(t->topic_telemetry, sizeof(t->topic_telemetry), "hvac/%s/telemetry",
			 t->device);
	int b = snprintf(t->topic_status, sizeof(t->topic_status), "hvac/%s/status", t->device);

	if (a < 0 || b < 0 || (size_t)a >= sizeof(t->topic_telemetry) ||
	    (size_t)b >= sizeof(t->topic_status))
		return -EINVAL;
	return 0;
}

int telemetry_mqtt_open(telemetry_mqtt_t *t, const char *host, uint16_t port, const char *device,
			uint32_t period_s, uint64_t now_ms)
{
	int rc = 0;

	if (t == NULL || host == NULL || device == NULL || period_s == 0u)
		return -EINVAL;
	*t = (telemetry_mqtt_t){ .offline_mid = -1, .device = device, .host = host, .port = port };
	t->period_ms = (uint64_t)period_s * MS_PER_S;
	t->next_publish_ms = now_ms + t->period_ms;
	t->next_attempt_ms = now_ms + TELEMETRY_RECONNECT_MS;
	if (make_topics(t) != 0)
		return -EINVAL;
	if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS)
		return -ENOMEM;
	t->mosq = mosquitto_new(device, true, t);
	if (t->mosq == NULL) {
		(void)mosquitto_lib_cleanup();
		return -ENOMEM;
	}
	mosquitto_connect_callback_set(t->mosq, on_connect);
	mosquitto_disconnect_callback_set(t->mosq, on_disconnect);
	mosquitto_publish_callback_set(t->mosq, on_publish);
	rc = mosquitto_will_set(t->mosq, t->topic_status, (int)strlen(STATUS_OFFLINE), STATUS_OFFLINE,
				MQTT_QOS, true);
	if (rc != MOSQ_ERR_SUCCESS) {
		telemetry_mqtt_close(t);
		return -ENOMEM;
	}
	/* A broker that is down is fine: telemetry_mqtt_service() retries later. */
	rc = mosquitto_connect_async(t->mosq, host, port, MQTT_KEEPALIVE_S);
	if (rc != MOSQ_ERR_SUCCESS)
		log_msg(LOG_WARN, "MQTT cannot reach %s:%u yet (%s); retrying every %u s", host, port,
			mosquitto_strerror(rc), TELEMETRY_RECONNECT_MS / MS_PER_S);
	return 0;
}

void telemetry_mqtt_add_fds(const telemetry_mqtt_t *t, fd_set *readable, int *max_fd)
{
	int fd = t->mosq != NULL ? mosquitto_socket(t->mosq) : -1;

	if (fd < 0 || fd >= FD_SETSIZE)
		return;
	FD_SET(fd, readable);
	if (fd > *max_fd)
		*max_fd = fd;
}

void telemetry_mqtt_service(telemetry_mqtt_t *t, uint64_t now_ms)
{
	int rc = 0;

	if (t->mosq == NULL)
		return;
	rc = mosquitto_loop(t->mosq, 0, 1);	/* timeout 0: never waits */
	if (rc == MOSQ_ERR_SUCCESS || now_ms < t->next_attempt_ms)
		return;
	t->connected = false;
	t->next_attempt_ms = now_ms + TELEMETRY_RECONNECT_MS;
	rc = mosquitto_reconnect_async(t->mosq);
	if (rc != MOSQ_ERR_SUCCESS)
		log_msg(LOG_DEBUG, "MQTT reconnect to %s:%u failed: %s", t->host, t->port,
			mosquitto_strerror(rc));
}

void telemetry_mqtt_publish_due(telemetry_mqtt_t *t, const telemetry_t *sample, uint64_t now_ms)
{
	char buf[TELEMETRY_JSON_MAX] = {0};
	telemetry_t msg = *sample;
	int n = 0;
	int rc = 0;

	if (t->mosq == NULL || now_ms < t->next_publish_ms)
		return;
	t->next_publish_ms += t->period_ms;
	if (t->next_publish_ms <= now_ms)
		t->next_publish_ms = now_ms + t->period_ms;	/* fell behind: skip, never burst */
	msg.seq = t->seq++;
	msg.device = t->device;
	if (!t->connected)
		return;	/* skipped; the gap in seq shows it */
	n = telemetry_format_json(&msg, buf, sizeof(buf));
	if (n < 0) {
		log_msg(LOG_WARN, "telemetry message %u not sent: %s", msg.seq, strerror(-n));
		return;
	}
	rc = mosquitto_publish(t->mosq, NULL, t->topic_telemetry, n, buf, MQTT_QOS, false);
	if (rc != MOSQ_ERR_SUCCESS)
		log_msg(LOG_DEBUG, "telemetry message %u not sent: %s", msg.seq,
			mosquitto_strerror(rc));
}

void telemetry_mqtt_close(telemetry_mqtt_t *t)
{
	if (t == NULL || t->mosq == NULL)
		return;
	if (t->connected && publish_status(t, STATUS_OFFLINE, &t->offline_mid) == MOSQ_ERR_SUCCESS) {
		for (int i = 0; i < SHUTDOWN_STEPS && !t->offline_sent && t->connected; i++)
			(void)mosquitto_loop(t->mosq, SHUTDOWN_STEP_MS, 1);
		(void)mosquitto_disconnect(t->mosq);
	}
	mosquitto_destroy(t->mosq);
	t->mosq = NULL;
	t->connected = false;
	(void)mosquitto_lib_cleanup();
}
