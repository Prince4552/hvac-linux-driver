/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Private per-device state of the hvac_temp driver and prototypes shared between its files.
 * One instance lives inside each IIO device (iio_priv()); there is no global mutable state.
 */
#ifndef HVAC_TEMP_H
#define HVAC_TEMP_H

#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>

/**
 * struct hvac_temp_data - per-device state
 * @client: the I2C device this driver instance talks to
 * @lock:   serializes complete I2C conversations with the chip (never held across user space)
 */
struct hvac_temp_data {
	struct i2c_client *client;
	struct mutex lock;
};

/* Alarm-limit sysfs attributes, defined in hvac_temp_alarm.c. */
extern const struct attribute_group hvac_temp_alarm_group;

#endif /* HVAC_TEMP_H */
