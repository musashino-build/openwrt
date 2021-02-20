// SPDX-License-Identifier: GPL-2.0-only
/*
 * MCU LED driver for Sony NCP-HG100
 *
 * This driver provides access to a LEDs, behind the MCU.
 * Currently, only basic LED control is supported.
 *
 * Copyright (C) 2021 INAGAKI Hiroshi <musashino.open@gmail.com>
 *
 * This file was based on the leds-pwm driver in the Linux Kernel.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <dt-bindings/leds/common.h>

#include <mfd/sony-mcu.h>

struct led_sony_mcu_data {
	struct led_classdev	cdev;
	struct sony_mcu		*mcu;
	unsigned int		color_id;
};

struct led_sony_mcu_priv {
	int				num_leds;
	struct led_sony_mcu_data	leds[0];
};

struct led_sony_mcu {
	const char	*name;
	unsigned int	color_id;
	const char	*default_trigger;
};

struct led_sony_mcu_platform_data {
	int			num_leds;
	struct led_sony_mcu	*leds;
};

static const u8 color_reg_table[] = {
	MCU_REG_INVALID,
	MCU_REG_LED_RED,
	MCU_REG_LED_GREEN,
	MCU_REG_LED_BLUE,
};

static int
led_sony_mcu_brightness_set(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	struct led_sony_mcu_data *led_data =
		container_of(led_cdev, struct led_sony_mcu_data, cdev);

	dev_dbg(led_cdev->dev, "color->%u, value->%u\n",
		led_data->color_id, value);

	return led_data->mcu->set_brightness(
			led_data->mcu,
			color_reg_table[led_data->color_id],
			value);
}

static int
led_sony_mcu_add(struct device *dev, struct led_sony_mcu_priv *priv,
		 struct led_sony_mcu *led, struct fwnode_handle *fwnode)
{
	struct led_sony_mcu_data *led_data = &priv->leds[priv->num_leds];
	int ret = 0;

	led_data->mcu 			= dev_get_drvdata(dev->parent);
	led_data->cdev.name		= led->name;
	led_data->color_id		= led->color_id;
	led_data->cdev.default_trigger	= led->default_trigger;
	led_data->cdev.brightness	= LED_OFF;
	led_data->cdev.max_brightness	= LED_FULL;
	led_data->cdev.brightness_set_blocking
					= led_sony_mcu_brightness_set;

	ret = devm_led_classdev_register(dev, &led_data->cdev);
	if (ret == 0) {
		dev_dbg(dev, "LED registered: %s (color ID: %u)\n",
			 led->name, led->color_id);
		priv->num_leds++;
	} else {
		dev_err(dev, "failed to register MCU LED for %s: %d\n",
			led->name, ret);
	}

	return ret;
}

static int
led_sony_mcu_create_fwnode(struct device *dev,
			   struct led_sony_mcu_priv *priv)
{
	struct fwnode_handle *fwnode;
	struct led_sony_mcu led;
	int ret = 0;

	memset(&led, 0, sizeof(led));

	device_for_each_child_node(dev, fwnode) {
		ret = fwnode_property_read_string(fwnode, "label",
						  &led.name);
		if (ret && is_of_node(fwnode))
			led.name = to_of_node(fwnode)->name;

		if (!led.name) {
			fwnode_handle_put(fwnode);
			return -EINVAL;
		}

		ret = fwnode_property_read_u32(fwnode, "color",
					       &led.color_id);
		if (ret) {
			dev_warn(dev,
				 "no or invalid LED color specified: %s\n",
				 led.name);
			continue;
		}

		switch (led.color_id) {
			case LED_COLOR_ID_RED:
			case LED_COLOR_ID_GREEN:
			case LED_COLOR_ID_BLUE:
				break;
			default:
				dev_warn(dev, "invalid LED color ID specified: %u\n",
					 led.color_id);
				continue;
				break;
		}

		fwnode_property_read_string(fwnode, "linux,default-trigger",
					    &led.default_trigger);

		ret = led_sony_mcu_add(dev, priv, &led, fwnode);
		if (ret) {
			fwnode_handle_put(fwnode);
			break;
		}
	}

	return ret;
}

static int
led_sony_mcu_probe(struct platform_device *pdev)
{
	struct led_sony_mcu_platform_data *pdata =
					   dev_get_platdata(&pdev->dev);
	struct led_sony_mcu_priv *priv;
	int i, count, ret = 0;

	dev_info(&pdev->dev, "MCU LED driver for Sony NCP-HG100\n");

	if (pdata)
		count = pdata->num_leds;
	else
		count = device_get_child_node_count(&pdev->dev);

	priv = devm_kzalloc(&pdev->dev, struct_size(priv, leds, count),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (pdata) {
		for (i = 0; i < count; i++) {
			ret = led_sony_mcu_add(&pdev->dev, priv,
					       &pdata->leds[i], NULL);
			if (ret)
				break;
		}
	} else {
		ret = led_sony_mcu_create_fwnode(&pdev->dev, priv);
	}

	if (ret)
		return ret;

	platform_set_drvdata(pdev, priv);

	return 0;
}

static int
led_sony_mcu_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id led_sony_mcu_dt_match[] = {
	{ .compatible = "sony,mcu-leds", },
	{ },
};
MODULE_DEVICE_TABLE(of, sony_mcu_dt_match);

static struct platform_driver led_sony_mcu_driver = {
	.probe			= led_sony_mcu_probe,
	.remove			= led_sony_mcu_remove,
	.driver	= {
		.name		= "leds-sony-mcu",
		.of_match_table	= of_match_ptr(led_sony_mcu_dt_match),
	},
};

module_platform_driver(led_sony_mcu_driver);

MODULE_DESCRIPTION("MCU LED driver for Sony NCP-HG100");
MODULE_AUTHOR("INAGAKI Hiroshi <musashino.open@gmail.com>");
MODULE_LICENSE("GPL v2");
