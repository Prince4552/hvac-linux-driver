/* SPDX-License-Identifier: MIT */
/*
 * sil_metrics: control-quality measurements for one simulated day (comfort, time to comfort,
 * compressor starts, short cycles, off gaps, duty, overshoot). Fed once per simulated second.
 * Integer arithmetic only; percentages are kept as hundredths and printed with two decimals.
 */
#ifndef SIL_METRICS_H
#define SIL_METRICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SIL_COMFORT_LOW_MC 18500	/* heating start 19 C minus 0.5 C tolerance */
#define SIL_COMFORT_HIGH_MC 27500	/* cooling start 27 C plus 0.5 C tolerance */

typedef struct {
	uint32_t min_off_s;		/* starts closer than this to the last stop are short cycles */
	uint32_t seconds;		/* simulated seconds so far */
	bool entered_band;
	uint32_t entry_s;		/* first second inside the comfort band */
	uint32_t in_band_s;		/* seconds inside the band, counted from entry */
	int32_t max_overshoot_mc;	/* largest excursion outside the band after entry */
	uint32_t compressor_on_s;
	uint32_t starts;
	bool prev_compressor;
	bool has_stopped;
	uint32_t last_off_s;
	uint32_t short_cycles;
	bool has_gap;
	uint32_t min_gap_s;
} sil_metrics_t;

void sil_metrics_init(sil_metrics_t *m, uint32_t min_off_s);

/** Records second t_s: true room temperature and compressor output during that second. */
void sil_metrics_update(sil_metrics_t *m, uint32_t t_s, int32_t true_mc, bool compressor);

/** Comfort share in hundredths of a percent (9876 = 98.76 %); 0 if the band was never entered. */
uint32_t sil_metrics_comfort_hundredths(const sil_metrics_t *m);

/** Compressor starts per hour in hundredths. */
uint32_t sil_metrics_starts_per_hour_hundredths(const sil_metrics_t *m);

/** Writes the metrics as one JSON object. Returns 0, or -ENOSPC if buf is too small. */
int sil_metrics_format_json(const sil_metrics_t *m, const char *scenario, const char *controller,
			    uint32_t seed, char *buf, size_t len);

#endif /* SIL_METRICS_H */
