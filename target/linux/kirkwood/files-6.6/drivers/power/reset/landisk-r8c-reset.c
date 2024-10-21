// SPDX-License-Identifier: GPL-2.0-or-later

#define DRV_NAME	"landisk-r8c-reset"
#define pr_fmt(fmt)	DRV_NAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/reboot.h>

#include <linux/mfd/landisk-r8c.h>

#define CMD_RESET	"reset"
#define CMD_POFF	"poweroff"
#define CMD_WOL		"wol_flag"
#define CMD_FIRST	"first"
#define CMD_PWR_AC	"intrp"
#define CMD_DUMMY	"sts"

static int landisk_r8c_handler_common(struct sys_off_data *data, char *cmd)
{
	struct r8c_mcu *r8c = data->cb_data;
	int ret;

	/* work-around for "reset" command failure */
	if (!strncmp(cmd, CMD_RESET, 5)) {
		char buf[4];
		ret = landisk_r8c_exec_cmd(r8c, CMD_DUMMY, NULL, buf, sizeof(buf));
		if (ret <= 0) {
			pr_err("failed to execute dummy command %d\n",
			       ret);
			return NOTIFY_BAD;
		}
	}

	ret = landisk_r8c_exec_cmd(r8c, cmd, NULL, NULL, 0);
	if (ret < 0)
		pr_err("failed to execute %s %d\n", cmd, ret);

	msleep(1000);
	return ret < 0 ? NOTIFY_BAD : NOTIFY_STOP;
}

static int landisk_r8c_reset_handler(struct sys_off_data *data)
{
	return landisk_r8c_handler_common(data, CMD_RESET);
}

static int landisk_r8c_poweroff_handler(struct sys_off_data *data)
{
	return landisk_r8c_handler_common(data, CMD_POFF);
}

static ssize_t landisk_r8c_flag_get(struct device *dev, char *buf, const char *cmd)
{
	struct r8c_mcu *r8c = dev_get_drvdata(dev);
	ssize_t ret;
	char cmdbuf[4];

	ret = landisk_r8c_exec_cmd(r8c, cmd, NULL, cmdbuf, sizeof(cmdbuf));
	if (ret != 1)
		return ret < 0 ? ret : -EINVAL;

	return scnprintf(buf, sizeof(cmdbuf), "%s\n", cmdbuf);
}

static ssize_t landisk_r8c_flag_set(struct device *dev,
				    const char *buf, size_t len, const char *cmd,
				    const char *arg_en, const char *arg_dis)
{
	struct r8c_mcu *r8c = dev_get_drvdata(dev);
	ssize_t ret;
	char cmdbuf[4];
	u32 enable;

	ret = kstrtou32(buf, 0, &enable);
	if (ret)
		return ret;

	ret = landisk_r8c_exec_cmd(r8c, cmd, enable ? arg_en : arg_dis,
				   cmdbuf, sizeof(cmdbuf));
	/*
	 * R8C returns a result of execution
	 * length of response is not 1 (err included), or not success(!='0')
	 */
	if (ret != 1 || cmdbuf[0] != '0')
		return ret < 0 ? ret : -EINVAL;

	return len;
}

static ssize_t wol_flag_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return landisk_r8c_flag_get(dev, buf, CMD_WOL);
}

static ssize_t wol_flag_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t len)
{
	return landisk_r8c_flag_set(dev, buf, len, CMD_WOL, "set", "remove");
}

static DEVICE_ATTR_RW(wol_flag);

static ssize_t first_boot_on_ac_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return landisk_r8c_flag_get(dev, buf, CMD_FIRST);
}

static DEVICE_ATTR_RO(first_boot_on_ac);

static ssize_t power_on_ac_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return landisk_r8c_flag_get(dev, buf, CMD_PWR_AC);
}

static ssize_t power_on_ac_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len)
{
	return landisk_r8c_flag_set(dev, buf, len, CMD_PWR_AC, "3 1", "3 0");
}

static DEVICE_ATTR_RW(power_on_ac);

static struct attribute *r8c_poweroff_attrs[] = {
	&dev_attr_wol_flag.attr,
	&dev_attr_first_boot_on_ac.attr,
	&dev_attr_power_on_ac.attr,
	NULL,
};

static const struct attribute_group r8c_poweroff_attr_group = {
	.attrs = r8c_poweroff_attrs,
};

static int landisk_r8c_reset_probe(struct platform_device *pdev)
{
	struct r8c_mcu *r8c;
	struct device *dev = &pdev->dev;
	int ret;

	r8c = dev_get_drvdata(dev->parent);
	if (!r8c)
		return -EINVAL;
	dev_set_drvdata(dev, r8c);

	/* register r8c poweroff handler with the higher priority than arm's one */
	ret = devm_register_sys_off_handler(dev, SYS_OFF_MODE_RESTART, 140,
					    landisk_r8c_reset_handler, r8c);
	if (ret)
		return ret;
	ret = devm_register_sys_off_handler(dev, SYS_OFF_MODE_POWER_OFF, 128,
					    landisk_r8c_poweroff_handler, r8c);
	if (ret)
		return ret;

	return sysfs_create_group(&dev->kobj, &r8c_poweroff_attr_group);
}

static const struct of_device_id landisk_r8c_reset_ids[] = {
	{ .compatible = "iodata,landisk-r8c-reset" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, landisk_r8c_reset_ids);

static struct platform_driver landisk_r8c_reset_driver = {
	.probe = landisk_r8c_reset_probe,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = landisk_r8c_reset_ids,
	},
};
module_platform_driver(landisk_r8c_reset_driver);

MODULE_AUTHOR("INAGAKI Hiroshi <musashino.open@gmail.com>");
MODULE_DESCRIPTION("R8C Restart driver for I-O DATA LAN DISK series");
MODULE_LICENSE("GPL v2");
