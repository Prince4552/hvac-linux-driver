/* SPDX-License-Identifier: MIT */
/*
 * sil_trace: the per-second trace used by the FreeRTOS equivalence test (Ext 4, decision D-030).
 * The host simulator ("sil trace") and the FreeRTOS port write their traces with these same two
 * functions, so equal runs give byte-identical files. The run's settings are fixed here, once.
 */
#ifndef SIL_TRACE_H
#define SIL_TRACE_H

#include <stdint.h>
#include <stdio.h>

#include "hvac_core.h"

#define SIL_TRACE_SCENARIO "hot_day"
#define SIL_TRACE_SECONDS 7200u		/* 2 simulated hours */
#define SIL_TRACE_SEED 1u

/** Writes the CSV header line. Returns 0 or -EIO. */
int sil_trace_header(FILE *f);

/** Writes one row "t_s,temp_meas_mc,state,compressor,heater". Returns 0 or -EIO. */
int sil_trace_row(FILE *f, uint32_t t_s, int32_t temp_meas_mc, hvac_state_t state,
		  const hvac_outputs_t *out);

#endif /* SIL_TRACE_H */
