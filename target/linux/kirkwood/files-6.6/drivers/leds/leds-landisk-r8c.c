// SPDX-License-Identifier: GPL-2.0-or-later

#define DRV_NAME	"leds-landisk-r8c"
#define pr_fmt(fmt)	DRV_NAME ": " fmt

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/bitfield.h>
#include <linux/leds.h>
#include <linux/led-class-multicolor.h>
#include <linux/reboot.h>

#include <linux/mfd/landisk-r8c.h>

/* default is 100 */
#define R8C_LED_BRNS_MAX	100
#define R8C_LED_BRNS_MIN	0

#define R8C_HDD_LEDS_MAX	4
#define R8C_HDD_COLORS		2
#define R8C_HDD_SUB_BNK(x)	FIELD_GET(0xf0, x)
#define R8C_HDD_SUB_ON(x)	FIELD_GET(0xf, x)
#define R8C_HDD_SUB_CH(bnk,on)	(bnk << 4 | on)

/*
 * HDD LED
 *
 * - Red (indicates errors, HDL-A/HDL2-A)
 *   - 0  : turned off (no error)
 *   - 1-4: blink (any errors)
 *   - 5  : turned on (not connected)
 *
 * - Blue/Red (indicates normal/errors, HDL-XV)
 *   - 0  : Blue, turned on/blink (connected/access)
 *   - 1  : Red, turned on (failure)
 *   - 2  : Red, blink (error)
 *   - 3  : Blue, blink (plugged)
 *   - 4  : Blue, blink (unplugged)
 *   - 5  : turned off (not connected)
 */
enum {
	R8C_HDD_NORMAL = 0,
	R8C_HDD_FAIL,
	R8C_HDD_ERROR,
	R8C_HDD_PLUG,
	R8C_HDD_UNPLUG,
	R8C_HDD_NC,
};

/*
 * commands
 *
 * - LED: brightness setting for all (not for each)
 * - STS: mode setting for status LED
 *   - this LED doesn't have off-state mode and has Red/Green color but
 *     cannot be controlled separately, so registration to Linux is not
 *     good, export only custom sysfs
 * - HDD: mode setting for HDD LED
 */
#define CMD_LED			"led"
#define CMD_STS			"sts"
#define CMD_HDD			"hdd"

struct r8c_leds;

struct r8c_led_data {
	struct led_classdev_mc mccdev;
	struct mc_subled subled_info[R8C_HDD_COLORS];
	struct r8c_leds *l;
	int port;
};

struct r8c_leds {
	struct r8c_mcu *r8c;
	int brns_div;
	int ledcnt;
	struct r8c_led_data *leds[0];
};

struct r8c_status_mode {
	int id;
	char *name;
};

static int r8c_led_mode_set(struct led_classdev *cdev, int mode)
{
	struct led_classdev_mc *mccdev = lcdev_to_mccdev(cdev);
	struct r8c_led_data *data = container_of(mccdev, struct r8c_led_data,
						 mccdev);
	char arg[8];

	scnprintf(arg, sizeof(arg),
		  "%d %d", data->port, mode);
	return landisk_r8c_exec_cmd(data->l->r8c, CMD_HDD, arg, NULL, 0);
}

static int r8c_led_mc_intensity_update(struct led_classdev_mc *mccdev)
{
	struct r8c_led_data *data = container_of(mccdev, struct r8c_led_data,
						 mccdev);
	char buf[8];
	int ret, i;

	ret = landisk_r8c_exec_cmd(data->l->r8c, CMD_HDD, NULL, buf, sizeof(buf));
	if (ret < 0)
		return ret;
	/* example: "<hdd0><hdd1><hdd2><hdd3>" */
	if (ret <= data->port)
		return -EINVAL;
	for (i = 0; i < mccdev->num_colors; i++) {
		struct mc_subled *sub = &mccdev->subled_info[i];
		int cur = buf[data->port] - '0';

		if (R8C_HDD_SUB_ON(sub->channel) == cur ||
		    R8C_HDD_SUB_BNK(sub->channel) == cur)
			sub->intensity = 1;
		else
			sub->intensity = 0;
	}

	return 0;
}

static int r8c_led_mc_common_set(struct led_classdev *cdev,
				 enum led_brightness brns)
{
	struct led_classdev_mc *mccdev = lcdev_to_mccdev(cdev);
	int ret, i, mode = R8C_HDD_NC;

	for (i = 0; i < mccdev->num_colors; i++)
		if (mccdev->subled_info[i].intensity > LED_OFF)
			break;

	/* intensities of all LEDs are 0 */
	if (i >= mccdev->num_colors)
		goto set_mode;

	mode = mccdev->subled_info[i].channel;
	mode = R8C_HDD_SUB_ON(mode);

set_mode:
	ret = r8c_led_mode_set(cdev, mode);
	if (ret < 0)
		return ret;

	return r8c_led_mc_intensity_update(mccdev);
}

static void r8c_led_mc_brightness_set(struct led_classdev *cdev,
				      enum led_brightness brns)
{
	r8c_led_mc_common_set(cdev, brns);
}

static void r8c_led_brightness_set(struct led_classdev *cdev,
				    enum led_brightness brns)
{
	r8c_led_mode_set(cdev, brns ? R8C_HDD_NC : R8C_HDD_NORMAL);
}

static int r8c_led_blink_set(struct led_classdev *cdev,
			      unsigned long *delay_on,
			      unsigned long *delay_off)
{
	return r8c_led_mode_set(cdev,
				(*delay_on == 0 && *delay_off == 0) ?
					R8C_HDD_NORMAL : R8C_HDD_ERROR);
}

static const struct mc_subled r8c_hdd_subleds[] = {
	{ LED_COLOR_ID_BLUE, 0, 0, R8C_HDD_SUB_CH(R8C_HDD_PLUG,  R8C_HDD_NORMAL) },
	{ LED_COLOR_ID_RED,  0, 0, R8C_HDD_SUB_CH(R8C_HDD_ERROR, R8C_HDD_FAIL) },
};

static int r8c_leds_register(struct r8c_leds *l, struct device *dev)
{
	struct r8c_led_data *data;
	struct fwnode_handle *child;
	int ret = 0, i;

	device_for_each_child_node(dev, child) {
		struct led_init_data init_data = { .fwnode = child };
		struct led_classdev *cdev;
		int port, color;

		ret = fwnode_property_read_u32(child, "reg", &port);
		if (ret)
			break;
		ret = fwnode_property_read_u32(child, "color", &color);
		if (ret)
			continue;
		if (color != LED_COLOR_ID_RED && color != LED_COLOR_ID_MULTI) {
			dev_warn(dev,
				 "only Red or Multi color LEDs are supported\n");
			continue;
		}

		if (port > R8C_HDD_LEDS_MAX - 1) {
			dev_warn(dev,
				 "index %d exceeds maximum index %d, skipping...\n",
				 port, R8C_HDD_LEDS_MAX - 1);
			continue;
		}

		data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
		if (!data)
			return -ENOMEM;

		data->l = l;
		data->port = port;

		cdev = &data->mccdev.led_cdev;
		cdev->brightness = LED_OFF;
		cdev->max_brightness = LED_ON;
		l->leds[l->ledcnt] = data;

		if (color == LED_COLOR_ID_MULTI) {
			for (i = 0; i < R8C_HDD_COLORS; i++) {
				data->subled_info[i].color_index
					= r8c_hdd_subleds[i].color_index;
				data->subled_info[i].channel
					= r8c_hdd_subleds[i].channel;
			}
			data->mccdev.subled_info = data->subled_info;
			data->mccdev.num_colors = R8C_HDD_COLORS;
			ret = r8c_led_mc_intensity_update(&data->mccdev);
			if (ret)
				continue;
			cdev->brightness_set = r8c_led_mc_brightness_set;
			ret = devm_led_classdev_multicolor_register_ext(
					dev, &data->mccdev, &init_data);
		} else {
			cdev->brightness_set = r8c_led_brightness_set;
			cdev->blink_set = r8c_led_blink_set;
			ret = devm_led_classdev_register_ext(
					dev, cdev, &init_data);
		}

		if (ret) {
			dev_err(dev, "failed to register HDD LED, index %d\n",
				l->ledcnt);
			return -ENODEV;
		}

		l->ledcnt++;
	}

	fwnode_handle_put(child);
	return ret;
}

static const struct r8c_status_mode status_mode_list[] = {
	{ .id = 0, .name = "on" },
	{ .id = 1, .name = "blink" },
	{ .id = 2, .name = "err" },
	{ .id = 4, .name = "notice" },
	{ .id = 5, .name = "notify" },
	{ .id = 8, .name = "serious_err" },
};

static ssize_t status_mode_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct r8c_leds *l = dev_get_drvdata(dev);
	ssize_t ret, totlen = 0;
	char cmdbuf[4];
	long mode_id;
	int i;

	ret = landisk_r8c_exec_cmd(l->r8c, CMD_STS, NULL, cmdbuf, sizeof(cmdbuf));
	if (ret <= 0)
		return 0;

	ret = kstrtol(cmdbuf, 16, &mode_id);
	if (ret) {
		dev_err(dev, "failed to parse \"%s\" (%d)\n", cmdbuf, ret);
		return 0;
	};

	for (i = 0; i < ARRAY_SIZE(status_mode_list); i++) {
		const struct r8c_status_mode *mode = &status_mode_list[i];

		ret = scnprintf(buf + totlen, 16, "%s%s%s ",
				!!(mode_id == mode->id) ? "[" : "",
				mode->name,
				!!(mode_id == mode->id) ? "]" : "");

		totlen += ret;
	}

	buf[totlen] = '\n';
	totlen++;

	return totlen;
}

static ssize_t status_mode_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	struct r8c_leds *l = dev_get_drvdata(dev);
	ssize_t ret;
	const char *mode_name = NULL;
	int i;

	/* check minimum length */
	if (len < 2)
		return -EINVAL;

	for (i = 0; i < 5; i++) {
		const struct r8c_status_mode *mode = &status_mode_list[i];

		if (!strncmp(buf, mode->name, strlen(mode->name))) {
			mode_name = mode->name;
			break;
		}
	}

	if (!mode_name) {
		dev_dbg(dev, "invalid mode specified\n");
		return -EINVAL;
	}

	ret = landisk_r8c_exec_cmd(l->r8c, CMD_STS, mode_name, NULL, 0);

	return ret ? ret : len;
}

static DEVICE_ATTR_RW(status_mode);

static ssize_t leds_brightness_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct r8c_leds *l = dev_get_drvdata(dev);
	ssize_t ret;
	char cmdbuf[4];
	/* brightness (%) */
	long brightness;

	ret = landisk_r8c_exec_cmd(l->r8c, CMD_LED, NULL, cmdbuf, 4);
	if (ret <= 0 || !strlen(cmdbuf))
		return 0;

	/* R8C returns hex value */
	ret = kstrtol(cmdbuf, 16, &brightness);
	if (ret) {
		dev_err(dev,
			"failed to parse \"%s\" (%d)\n", cmdbuf, ret);
		return 0;
	}

	return scnprintf(buf, 10, "%ld %%\n", brightness * l->brns_div);
}

static ssize_t leds_brightness_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	struct r8c_leds *l = dev_get_drvdata(dev);
	ssize_t ret;
	/* brightness (%) */
	long brightness;
	char __buf[4];

	ret = kstrtol(buf, 0, &brightness);
	if (ret)
		return ret;

	if (brightness < R8C_LED_BRNS_MIN ||
	    R8C_LED_BRNS_MAX < brightness)
		return -EINVAL;

	scnprintf(__buf, 4, "%ld", DIV_ROUND_UP(brightness, l->brns_div));
	ret = landisk_r8c_exec_cmd(l->r8c, CMD_LED, __buf, NULL, 0);

	return ret ? ret : len;
}
/* for all LEDs, not for each */
static DEVICE_ATTR_RW(leds_brightness);

static struct attribute *r8c_leds_attrs[] = {
	&dev_attr_status_mode.attr,
	&dev_attr_leds_brightness.attr,
	NULL,
};

static const struct attribute_group r8c_leds_attr_group = {
	.attrs = r8c_leds_attrs,
};

static int landisk_r8c_get_brightness_div(struct r8c_mcu *r8c)
{
	char buf[8] = "100";
	int ret;

	ret = landisk_r8c_exec_cmd(r8c, CMD_LED, buf, NULL, 0);
	if (ret >= 0)
		ret = landisk_r8c_exec_cmd(r8c, CMD_LED, NULL, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	if (!strncmp(buf, "0a", 2))	/* 0-10 (HDL-XV) */
		return 10;
	else if (!strncmp(buf, "64", 2))/* 0-100 (HDL-A, HDL2-A) */
		return 1;
	else
		return -EINVAL;
}

static int landisk_r8c_leds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct r8c_mcu *r8c;
	struct r8c_leds *l;
	int ret, ledcnt;

	r8c = dev_get_drvdata(dev->parent);
	if (!r8c)
		return -ENODEV;
	/* only HDD LEDs will be registered */
	ledcnt = of_get_child_count(dev->of_node);
	l = devm_kzalloc(dev, struct_size(l, leds, ledcnt),
			 GFP_KERNEL);
	if (!l) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	dev_set_drvdata(dev, l);

	l->r8c = r8c;
	l->brns_div = landisk_r8c_get_brightness_div(r8c);
	if (l->brns_div < 0)
		return l->brns_div;

	ret = sysfs_create_group(&dev->kobj, &r8c_leds_attr_group);
	if (ret)
		return ret;

	if (ledcnt > 0) {
		ret = r8c_leds_register(l, dev);
		if (ret)
			goto err;
	}

	/* set "on" to status LED */
	landisk_r8c_exec_cmd(l->r8c, CMD_STS, "on", NULL, 0);

	return 0;

err:
	sysfs_remove_group(&dev->kobj, &r8c_leds_attr_group);

	return ret;
}

static int landisk_r8c_leds_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &r8c_leds_attr_group);

	return 0;
}

static void landisk_r8c_leds_shutdown(struct platform_device *pdev)
{
	struct r8c_leds *l = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < l->ledcnt; i++)
		led_set_brightness(&l->leds[i]->mccdev.led_cdev, LED_OFF);
}

static const struct of_device_id landisk_r8c_leds_ids[] = {
	{ .compatible = "iodata,landisk-r8c-leds" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, hdl_r8c_leds_ids);

static struct platform_driver landisk_r8c_leds_driver = {
	.probe = landisk_r8c_leds_probe,
	.remove = landisk_r8c_leds_remove,
	.shutdown = landisk_r8c_leds_shutdown,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = landisk_r8c_leds_ids,
	},
};
module_platform_driver(landisk_r8c_leds_driver);

MODULE_AUTHOR("INAGAKI Hiroshi <musashino.open@gmail.com>");
MODULE_DESCRIPTION("R8C LED driver for I-O DATA LAN DISK series");
MODULE_LICENSE("GPL v2");
