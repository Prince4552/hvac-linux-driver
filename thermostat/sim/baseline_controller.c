// SPDX-License-Identifier: MIT
/*
 * baseline_controller: naive on/off thermostat (simulator only). At exactly the setpoint the
 * output keeps its previous value; anywhere else it follows the temperature immediately.
 */
#include "baseline_controller.h"

#define BASELINE_COOL_MC 26000
#define BASELINE_HEAT_MC 20000

void baseline_step(baseline_t *b, int32_t temp_mc)
{
	if (temp_mc > BASELINE_COOL_MC)
		b->compressor = true;
	else if (temp_mc < BASELINE_COOL_MC)
		b->compressor = false;

	if (temp_mc < BASELINE_HEAT_MC)
		b->heater = true;
	else if (temp_mc > BASELINE_HEAT_MC)
		b->heater = false;
}
