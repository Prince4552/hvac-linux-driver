/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Register map of the TMP102-compatible temperature chip (PROJECT_PLAN.md section 4.1).
 * Each register is 16 bits, sent most significant byte first. Shared by the driver and its tests.
 */
#ifndef HVAC_TEMP_REGS_H
#define HVAC_TEMP_REGS_H

#define HVAC_TEMP_REG_TEMP	0x00	/* latest temperature (read only) */
#define HVAC_TEMP_REG_CONFIG	0x01	/* configuration (read in probe as a presence check) */
#define HVAC_TEMP_REG_T_LOW	0x02	/* lower alarm limit */
#define HVAC_TEMP_REG_T_HIGH	0x03	/* upper alarm limit */

/* Temperature registers hold a 12-bit two's complement value in bits 15..4. */
#define HVAC_TEMP_RAW_SHIFT	4
#define HVAC_TEMP_RAW_BITS	12

#endif /* HVAC_TEMP_REGS_H */
