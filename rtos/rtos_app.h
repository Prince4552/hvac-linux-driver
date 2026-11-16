/* SPDX-License-Identifier: MIT */
/*
 * rtos_app: what the three FreeRTOS tasks share (Ext 4): the queue messages and one context struct
 * that main.c fills and passes to every task. Each counter is written by exactly one task, so no
 * lock is needed. Lockstep: plant -> reading -> control -> outputs -> plant -> log -> logger.
 */
#ifndef RTOS_APP_H
#define RTOS_APP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "hvac_core.h"

#define RTOS_SEND_TIMEOUT_TICKS ((TickType_t)100u)	/* a send that waits longer counts as failed */
#define RTOS_LOG_QUEUE_LENGTH 32u
#define RTOS_STACK_WORDS 4096u

typedef struct {
	uint32_t t_s;			/* simulated second (the plant task's clock) */
	int32_t temp_meas_mc;		/* simulated sensor reading */
} rtos_reading_t;

typedef struct {
	hvac_state_t state;
	hvac_outputs_t out;
} rtos_outputs_t;

typedef struct {
	bool end;			/* last message: the logger finishes and ends the scheduler */
	rtos_reading_t reading;
	rtos_outputs_t result;
} rtos_log_t;

typedef struct {
	QueueHandle_t readings;		/* plant -> control, length 1 */
	QueueHandle_t outputs;		/* control -> plant, length 1 */
	QueueHandle_t log;		/* plant -> logger, length RTOS_LOG_QUEUE_LENGTH */
	TaskHandle_t plant;
	TaskHandle_t control;
	TaskHandle_t logger;
	FILE *trace;			/* opened by main(), closed by the logger */
	uint32_t plant_send_failures;
	uint32_t control_send_failures;
	int trace_status;		/* 0, or the first write error */
	UBaseType_t high_water_words[3];	/* plant, control, logger; filled by the logger */
	size_t heap_allocations;	/* successful kernel-heap allocations; filled by the logger */
	size_t min_free_heap_bytes;	/* lowest free kernel heap; filled by the logger */
} rtos_app_t;

/** Task bodies; the parameter is the shared rtos_app_t. */
void plant_task(void *param);
void control_task(void *param);
void logger_task(void *param);

#endif /* RTOS_APP_H */
