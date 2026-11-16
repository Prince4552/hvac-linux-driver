// SPDX-License-Identifier: MIT
/*
 * plant_task: the simulated room and its sensor (reusing thermostat/sim/room_model.c). Once per tick
 * it sends a reading to the control task, waits for the outputs, hands the step to the logger and
 * advances the room by one simulated second. vTaskDelayUntil() keeps a steady 1-tick period.
 */
#include "rtos_app.h"
#include "room_model.h"
#include "sil_trace.h"

#define PLANT_PERIOD_TICKS ((TickType_t)1u)

static void send_or_count(rtos_app_t *app, QueueHandle_t q, const void *msg)
{
	if (xQueueSend(q, msg, RTOS_SEND_TIMEOUT_TICKS) != pdPASS)
		app->plant_send_failures++;
}

void plant_task(void *param)
{
	rtos_app_t *app = param;
	room_t room = {0};
	TickType_t last_wake = xTaskGetTickCount();
	const rtos_log_t end = { .end = true };

	room_init(&room, room_find_scenario(SIL_TRACE_SCENARIO), SIL_TRACE_SEED);
	for (uint32_t t = 0; t < SIL_TRACE_SECONDS; t++) {
		rtos_log_t step = { .end = false };
		BaseType_t got = pdFAIL;

		step.reading.t_s = t;
		step.reading.temp_meas_mc = room_sensor_read_mc(&room);
		send_or_count(app, app->readings, &step.reading);
		/* Lockstep: the room does not move on until the controller has answered. */
		got = xQueueReceive(app->outputs, &step.result, portMAX_DELAY);
		configASSERT(got == pdPASS);	/* portMAX_DELAY: waits forever, cannot time out */
		send_or_count(app, app->log, &step);
		room_step(&room, t, step.result.out.compressor, step.result.out.heater);
		vTaskDelayUntil(&last_wake, PLANT_PERIOD_TICKS);
	}
	send_or_count(app, app->log, &end);
	vTaskSuspend(NULL);	/* done; the logger ends the scheduler */
}
