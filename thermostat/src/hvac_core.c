// SPDX-License-Identifier: MIT
/*
 * hvac_core: thermostat state machine (IDLE, COOLING, HEATING, FAULT) with hysteresis
 * (deadband), compressor minimum run/off timers and sensor-fault handling.
 * Pure: no allocation, no I/O, no clock. Rules and their order: PROJECT_PLAN.md section 4.4.
 */
#include "hvac_core.h"

#include <errno.h>
#include <stddef.h>

#define MS_PER_S 1000u

void hvac_config_set_defaults(hvac_config_t *cfg)
{
	if (cfg == NULL)
		return;
	cfg->cool_setpoint_mc = HVAC_DEFAULT_COOL_SETPOINT_MC;
	cfg->heat_setpoint_mc = HVAC_DEFAULT_HEAT_SETPOINT_MC;
	cfg->deadband_mc = HVAC_DEFAULT_DEADBAND_MC;
	cfg->min_off_s = HVAC_DEFAULT_MIN_OFF_S;
	cfg->min_run_s = HVAC_DEFAULT_MIN_RUN_S;
	cfg->fault_recover_count = HVAC_DEFAULT_FAULT_RECOVER_COUNT;
	cfg->max_step_mc = HVAC_DEFAULT_MAX_STEP_MC;
}

static bool in_range_i32(int32_t v, int32_t lo, int32_t hi)
{
	return v >= lo && v <= hi;
}

int hvac_config_validate(const hvac_config_t *cfg)
{
	if (cfg == NULL)
		return -EINVAL;
	if (!in_range_i32(cfg->cool_setpoint_mc, HVAC_SETPOINT_MIN_MC, HVAC_SETPOINT_MAX_MC) ||
	    !in_range_i32(cfg->heat_setpoint_mc, HVAC_SETPOINT_MIN_MC, HVAC_SETPOINT_MAX_MC) ||
	    !in_range_i32(cfg->deadband_mc, HVAC_DEADBAND_MIN_MC, HVAC_DEADBAND_MAX_MC) ||
	    !in_range_i32(cfg->max_step_mc, HVAC_MAX_STEP_MIN_MC, HVAC_MAX_STEP_MAX_MC))
		return -EINVAL;
	if (cfg->min_off_s > HVAC_TIMER_MAX_S || cfg->min_run_s > HVAC_TIMER_MAX_S)
		return -EINVAL;
	if (cfg->fault_recover_count < HVAC_FAULT_RECOVER_MIN ||
	    cfg->fault_recover_count > HVAC_FAULT_RECOVER_MAX)
		return -EINVAL;
	/* The heating band must sit entirely below the cooling band (all values are small). */
	if (cfg->heat_setpoint_mc + cfg->deadband_mc >= cfg->cool_setpoint_mc - cfg->deadband_mc)
		return -EINVAL;
	return 0;
}

int hvac_core_init(hvac_ctx_t *ctx, const hvac_config_t *cfg)
{
	if (ctx == NULL || hvac_config_validate(cfg) != 0)
		return -EINVAL;
	*ctx = (hvac_ctx_t){0};
	ctx->cfg = *cfg;
	ctx->state = HVAC_STATE_IDLE;
	return 0;
}

/* Time since an earlier timestamp; 0 if the clock appears to have gone backwards. */
static uint64_t elapsed_ms(uint64_t now_ms, uint64_t since_ms)
{
	return now_ms >= since_ms ? now_ms - since_ms : 0u;
}

/* Rule 1: in range, and no implausible jump from the last valid reading. */
static bool reading_is_valid(const hvac_ctx_t *ctx, const hvac_input_t *in)
{
	int64_t step_mc = 0;

	if (!in->read_ok || !in_range_i32(in->temp_mc, HVAC_TEMP_MIN_MC, HVAC_TEMP_MAX_MC))
		return false;
	if (!ctx->has_last_valid)
		return true;
	step_mc = (int64_t)in->temp_mc - (int64_t)ctx->last_valid_mc;
	if (step_mc < 0)
		step_mc = -step_mc;
	return step_mc <= (int64_t)ctx->cfg.max_step_mc;
}

static bool compressor_may_start(const hvac_ctx_t *ctx, uint64_t now_ms)
{
	if (!ctx->compressor_has_run)
		return true;
	return elapsed_ms(now_ms, ctx->compressor_off_since_ms) >=
	       (uint64_t)ctx->cfg.min_off_s * MS_PER_S;
}

static void step_fault(hvac_ctx_t *ctx, bool valid)
{
	if (!valid) {
		ctx->good_count = 0;
		return;
	}
	ctx->good_count++;
	if (ctx->good_count >= ctx->cfg.fault_recover_count)
		ctx->state = HVAC_STATE_IDLE;
}

static void step_idle(hvac_ctx_t *ctx, int32_t temp_mc, uint64_t now_ms)
{
	const hvac_config_t *c = &ctx->cfg;

	if (temp_mc >= c->cool_setpoint_mc + c->deadband_mc && compressor_may_start(ctx, now_ms)) {
		ctx->state = HVAC_STATE_COOLING;
		ctx->compressor_on_since_ms = now_ms;
		ctx->compressor_starts++;
		ctx->compressor_has_run = true;
	} else if (temp_mc <= c->heat_setpoint_mc - c->deadband_mc) {
		ctx->state = HVAC_STATE_HEATING;
	}
}

static void step_cooling(hvac_ctx_t *ctx, int32_t temp_mc, uint64_t now_ms)
{
	const hvac_config_t *c = &ctx->cfg;

	if (temp_mc <= c->cool_setpoint_mc - c->deadband_mc &&
	    elapsed_ms(now_ms, ctx->compressor_on_since_ms) >= (uint64_t)c->min_run_s * MS_PER_S) {
		ctx->state = HVAC_STATE_IDLE;
		ctx->compressor_off_since_ms = now_ms;
	}
}

static void step_heating(hvac_ctx_t *ctx, int32_t temp_mc)
{
	if (temp_mc >= ctx->cfg.heat_setpoint_mc + ctx->cfg.deadband_mc)
		ctx->state = HVAC_STATE_IDLE;
}

static void set_outputs(hvac_state_t state, hvac_outputs_t *out)
{
	out->compressor = (state == HVAC_STATE_COOLING);
	out->heater = (state == HVAC_STATE_HEATING);
	out->fan = out->compressor || out->heater;
	out->alarm = (state == HVAC_STATE_FAULT);
}

hvac_state_t hvac_core_step(hvac_ctx_t *ctx, const hvac_input_t *in, hvac_outputs_t *out)
{
	bool valid = false;

	if (ctx == NULL || in == NULL || out == NULL)
		return HVAC_STATE_FAULT;

	valid = reading_is_valid(ctx, in);
	if (valid) {
		ctx->last_valid_mc = in->temp_mc;
		ctx->has_last_valid = true;
	}

	if (!valid && ctx->state != HVAC_STATE_FAULT) {
		if (ctx->state == HVAC_STATE_COOLING)
			ctx->compressor_off_since_ms = in->now_ms;
		ctx->state = HVAC_STATE_FAULT;
		ctx->good_count = 0;
	} else if (ctx->state == HVAC_STATE_FAULT) {
		step_fault(ctx, valid);
	} else if (ctx->state == HVAC_STATE_IDLE) {
		step_idle(ctx, in->temp_mc, in->now_ms);
	} else if (ctx->state == HVAC_STATE_COOLING) {
		step_cooling(ctx, in->temp_mc, in->now_ms);
	} else {
		step_heating(ctx, in->temp_mc);
	}

	set_outputs(ctx->state, out);
	return ctx->state;
}

int hvac_core_set_setpoints(hvac_ctx_t *ctx, int32_t cool_mc, int32_t heat_mc)
{
	hvac_config_t candidate = {0};

	if (ctx == NULL)
		return -EINVAL;
	candidate = ctx->cfg;
	candidate.cool_setpoint_mc = cool_mc;
	candidate.heat_setpoint_mc = heat_mc;
	if (hvac_config_validate(&candidate) != 0)
		return -EINVAL;
	ctx->cfg = candidate;
	return 0;
}

uint32_t hvac_core_compressor_starts(const hvac_ctx_t *ctx)
{
	return ctx == NULL ? 0u : ctx->compressor_starts;
}

const char *hvac_state_name(hvac_state_t state)
{
	switch (state) {
	case HVAC_STATE_IDLE:
		return "IDLE";
	case HVAC_STATE_COOLING:
		return "COOLING";
	case HVAC_STATE_HEATING:
		return "HEATING";
	case HVAC_STATE_FAULT:
		return "FAULT";
	default:
		return "UNKNOWN";
	}
}
