// SPDX-License-Identifier: GPL-2.0-only
/*
 * MCU driver for Sony NCP-HG100
 *
 * This driver provides access to a MCU that interfaces between the SoC
 * I2C bus and other devices. Behind the MCU there is a RGB LED and fan,
 * temperature sensors.
 * Currently, only RGB LED is supported.
 *
 * Copyright (C) 2021 INAGAKI Hiroshi <musashino.open@gmail.com>
 *
 * This file was based on the rb4xx-cpld driver in OpenWrt and mcu-i2c
 * driver provided by Sony.
*/
#include <linux/mfd/core.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>

#include <mfd/sony-mcu.h>

int
mcu_rd(struct sony_mcu *mcu, u8 reg, u8 *val)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(mcu->client, reg);
	if (ret < 0)
		return ret;

	*val = ret;

	return 0;
}

int
mcu_wr(struct sony_mcu *mcu, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(mcu->client, reg, val);
}

static int
led_brightness(struct sony_mcu *mcu, u8 color_reg, u8 val)
{
	return mcu_wr(mcu, color_reg, val);
}

static int
led_pattern(struct sony_mcu *mcu, u8 val)
{
	return mcu_wr(mcu, MCU_REG_LED_PTN, val);
}

static const struct mfd_cell sony_mcu_cells[] = {
	{
		.name = "leds-sony-mcu",
		.of_compatible = "sony,mcu-leds",
	},
};

static int
sony_mcu_probe(struct i2c_client *client,
	       const struct i2c_device_id *ids)
{
	struct device *dev = &client->dev;
	struct sony_mcu *mcu;
	u8 fw_ver = 0;
	int ret;

	dev_info(dev,
		 "MCU (I2C) driver for Sony NCP-HG100\n");

	mcu = devm_kzalloc(dev, sizeof(*mcu), GFP_KERNEL);
	if (!mcu)
		return -ENOMEM;

	dev_set_drvdata(dev, mcu);

	mcu->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(mcu->reset_gpio)) {
		dev_err(dev, "missing gpio reset: %ld\n",
			PTR_ERR(mcu->reset_gpio));
		return -ENOENT;
	}

	mcu->boot_gpio = devm_gpiod_get(dev, "boot", GPIOD_OUT_LOW);
	if (IS_ERR(mcu->boot_gpio)) {
		dev_err(dev, "missing gpio boot: %ld\n",
			PTR_ERR(mcu->boot_gpio));
		return -ENOENT;
	}

	gpiod_set_consumer_name(mcu->reset_gpio, "sony:mcu:reset");
	gpiod_set_consumer_name(mcu->boot_gpio, "sony:mcu:boot");

	/* reset MCU */
	gpiod_set_value_cansleep(mcu->boot_gpio, 0);
	msleep(10);
	gpiod_set_value_cansleep(mcu->reset_gpio, 1);
	msleep(10);
	gpiod_set_value_cansleep(mcu->reset_gpio, 0);
	msleep(10);
	gpiod_set_value_cansleep(mcu->reset_gpio, 1);
	msleep(100);

	mcu->client		= client;
	i2c_set_clientdata(client, mcu);

	mcu->set_brightness	= led_brightness;
	mcu->set_pattern	= led_pattern;

	/* set "ON" pattern (with turning on blue) */
	ret = led_pattern(mcu, PTN_LED_ON_B);
	if (ret)
		dev_err(dev, "failed to initialize RGB LED: %d\n", ret);

	msleep(1);
	/* turn off blue */
	ret = led_brightness(mcu, MCU_REG_LED_BLUE, 0);
	if (ret)
		dev_warn(dev, "failed to initialize RGB brightness: %d\n",
			 ret);

	/* show fw version */
	ret = mcu_rd(mcu, MCU_REG_READ_FW_VER, &fw_ver);
	if (!ret)
		dev_info(dev, "MCU FW ver: %d\n", fw_ver);

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE,
				    sony_mcu_cells,
				    ARRAY_SIZE(sony_mcu_cells),
				    NULL, 0, NULL);
}

static int
sony_mcu_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id mcu_id[] = {
	{ "sony-mcu", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, mcu_id);

static const struct of_device_id sony_mcu_dt_match[] = {
	{ .compatible = "sony,mcu", },
	{ },
};
MODULE_DEVICE_TABLE(of, sony_mcu_dt_match);

static struct i2c_driver sony_mcu_driver = {
	.probe			= sony_mcu_probe,
	.remove			= sony_mcu_remove,
	.id_table		= mcu_id,
	.driver = {
		.name		= "sony-mcu",
		.of_match_table = of_match_ptr(sony_mcu_dt_match),
	},
};

module_i2c_driver(sony_mcu_driver);

MODULE_DESCRIPTION("MCU driver for Sony NCP-HG100");
MODULE_AUTHOR("INAGAKI Hiroshi <musashino.open@gmail.com>");
MODULE_LICENSE("GPL v2");
