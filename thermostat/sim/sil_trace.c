// SPDX-License-Identifier: MIT
/*
 * sil_trace: writes the equivalence-test trace (Ext 4). Shared by the host simulator and the
 * FreeRTOS logger task, so both produce the same bytes for the same run.
 */
#include "sil_trace.h"

#include <errno.h>

int sil_trace_header(FILE *f)
{
	return fprintf(f, "t_s,temp_meas_mc,state,compressor,heater\n") < 0 ? -EIO : 0;
}

int sil_trace_row(FILE *f, uint32_t t_s, int32_t temp_meas_mc, hvac_state_t state,
		  const hvac_outputs_t *out)
{
	int n = fprintf(f, "%u,%d,%s,%d,%d\n", t_s, temp_meas_mc, hvac_state_name(state),
			out->compressor ? 1 : 0, out->heater ? 1 : 0);

	return n < 0 ? -EIO : 0;
}
