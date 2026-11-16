/* SPDX-License-Identifier: MIT */
/*
 * FreeRTOSConfig.h: kernel settings for the FreeRTOS port of the thermostat (Ext 4, decision D-030).
 * Runs on the official POSIX simulator port, so FreeRTOS is an ordinary Linux program here.
 * 1 tick = 1 ms of real time = 1 simulated second of the room (the plant task's clock).
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* Scheduling */
#define configUSE_PREEMPTION 1			/* a higher-priority ready task runs at once */
#define configTICK_RATE_HZ 1000			/* 1 ms ticks */
#define configMAX_PRIORITIES 4			/* 0 idle, 1 logger, 2 plant, 3 control */
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD 1
#define configMAX_TASK_NAME_LEN 12

/* Memory: application objects are static; the kernel heap (heap_4) is used only by the kernel. */
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configTOTAL_HEAP_SIZE ((size_t)(64u * 1024u))
/* Each task runs on a pthread that uses this stack; it must exceed PTHREAD_STACK_MIN (16 KiB). */
#define configMINIMAL_STACK_SIZE ((unsigned short)4096u)	/* words; 32 KiB on 64-bit Linux */

/* Features kept off to stay small */
#define configUSE_TIMERS 0
#define configUSE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configUSE_TASK_NOTIFICATIONS 1
#define configQUEUE_REGISTRY_SIZE 0
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_TRACE_FACILITY 0
#define configGENERATE_RUN_TIME_STATS 0
#define configUSE_CO_ROUTINES 0

/* Safety checks: every failure stops the program with a message (hooks are in main.c). */
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK 1
void rtos_assert_failed(const char *file, unsigned long line);
#define configASSERT(x) \
	do { \
		if (!(x)) \
			rtos_assert_failed(__FILE__, __LINE__); \
	} while (0)

/* API functions this program uses */
#define INCLUDE_xTaskDelayUntil 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelete 1				/* used by the POSIX port when ending */
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetSchedulerState 1

#endif /* FREERTOS_CONFIG_H */
