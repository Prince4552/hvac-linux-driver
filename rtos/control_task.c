// SPDX-License-Identifier: MIT
/*
 * control_task: the thermostat controller. Waits for a reading, calls the unchanged
 * hvac_core_step() from thermostat/src/hvac_core.c, and sends the outputs back. Highest priority,
 * so it answers as soon as a reading arrives.
 */
#include "rtos_app.h"

#define MS_PER_S 1000u

void control_task(void *param)
{
	rtos_app_t *app = param;
	hvac_config_t cfg = {0};
	hvac_ctx_t core = {0};
	int init_ret = 0;

	hvac_config_set_defaults(&cfg);
	init_ret = hvac_core_init(&core, &cfg);
	configASSERT(init_ret == 0);	/* the defaults are always valid */
	for (;;) {
		rtos_reading_t r = {0};
		rtos_outputs_t result = {0};
		hvac_input_t in = {0};
		BaseType_t got = xQueueReceive(app->readings, &r, portMAX_DELAY);

		configASSERT(got == pdPASS);	/* portMAX_DELAY: waits forever, cannot time out */
		in.read_ok = true;
		in.temp_mc = r.temp_meas_mc;
		in.now_ms = (uint64_t)r.t_s * MS_PER_S;	/* simulated time, not the tick count */
		result.state = hvac_core_step(&core, &in, &result.out);
		if (xQueueSend(app->outputs, &result, RTOS_SEND_TIMEOUT_TICKS) != pdPASS)
			app->control_send_failures++;
	}
}
