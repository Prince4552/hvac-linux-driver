/* SPDX-License-Identifier: MIT */
/*
 * sil_fdd: fault-detection experiments for Ext 2. Runs the product controller with hvac_fdd
 * against injected faults (detection matrix) and against healthy days with several seeds
 * (false-alarm matrix), and writes OUT_DIR/fdd_metrics.json.
 */
#ifndef SIL_FDD_H
#define SIL_FDD_H

/** Runs both matrices. Returns 0 on success (results may still miss targets) or -errno. */
int sil_fdd_run_all(const char *out_dir);

#endif /* SIL_FDD_H */
