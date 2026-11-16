// SPDX-License-Identifier: MIT
/*
 * logger_task: lowest priority. Writes each step to the trace with the shared sil_trace_row(), so
 * slow file output never delays control. On the final message it records the kernel-heap use and
 * every task's stack high-water mark, closes the trace and ends the scheduler (main() then writes
 * the metrics). On the POSIX port the high-water mark covers only the FreeRTOS stack area, not the
 * Linux thread's own stack (D-030).
 */
#include <errno.h>

#include "rtos_app.h"
#include "sil_trace.h"

/* heap_4 sets up its bookkeeping on the first allocation; with none, the whole heap stayed free. */
static void record_heap(rtos_app_t *app)
{
	HeapStats_t stats = {0};

	vPortGetHeapStats(&stats);
	app->heap_allocations = stats.xNumberOfSuccessfulAllocations;
	app->min_free_heap_bytes = stats.xNumberOfSuccessfulAllocations == 0u ?
				   (size_t)configTOTAL_HEAP_SIZE : stats.xMinimumEverFreeBytesRemaining;
}

static void finish(rtos_app_t *app)
{
	record_heap(app);
	app->high_water_words[0] = uxTaskGetStackHighWaterMark(app->plant);
	app->high_water_words[1] = uxTaskGetStackHighWaterMark(app->control);
	app->high_water_words[2] = uxTaskGetStackHighWaterMark(NULL);	/* this task */
	if (fclose(app->trace) != 0 && app->trace_status == 0)
		app->trace_status = -EIO;
	app->trace = NULL;
	vTaskEndScheduler();
}

void logger_task(void *param)
{
	rtos_app_t *app = param;

	for (;;) {
		rtos_log_t msg = { .end = false };
		BaseType_t got = xQueueReceive(app->log, &msg, portMAX_DELAY);

		configASSERT(got == pdPASS);	/* portMAX_DELAY: waits forever, cannot time out */
		if (msg.end)
			break;
		if (app->trace_status == 0)
			app->trace_status = sil_trace_row(app->trace, msg.reading.t_s,
							  msg.reading.temp_meas_mc, msg.result.state,
							  &msg.result.out);
	}
	finish(app);
	vTaskSuspend(NULL);	/* not reached on the POSIX port: the scheduler has ended */
}
