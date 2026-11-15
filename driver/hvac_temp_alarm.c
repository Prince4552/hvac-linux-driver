// SPDX-License-Identifier: GPL-2.0-only
/*
 * hvac_temp alarm limits: the sysfs files alarm_low_millicelsius and alarm_high_millicelsius
 * (PROJECT_PLAN.md section 4.2). Values are millidegrees; writes are parsed with kstrtoint(),
 * range-checked (-40000..125000) and stored rounded to the chip's 0.0625 C step.
 * The driver does not require low < high, because the chip does not either.
 */
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>

#include "hvac_temp.h"
#include "hvac_temp_conv.h"
#include "hvac_temp_regs.h"

/* The register behind each attribute is stored in its iio_dev_attr "address" field. */
static u8 hvac_temp_attr_reg(struct device_attribute *attr)
{
	return (u8)to_iio_dev_attr(attr)->address;
}

static ssize_t hvac_temp_alarm_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct hvac_temp_data *data = iio_priv(dev_to_iio_dev(dev));
	int ret;

	mutex_lock(&data->lock);
	ret = i2c_smbus_read_word_swapped(data->client, hvac_temp_attr_reg(attr));
	mutex_unlock(&data->lock);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", hvac_temp_raw_to_mc(hvac_temp_reg_to_raw((u16)ret)));
}

static ssize_t hvac_temp_alarm_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t len)
{
	struct hvac_temp_data *data = iio_priv(dev_to_iio_dev(dev));
	u16 reg_val;
	int mc;
	int ret;

	/* Any unparsable input (including overflow, which kstrtoint reports as -ERANGE) is -EINVAL. */
	if (kstrtoint(buf, 10, &mc))
		return -EINVAL;
	if (mc < HVAC_TEMP_MIN_MC || mc > HVAC_TEMP_MAX_MC)
		return -EINVAL;

	reg_val = hvac_temp_raw_to_reg(hvac_temp_mc_to_raw(mc));
	mutex_lock(&data->lock);
	ret = i2c_smbus_write_word_swapped(data->client, hvac_temp_attr_reg(attr), reg_val);
	mutex_unlock(&data->lock);
	if (ret < 0)
		return ret;

	return len;
}

static IIO_DEVICE_ATTR(alarm_low_millicelsius, 0644, hvac_temp_alarm_show,
		       hvac_temp_alarm_store, HVAC_TEMP_REG_T_LOW);
static IIO_DEVICE_ATTR(alarm_high_millicelsius, 0644, hvac_temp_alarm_show,
		       hvac_temp_alarm_store, HVAC_TEMP_REG_T_HIGH);

static struct attribute *hvac_temp_alarm_attrs[] = {
	&iio_dev_attr_alarm_low_millicelsius.dev_attr.attr,
	&iio_dev_attr_alarm_high_millicelsius.dev_attr.attr,
	NULL
};

/**
 * hvac_temp_alarm_group - sysfs group with the two alarm-limit attributes.
 *
 * Attached to the IIO device through &struct iio_info.attrs in hvac_temp_main.c.
 */
const struct attribute_group hvac_temp_alarm_group = {
	.attrs = hvac_temp_alarm_attrs,
};
