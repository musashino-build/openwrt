// SPDX-License-Identifier: GPL-2.0-only
/*
 * MCU driver for the Sony NCP-HG100
 *
 * Copyright (C) 2021 INAGAKI Hiroshi <musashino.open@gmail.com>
 *
 * This file was based on the rb4xx-cpld driver in OpenWrt and mcu-i2c
 * driver provided by Sony.
 */

#include <linux/i2c.h>

/* registers */
#define MCU_REG_LED_PTN	0x00
#define MCU_REG_READ_FW_VER	0x09
#define MCU_REG_LED_RED	0x0a
#define MCU_REG_LED_GREEN	0x0b
#define MCU_REG_LED_BLUE	0x0c
#define MCU_REG_INVALID	0xff

/* patterns on REG_LED */
#define PTN_LED_ON_B		0xa0

struct sony_mcu {
	struct i2c_client *client;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *boot_gpio;

	int (*set_brightness)(struct sony_mcu *self, u8 color_reg, u8 val);
	int (*set_pattern)(struct sony_mcu *self, u8 val);
};
