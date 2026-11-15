// SPDX-License-Identifier: GPL-2.0-only
/*
 * hvac_temp: I2C driver for a TMP102-compatible temperature sensor.
 * This file holds the I2C driver: device-id tables, probe, and module registration.
 * Probe checks that the adapter can do SMBus word transfers and that the chip answers.
 * All resources are devm_-managed, so no remove() callback is needed.
 */
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>

#include "hvac_temp.h"
#include "hvac_temp_regs.h"

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
