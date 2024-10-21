// SPDX-License-Identifier: GPL-2.0-or-later

#define DRV_NAME	"leds-landisk-r8c"
#define pr_fmt(fmt)	DRV_NAME ": " fmt

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/leds.h>
#include <linux/reboot.h>

#include <linux/mfd/landisk-r8c.h>

/* default is 100 */
#define R8C_LED_BRNS_MAX	100
#define R8C_LED_BRNS_MIN	0

#define R8C_HDD_LEDS_MAX	2

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
	struct led_classdev cdev;
	struct r8c_leds *l;
	int port;
};

struct r8c_leds {
	struct r8c_mcu *r8c;
	int ledcnt;
	struct r8c_led_data *leds[0];
};

struct r8c_status_mode {
	int id;
	char *name;
};

static int r8c_led_mode_set(struct led_classdev *cdev,
			     enum led_brightness brns, bool blink)
{
	struct r8c_led_data *data = container_of(cdev, struct r8c_led_data, cdev);
	char arg[8];

	scnprintf(arg, sizeof(arg),
		  "%d %d", data->port, blink ? 1 : (brns ? 5 : 0));
	return landisk_r8c_exec_cmd(data->l->r8c, CMD_HDD, arg, NULL, 0);
}

static void r8c_led_brightness_set(struct led_classdev *cdev,
				    enum led_brightness brns)
{
	r8c_led_mode_set(cdev, brns, false);
}

static int r8c_led_blink_set(struct led_classdev *cdev,
			      unsigned long *delay_on,
			      unsigned long *delay_off)
{
	if (*delay_on == 0 && *delay_off == 0)
		return 0;

	return r8c_led_mode_set(cdev, LED_ON, true);
}

static int r8c_leds_register(struct r8c_leds *l, struct device *dev)
{
	struct r8c_led_data *data;
	struct fwnode_handle *child;
	int ret = 0;

	device_for_each_child_node(dev, child) {
		struct led_init_data init_data = { .fwnode = child };
		int port;

		ret = fwnode_property_read_u32(child, "reg", &port);
		if (ret)
			break;

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
		data->cdev.brightness = LED_OFF;
		data->cdev.brightness_set = r8c_led_brightness_set;
		data->cdev.blink_set = r8c_led_blink_set;
		l->leds[l->ledcnt] = data;

		ret = devm_led_classdev_register_ext(dev, &data->cdev, &init_data);
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

/* HDL-XR/XV seems to have "notice" mode (id=4) */
static const struct r8c_status_mode status_mode_list[] = {
	{ .id = 0, .name = "on" },
	{ .id = 1, .name = "blink" },
	{ .id = 2, .name = "err" },
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

	return scnprintf(buf, 10, "%ld %%\n", brightness);
}

static ssize_t leds_brightness_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	struct r8c_leds *l = dev_get_drvdata(dev);
	ssize_t ret;
	/* brightness (%) */
	long brightness;

	ret = kstrtol(buf, 0, &brightness);
	if (ret)
		return ret;

	if (brightness < R8C_LED_BRNS_MIN ||
	    R8C_LED_BRNS_MAX < brightness)
		return -EINVAL;

	ret = landisk_r8c_exec_cmd(l->r8c, CMD_LED, buf, NULL, 0);

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
		led_set_brightness(&l->leds[i]->cdev, LED_OFF);
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
