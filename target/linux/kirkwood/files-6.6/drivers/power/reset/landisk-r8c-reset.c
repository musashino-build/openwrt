// SPDX-License-Identifier: GPL-2.0-or-later

#define DRV_NAME	"landisk-r8c-reset"
#define pr_fmt(fmt)	DRV_NAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/reboot.h>

#include <linux/mfd/landisk-r8c.h>

#define CMD_RESET	"reset"
#define CMD_POFF	"poweroff"
#define CMD_WOL		"wol_flag"
#define CMD_FIRST	"first"
#define CMD_PWR		"intrp"
#define CMD_DUMMY	"sts"

#define MII_MARVELL_COPPER_PAGE			0
#  define MII_MARVELL_COPPER_CTRL		0
#    define MII_MARVELL_COPPER_CTRL_RST		BIT(15)
#    define MII_MARVELL_COPPER_CTRL_AN		BIT(12)
#    define MII_MARVELL_COPPER_CTRL_ISO		BIT(10)
#    define MII_MARVELL_COPPER_CTRL_DUP		BIT(8)
#    define MII_MARVELL_COPPER_CTRL_SPD_M	BIT(6)
#define MII_MARVELL_LED_PAGE			3
#  define MII_MARVELL_LED_POL_CTRL		17
#    define MII_MARVELL_LED_POL_CTRL_LED2	GENMASK(5,4)

struct r8c_reset {
	struct r8c_mcu *r8c;
	struct phy_device *phydev;
};

static int landisk_r8c_wol_setup(struct r8c_reset *r)
{
	struct ethtool_wolinfo wol = {
		.wolopts = WAKE_MAGIC,
	};
	char cmdbuf[4];
	int ret;

	if (!r->phydev)
		return 0;
	else if (!r->phydev->drv)
		return -EUNATCH;
	else if (!r->phydev->drv->set_wol)
		return -EOPNOTSUPP;

	ret = landisk_r8c_exec_cmd(r->r8c, CMD_WOL, NULL,
				   cmdbuf, sizeof(cmdbuf));
	/* invalid response from MCU (or execution failure) */
	if (ret != 1)
		return -EINVAL;

	pr_info("WoL is %s\n", cmdbuf[0] == '0' ? "disabled" : "enabled");
	if (cmdbuf[0] == '0')
		return 0;

	/*
	 * reset  : reset->exit
	 * speed  : 1000M (default)
	 * aneg   : enable
	 * isolate: enable
	 * duplex : full (default)
	 *
	 * Note: use Auto-Negotiation instead of fixed speeds for Multi-Gig
	 *       partner devices (without 10/100M support) and FE partner devices
	 */
	ret = phy_write_paged(r->phydev, MII_MARVELL_COPPER_PAGE,
			      MII_MARVELL_COPPER_CTRL,
			      MII_MARVELL_COPPER_CTRL_RST);
	if (ret >= 0)
		ret = phy_write_paged(r->phydev, MII_MARVELL_COPPER_PAGE,
				      MII_MARVELL_COPPER_CTRL,
				      MII_MARVELL_COPPER_CTRL_AN |
				      MII_MARVELL_COPPER_CTRL_ISO |
				      MII_MARVELL_COPPER_CTRL_DUP |
				      MII_MARVELL_COPPER_CTRL_SPD_M);
	if (ret < 0)
		return ret;

	/* setup WoL via PHY driver */
	ret = r->phydev->drv->set_wol(r->phydev, &wol);
	if (ret)
		return ret;

	/* set polarity of LED[2] to prevent unwanted reboot when poweroff */
	return phy_modify_paged(r->phydev, MII_MARVELL_LED_PAGE,
				MII_MARVELL_LED_POL_CTRL,
				MII_MARVELL_LED_POL_CTRL_LED2, 0);
}

static int landisk_r8c_handler_common(struct sys_off_data *data, char *cmd)
{
	struct r8c_reset *r = data->cb_data;
	int ret;

	/* work-around for "reset" command failure */
	if (!strncmp(cmd, CMD_RESET, 5)) {
		char buf[4];
		ret = landisk_r8c_exec_cmd(r->r8c, CMD_DUMMY, NULL,
					   buf, sizeof(buf));
		if (ret <= 0) {
			pr_err("failed to execute dummy command %d\n",
			       ret);
			return NOTIFY_BAD;
		}
	} else {
		if (landisk_r8c_wol_setup(r))
			pr_err("failed to setup WoL!"
			       " please use \"POWER\" button instead\n");
	}

	ret = landisk_r8c_exec_cmd(r->r8c, cmd, NULL, NULL, 0);
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

static ssize_t landisk_r8c_flag_get(struct device *dev, char *buf, const char *cmd,
				    const char *arg)
{
	struct r8c_reset *r = dev_get_drvdata(dev);
	ssize_t ret;
	char cmdbuf[4];

	ret = landisk_r8c_exec_cmd(r->r8c, cmd, arg, cmdbuf, sizeof(cmdbuf));
	if (ret != 1)
		return ret < 0 ? ret : -EINVAL;

	return scnprintf(buf, sizeof(cmdbuf), "%s\n", cmdbuf);
}

static ssize_t landisk_r8c_flag_set(struct device *dev,
				    const char *buf, size_t len, const char *cmd,
				    const char *arg_en, const char *arg_dis)
{
	struct r8c_reset *r = dev_get_drvdata(dev);
	ssize_t ret;
	char cmdbuf[4];
	u32 enable;

	ret = kstrtou32(buf, 0, &enable);
	if (ret)
		return ret;

	ret = landisk_r8c_exec_cmd(r->r8c, cmd, enable ? arg_en : arg_dis,
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
	return landisk_r8c_flag_get(dev, buf, CMD_WOL, NULL);
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
	return landisk_r8c_flag_get(dev, buf, CMD_FIRST, NULL);
}

static DEVICE_ATTR_RO(first_boot_on_ac);

static ssize_t power_on_ac_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return landisk_r8c_flag_get(dev, buf, CMD_PWR, "3");
}

static ssize_t power_on_ac_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len)
{
	return landisk_r8c_flag_set(dev, buf, len, CMD_PWR, "3 1", "3 0");
}

static DEVICE_ATTR_RW(power_on_ac);

struct r8c_pon_reason {
	char id;
	char *reason;
};

static ssize_t power_on_reason_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	const struct r8c_pon_reason landisk_r8c_pon_reasons[] = {
		{ '0', "button" },	/* pushing "POWER" button */
		{ '1', "rtc" },		/* RTC alarm */
		{ '2', "wol" },		/* Wake on LAN (if wol_flag=1) */
		{ '4', "ac" },		/* AC Connection (if power_on_ac=1)*/
	};
	int i, ret;

	ret = landisk_r8c_flag_get(dev, buf, CMD_PWR, NULL);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(landisk_r8c_pon_reasons); i++)
		if (buf[0] == landisk_r8c_pon_reasons[i].id)
			return scnprintf(buf, 32, "%c:%s\n",
					 landisk_r8c_pon_reasons[i].id,
					 landisk_r8c_pon_reasons[i].reason);

	/* unknown id returned */
	return -EINVAL;
}

static DEVICE_ATTR_RO(power_on_reason);

static struct attribute *landisk_r8c_reset_attrs[] = {
	&dev_attr_wol_flag.attr,
	&dev_attr_first_boot_on_ac.attr,
	&dev_attr_power_on_ac.attr,
	&dev_attr_power_on_reason.attr,
	NULL,
};

static umode_t landisk_r8c_reset_is_visible(struct kobject *kobj,
					    struct attribute *attr, int idx)
{
	struct r8c_reset *r = dev_get_drvdata(kobj_to_dev(kobj));

	/* return default visibility if phydev is set */
	if (attr == &dev_attr_wol_flag.attr)
		return r->phydev ? attr->mode : 0;

	return attr->mode;
}

static const struct attribute_group landisk_r8c_reset_attr_group = {
	.attrs = landisk_r8c_reset_attrs,
	.is_visible = landisk_r8c_reset_is_visible,
};

static int landisk_r8c_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct r8c_reset *r;
	struct r8c_mcu *r8c;
	struct device_node *np;
	int ret;

	r8c = dev_get_drvdata(dev->parent);
	if (!r8c)
		return -ENODEV;

	r = devm_kzalloc(dev, sizeof(*r), GFP_KERNEL);
	if (!r)
		return -ENOMEM;

	r->r8c = r8c;
	dev_set_drvdata(dev, r);

	np = of_parse_phandle(dev->of_node, "phy-handle", 0);
	if (np) {
		r->phydev = of_phy_find_device(np);
		of_node_put(np);
		if (!r->phydev) {
			dev_err(dev, "specified ethernet phy for WoL not found!\n");
			return -ENODEV;
		}
		dev_info(dev, "got phy device (id: 0x%x)\n", r->phydev->phy_id);
	}

	/* register r8c restart handler with the higher priority than arm's one */
	ret = devm_register_sys_off_handler(dev, SYS_OFF_MODE_RESTART, 140,
					    landisk_r8c_reset_handler, r);
	if (ret)
		return ret;
	ret = devm_register_sys_off_handler(dev, SYS_OFF_MODE_POWER_OFF, 128,
					    landisk_r8c_poweroff_handler, r);
	if (ret)
		return ret;

	return sysfs_create_group(&dev->kobj, &landisk_r8c_reset_attr_group);
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
