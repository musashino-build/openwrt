// SPDX-License-Identifier: GPL-2.0-or-later

#define DRV_NAME	"iodata-landisk-reboot"
#define pr_fmt(fmt)	DRV_NAME ": " fmt

#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/phy.h>
#include <linux/tty.h>

#define CMD_RESET	":reset\n"
#define CMD_POWEROFF	":poweroff\n"

#define MCU_TTY		"ttyS1"
#define MCU_BAUD	57600

#define MII_MARVELL_COPPER_PAGE		0
#define   MII_MARVELL_COPPER_CTRL	0
#define     MII_MARVELL_COPPER_CTRL_RST		BIT(15)
#define     MII_MARVELL_COPPER_CTRL_AN		BIT(12)
#define     MII_MARVELL_COPPER_CTRL_ISO		BIT(10)
#define     MII_MARVELL_COPPER_CTRL_DUP		BIT(8)
#define     MII_MARVELL_COPPER_CTRL_SPD_M	BIT(6)
#define MII_MARVELL_LED_PAGE		3
#define   MII_MARVELL_LED_POL_CTRL	17
#define     MII_MARVELL_LED_POL_CTRL_LED2	GENMASK(5,4)

static struct phy_device *phydev;

static int landisk_wol_setup(void)
{
	struct ethtool_wolinfo wol = {
		.wolopts = WAKE_MAGIC,
	};
	int ret;

	/*
	 * reset  : reset->exit
	 * speed  : 1000M (default)
	 * aneg   : enable
	 * isolate: enable
	 * duplex : full (default)
	 */
	ret = phy_write_paged(phydev, MII_MARVELL_COPPER_PAGE,
			      MII_MARVELL_COPPER_CTRL,
			      MII_MARVELL_COPPER_CTRL_RST);
	if (ret >= 0)
		ret = phy_write_paged(phydev, MII_MARVELL_COPPER_PAGE,
				      MII_MARVELL_COPPER_CTRL,
				      MII_MARVELL_COPPER_CTRL_AN |
				      MII_MARVELL_COPPER_CTRL_ISO |
				      MII_MARVELL_COPPER_CTRL_DUP |
				      MII_MARVELL_COPPER_CTRL_SPD_M);
	if (ret < 0)
		return ret;

	/* setup WoL via PHY driver */
	if (phydev->drv && phydev->drv->set_wol) {
		ret = phydev->drv->set_wol(phydev, &wol);
		if (ret)
			return ret;
	}

	/* set polarity of LED[2] to prevent unwanted reboot when poweroff */
	return phy_modify_paged(phydev, MII_MARVELL_LED_PAGE,
				MII_MARVELL_LED_POL_CTRL,
				MII_MARVELL_LED_POL_CTRL_LED2, 0);
}

static int landisk_handler_common(struct sys_off_data *data, char *cmd)
{
	struct tty_struct *tty;
	struct ktermios ktermios;
	dev_t n;

	if (phydev && landisk_wol_setup())
		pr_warn("failed to setup WoL!"
			" please use \"POWER\" button for next turning on\n");

	if (tty_dev_name_to_number(MCU_TTY, &n))
		return NOTIFY_BAD;
	tty = tty_kopen_exclusive(n);
	if (IS_ERR_OR_NULL(tty))
		return NOTIFY_BAD;

	if (tty->ops->open)
		tty->ops->open(tty, NULL);

	ktermios = tty->termios;
	ktermios.c_cflag &= ~(CBAUD | CRTSCTS | PARENB | PARODD);
	tty_termios_encode_baud_rate(&ktermios, MCU_BAUD, MCU_BAUD);
	tty_set_termios(tty, &ktermios);
	tty->ops->write(tty, cmd, strnlen(cmd, 12));

	if (tty->ops->close)
		tty->ops->close(tty, NULL);

	tty_unlock(tty);
	tty_kclose(tty);

	return NOTIFY_DONE;
}

static int landisk_reboot_handler(struct sys_off_data *data)
{
	return landisk_handler_common(data, CMD_RESET);
}

static int landisk_poweroff_handler(struct sys_off_data *data)
{
	return landisk_handler_common(data, CMD_POWEROFF);
}

static int landisk_reboot_probe(struct platform_device *pdev)
{
	struct device_node *np;
	int ret;

	np = of_parse_phandle(pdev->dev.of_node, "phy-handle", 0);
	if (np) {
		phydev = of_phy_find_device(np);
		of_node_put(np);
		if (!phydev)
			dev_warn(&pdev->dev, "failed to get a phy device, ignored\n");
		else
			dev_info(&pdev->dev, "got a phy device (id: 0x%x)\n",
				 phydev->phy_id);
	}

	ret = devm_register_sys_off_handler(&pdev->dev, SYS_OFF_MODE_RESTART,
			SYS_OFF_PRIO_HIGH,
			landisk_reboot_handler, NULL);
	if (ret)
		return ret;
	return devm_register_sys_off_handler(&pdev->dev, SYS_OFF_MODE_POWER_OFF,
			SYS_OFF_PRIO_HIGH,
			landisk_poweroff_handler, NULL);
}

static const struct of_device_id landisk_reboot_ids[] = {
	{ .compatible = "iodata,landisk-reboot" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, landisk_reboot_ids);

static struct platform_driver landisk_reboot_driver = {
	.probe = landisk_reboot_probe,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = landisk_reboot_ids,
	},
};
module_platform_driver(landisk_reboot_driver);

MODULE_AUTHOR("INAGAKI Hiroshi <musashino.open@gmail.com>");
MODULE_DESCRIPTION("Reboot driver for I-O DATA LAN DISK series NAS");
MODULE_LICENSE("GPL v2");
