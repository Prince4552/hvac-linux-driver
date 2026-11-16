// SPDX-License-Identifier: MIT
/*
 * hvac_rtos: the thermostat's unchanged control core on FreeRTOS (Ext 4), on the POSIX simulator
 * port. Creates three static tasks (control, plant, logger) and three static queues, runs the
 * hot_day scenario for 2 simulated hours, then writes metrics as JSON.
 * Usage: hvac_rtos TRACE_CSV METRICS_JSON. Exit 0 only if the run was clean.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtos_app.h"
#include "sil_trace.h"

#define PRIO_LOGGER (tskIDLE_PRIORITY + 1u)
#define PRIO_PLANT (tskIDLE_PRIORITY + 2u)
#define PRIO_CONTROL (tskIDLE_PRIORITY + 3u)

/* Static storage for every task and queue: nothing comes from the heap (D-030). */
static StaticTask_t tcb_plant, tcb_control, tcb_logger, tcb_idle;
static StackType_t stack_plant[RTOS_STACK_WORDS], stack_control[RTOS_STACK_WORDS];
static StackType_t stack_logger[RTOS_STACK_WORDS], stack_idle[configMINIMAL_STACK_SIZE];
static StaticQueue_t q_readings, q_outputs, q_log;
static uint8_t q_readings_buf[sizeof(rtos_reading_t)];
static uint8_t q_outputs_buf[sizeof(rtos_outputs_t)];
static uint8_t q_log_buf[RTOS_LOG_QUEUE_LENGTH * sizeof(rtos_log_t)];

static rtos_app_t app;

void rtos_assert_failed(const char *file, unsigned long line)
{
	fprintf(stderr, "hvac_rtos: assertion failed at %s:%lu\n", file, line);
	abort();
}

/* The signature is fixed by FreeRTOS (task.h), so 'name' cannot be made const. */
// cppcheck-suppress constParameterPointer
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
	(void)task;
	fprintf(stderr, "hvac_rtos: stack overflow in task %s\n", name);
	abort();
}

void vApplicationMallocFailedHook(void)
{
	fprintf(stderr, "hvac_rtos: kernel heap exhausted\n");
	abort();
}

/* The kernel asks for the idle task's memory because configSUPPORT_STATIC_ALLOCATION is 1. */
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, configSTACK_DEPTH_TYPE *words)
{
	*tcb = &tcb_idle;
	*stack = stack_idle;
	*words = configMINIMAL_STACK_SIZE;
}

static int create_objects(void)
{
	app.readings = xQueueCreateStatic(1u, sizeof(rtos_reading_t), q_readings_buf, &q_readings);
	app.outputs = xQueueCreateStatic(1u, sizeof(rtos_outputs_t), q_outputs_buf, &q_outputs);
	app.log = xQueueCreateStatic(RTOS_LOG_QUEUE_LENGTH, sizeof(rtos_log_t), q_log_buf, &q_log);
	app.control = xTaskCreateStatic(control_task, "control", RTOS_STACK_WORDS, &app, PRIO_CONTROL,
					stack_control, &tcb_control);
	app.plant = xTaskCreateStatic(plant_task, "plant", RTOS_STACK_WORDS, &app, PRIO_PLANT,
				      stack_plant, &tcb_plant);
	app.logger = xTaskCreateStatic(logger_task, "logger", RTOS_STACK_WORDS, &app, PRIO_LOGGER,
				       stack_logger, &tcb_logger);
	if (app.readings == NULL || app.outputs == NULL || app.log == NULL || app.control == NULL ||
	    app.plant == NULL || app.logger == NULL)
		return -ENOMEM;
	return 0;
}

static int write_metrics(const char *path)
{
	FILE *f = fopen(path, "w");
	int n = 0;

	if (f == NULL)
		return -errno;
	n = fprintf(f, "{\"schema\": 1, \"scenario\": \"%s\", \"seconds\": %u, \"seed\": %u,\n"
		    " \"rtos_queue_send_failures\": %u, \"rtos_min_free_heap_bytes\": %zu,\n"
		    " \"rtos_heap_allocations\": %zu, \"rtos_heap_total_bytes\": %zu,\n"
		    " \"rtos_stack_high_water_words\": {\"plant\": %lu, \"control\": %lu, "
		    "\"logger\": %lu},\n \"rtos_stack_words\": %u}\n",
		    SIL_TRACE_SCENARIO, SIL_TRACE_SECONDS, SIL_TRACE_SEED,
		    app.plant_send_failures + app.control_send_failures,
		    app.min_free_heap_bytes, app.heap_allocations, (size_t)configTOTAL_HEAP_SIZE,
		    (unsigned long)app.high_water_words[0],
		    (unsigned long)app.high_water_words[1], (unsigned long)app.high_water_words[2],
		    RTOS_STACK_WORDS);
	if (fclose(f) != 0 || n < 0)
		return -EIO;
	return 0;
}

int main(int argc, char **argv)
{
	int ret = 0;

	if (argc != 3) {
		fprintf(stderr, "usage: %s TRACE_CSV METRICS_JSON\n", argv[0]);
		return 2;
	}
	app.trace = fopen(argv[1], "w");
	if (app.trace == NULL || sil_trace_header(app.trace) != 0 || create_objects() != 0) {
		fprintf(stderr, "hvac_rtos: start-up failed: %s\n", strerror(errno));
		return 1;
	}
	vTaskStartScheduler();	/* returns after the logger calls vTaskEndScheduler() */
	ret = write_metrics(argv[2]);
	if (ret != 0 || app.trace_status != 0) {
		fprintf(stderr, "hvac_rtos: writing results failed\n");
		return 1;
	}
	printf("hvac_rtos: %u simulated seconds done, %u queue send failures\n", SIL_TRACE_SECONDS,
	       app.plant_send_failures + app.control_send_failures);
	return 0;
}
