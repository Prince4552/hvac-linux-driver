// SPDX-License-Identifier: MIT
/*
 * sil_metrics: accumulates control-quality metrics for one simulated run and formats them as
 * JSON. Definitions: PROJECT_PLAN.md Phase 5 task 5 and docs/DECISIONS.md (comfort and overshoot).
 */
#include "sil_metrics.h"

#include <errno.h>
#include <stdio.h>

#define HUNDRED 100u
#define SECONDS_PER_HOUR 3600u

void sil_metrics_init(sil_metrics_t *m, uint32_t min_off_s)
{
	*m = (sil_metrics_t){0};
	m->min_off_s = min_off_s;
}

static int32_t outside_band_mc(int32_t true_mc)
{
	if (true_mc < SIL_COMFORT_LOW_MC)
		return SIL_COMFORT_LOW_MC - true_mc;
	if (true_mc > SIL_COMFORT_HIGH_MC)
		return true_mc - SIL_COMFORT_HIGH_MC;
	return 0;
}

static void track_compressor(sil_metrics_t *m, uint32_t t_s, bool compressor)
{
	if (compressor && !m->prev_compressor) {
		m->starts++;
		if (m->has_stopped) {
			uint32_t gap_s = t_s - m->last_off_s;

			if (gap_s < m->min_off_s)
				m->short_cycles++;
			if (!m->has_gap || gap_s < m->min_gap_s)
				m->min_gap_s = gap_s;
			m->has_gap = true;
		}
	} else if (!compressor && m->prev_compressor) {
		m->last_off_s = t_s;
		m->has_stopped = true;
	}
	if (compressor)
		m->compressor_on_s++;
	m->prev_compressor = compressor;
}

void sil_metrics_update(sil_metrics_t *m, uint32_t t_s, int32_t true_mc, bool compressor)
{
	int32_t outside = outside_band_mc(true_mc);

	m->seconds++;
	if (!m->entered_band && outside == 0) {
		m->entered_band = true;
		m->entry_s = t_s;
	}
	if (m->entered_band) {
		if (outside == 0)
			m->in_band_s++;
		if (outside > m->max_overshoot_mc)
			m->max_overshoot_mc = outside;
	}
	track_compressor(m, t_s, compressor);
}

uint32_t sil_metrics_comfort_hundredths(const sil_metrics_t *m)
{
	uint64_t counted = m->entered_band ? (uint64_t)m->seconds - m->entry_s : 0u;

	if (counted == 0u)
		return 0u;
	return (uint32_t)((uint64_t)m->in_band_s * HUNDRED * HUNDRED / counted);
}

uint32_t sil_metrics_starts_per_hour_hundredths(const sil_metrics_t *m)
{
	if (m->seconds == 0u)
		return 0u;
	return (uint32_t)((uint64_t)m->starts * HUNDRED * SECONDS_PER_HOUR / m->seconds);
}

int sil_metrics_format_json(const sil_metrics_t *m, const char *scenario, const char *controller,
			    uint32_t seed, char *buf, size_t len)
{
	uint32_t comfort = sil_metrics_comfort_hundredths(m);
	uint32_t starts_ph = sil_metrics_starts_per_hour_hundredths(m);
	uint32_t duty = m->seconds == 0u ? 0u :
		(uint32_t)((uint64_t)m->compressor_on_s * HUNDRED * HUNDRED / m->seconds);
	char ttc[16] = "null";
	char gap[16] = "null";
	int n = 0;

	if (m->entered_band)
		(void)snprintf(ttc, sizeof(ttc), "%u.%u", m->entry_s / 60u, (m->entry_s % 60u) / 6u);
	if (m->has_gap)
		(void)snprintf(gap, sizeof(gap), "%u", m->min_gap_s);
	n = snprintf(buf, len,
		     "{\"scenario\": \"%s\", \"controller\": \"%s\", \"seed\": %u, "
		     "\"comfort_pct\": %u.%02u, \"time_to_comfort_min\": %s, "
		     "\"compressor_starts\": %u, \"compressor_starts_per_hour\": %u.%02u, "
		     "\"short_cycle_violations\": %u, \"min_off_gap_s\": %s, "
		     "\"compressor_duty_pct\": %u.%02u, \"max_overshoot_mc\": %d}",
		     scenario, controller, seed, comfort / HUNDRED, comfort % HUNDRED, ttc,
		     m->starts, starts_ph / HUNDRED, starts_ph % HUNDRED, m->short_cycles, gap,
		     duty / HUNDRED, duty % HUNDRED, m->max_overshoot_mc);
	if (n < 0 || (size_t)n >= len)
		return -ENOSPC;
	return 0;
}
