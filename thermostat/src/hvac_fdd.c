// SPDX-License-Identifier: MIT
/*
 * hvac_fdd: two rule-based detectors fed once per control step.
 * Cooling ineffective: while the compressor stays on, every window compares the temperature with
 * the value at the window's start; a drop smaller than the minimum sets bit0. The window restarts
 * when the compressor turns off. Sensor stuck: a valid reading unchanged for the stuck window sets bit1.
 */
#include "hvac_fdd.h"

#include <errno.h>
#include <stddef.h>

#define MS_PER_S 1000u

void hvac_fdd_config_set_defaults(hvac_fdd_config_t *cfg)
{
	if (cfg == NULL)
		return;
	cfg->enabled = true;
	cfg->ineffective_window_s = FDD_DEFAULT_INEFFECTIVE_WINDOW_S;
	cfg->ineffective_min_drop_mc = FDD_DEFAULT_INEFFECTIVE_MIN_DROP_MC;
	cfg->stuck_window_s = FDD_DEFAULT_STUCK_WINDOW_S;
}

int hvac_fdd_config_validate(const hvac_fdd_config_t *cfg)
{
	if (cfg == NULL)
		return -EINVAL;
	if (cfg->ineffective_window_s < FDD_WINDOW_MIN_S ||
	    cfg->ineffective_window_s > FDD_WINDOW_MAX_S ||
	    cfg->stuck_window_s < FDD_WINDOW_MIN_S || cfg->stuck_window_s > FDD_WINDOW_MAX_S)
		return -EINVAL;
	if (cfg->ineffective_min_drop_mc < FDD_MIN_DROP_MIN_MC ||
	    cfg->ineffective_min_drop_mc > FDD_MIN_DROP_MAX_MC)
		return -EINVAL;
	return 0;
}

int hvac_fdd_init(hvac_fdd_t *fdd, const hvac_fdd_config_t *cfg)
{
	if (fdd == NULL || hvac_fdd_config_validate(cfg) != 0)
		return -EINVAL;
	*fdd = (hvac_fdd_t){0};
	fdd->cfg = *cfg;
	return 0;
}

static uint64_t elapsed_ms(uint64_t now_ms, uint64_t since_ms)
{
	return now_ms >= since_ms ? now_ms - since_ms : 0u;
}

static void check_cooling(hvac_fdd_t *fdd, const hvac_fdd_input_t *in)
{
	uint64_t window_ms = (uint64_t)fdd->cfg.ineffective_window_s * MS_PER_S;

	if (!in->compressor) {
		fdd->window_active = false;	/* reference resets when the compressor turns off */
		return;
	}
	if (!in->valid)
		return;
	if (!fdd->window_active) {
		fdd->window_active = true;
		fdd->window_start_ms = in->now_ms;
		fdd->window_ref_mc = in->temp_mc;
		return;
	}
	if (elapsed_ms(in->now_ms, fdd->window_start_ms) < window_ms)
		return;
	if ((int64_t)fdd->window_ref_mc - (int64_t)in->temp_mc < (int64_t)fdd->cfg.ineffective_min_drop_mc)
		fdd->bits |= FDD_COOLING_INEFFECTIVE;
	fdd->window_start_ms = in->now_ms;	/* next window starts here */
	fdd->window_ref_mc = in->temp_mc;
}

static void check_stuck(hvac_fdd_t *fdd, const hvac_fdd_input_t *in)
{
	if (!in->valid)
		return;
	if (!fdd->has_stuck_ref || in->temp_mc != fdd->stuck_ref_mc) {
		fdd->has_stuck_ref = true;
		fdd->stuck_ref_mc = in->temp_mc;
		fdd->stuck_since_ms = in->now_ms;
		return;
	}
	if (elapsed_ms(in->now_ms, fdd->stuck_since_ms) >= (uint64_t)fdd->cfg.stuck_window_s * MS_PER_S)
		fdd->bits |= FDD_SENSOR_STUCK;
}

uint16_t hvac_fdd_step(hvac_fdd_t *fdd, const hvac_fdd_input_t *in)
{
	if (fdd == NULL || in == NULL || !fdd->cfg.enabled)
		return 0u;
	check_cooling(fdd, in);
	check_stuck(fdd, in);
	return fdd->bits;
}
