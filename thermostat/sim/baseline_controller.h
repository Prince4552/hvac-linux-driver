/* SPDX-License-Identifier: MIT */
/*
 * baseline_controller: a deliberately naive thermostat used only in the simulator, as the
 * "before" picture: cooling on above 26 C and off below 26 C, heating mirrored at 20 C,
 * no deadband and no timers. Comparing it with hvac_core shows what hysteresis and timers buy.
 */
#ifndef BASELINE_CONTROLLER_H
#define BASELINE_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	bool compressor;
	bool heater;
} baseline_t;

/** Updates the outputs from one reading (millidegrees). */
void baseline_step(baseline_t *b, int32_t temp_mc);

#endif /* BASELINE_CONTROLLER_H */
