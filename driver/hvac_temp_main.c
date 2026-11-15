// SPDX-License-Identifier: GPL-2.0-only
/*
 * hvac_temp: I2C driver for a TMP102-compatible temperature sensor.
 * This file holds the I2C driver (device-id tables, probe, registration) and the IIO channel:
 * in_temp_raw reads the chip under the per-device mutex, in_temp_scale is a constant.
 * Probe checks that the adapter can do SMBus word transfers and that the chip answers.
 * All resources are devm_-managed, so no remove() callback is needed.
 */
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>

#include "hvac_temp.h"
#include "hvac_temp_conv.h"
#include "hvac_temp_regs.h"

/* One raw step is 0.0625 C = 62.5 millidegrees: IIO scale 62 + 500000/1000000. */
#define HVAC_TEMP_SCALE_INT	62
#define HVAC_TEMP_SCALE_MICRO	500000

static const struct iio_chan_spec hvac_temp_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
	},
};

/* Reads the temperature register; returns raw steps or a negative error code. */
static int hvac_temp_read_temp_raw(struct hvac_temp_data *data, int *raw)
{
	int ret;

	mutex_lock(&data->lock);
	ret = i2c_smbus_read_word_swapped(data->client, HVAC_TEMP_REG_TEMP);
	mutex_unlock(&data->lock);
	if (ret < 0)
		return ret;

	*raw = hvac_temp_reg_to_raw((u16)ret);
	return 0;
}

static int hvac_temp_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct hvac_temp_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = hvac_temp_read_temp_raw(data, val);
		return ret ? ret : IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = HVAC_TEMP_SCALE_INT;
		*val2 = HVAC_TEMP_SCALE_MICRO;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static const struct iio_info hvac_temp_info = {
	.read_raw = hvac_temp_read_raw,
	.attrs = &hvac_temp_alarm_group,
};

static int hvac_temp_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct hvac_temp_data *data;
	struct iio_dev *indio_dev;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return dev_err_probe(dev, -EOPNOTSUPP, "adapter lacks SMBus word transfers\n");

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	ret = devm_mutex_init(dev, &data->lock);
	if (ret)
		return ret;

	/* Presence check: a missing chip makes this transfer fail. */
	ret = i2c_smbus_read_word_swapped(client, HVAC_TEMP_REG_CONFIG);
	if (ret < 0)
		return dev_err_probe(dev, ret, "chip not responding\n");

	indio_dev->name = "hvac_temp";
	indio_dev->info = &hvac_temp_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = hvac_temp_channels;
	indio_dev->num_channels = ARRAY_SIZE(hvac_temp_channels);

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "cannot register IIO device\n");

	dev_info(dev, "ready at address 0x%02x\n", client->addr);
	return 0;
}

static const struct i2c_device_id hvac_temp_id[] = {
	{ "hvac_temp" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hvac_temp_id);

static const struct of_device_id hvac_temp_of_match[] = {
	{ .compatible = "hvac,hvac-temp" },
	{ }
};
MODULE_DEVICE_TABLE(of, hvac_temp_of_match);

static struct i2c_driver hvac_temp_driver = {
	.driver = {
		.name = "hvac_temp",
		.of_match_table = hvac_temp_of_match,
	},
	.probe = hvac_temp_probe,
	.id_table = hvac_temp_id,
};
module_i2c_driver(hvac_temp_driver);

MODULE_AUTHOR("Priyanshu");
MODULE_DESCRIPTION("HVAC TMP102-compatible I2C temperature sensor driver (IIO)");
MODULE_LICENSE("GPL");
