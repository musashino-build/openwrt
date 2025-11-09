// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/phy.h>
#include <linux/platform_data/mv88e6xxx.h>
#include <net/dsa.h>

/* Trend Micro 70S G2 (+ 100S G2?) */
static struct dsa_mv88e6xxx_pdata dsa_mv88e6xxx_pdata = {
	.cd = {
		.port_names[1] = "lan1",
		.port_names[2] = "lan2",
		.port_names[3] = "lan3",
		.port_names[4] = "lan4",
		.port_names[5] = "lan5",
		.port_names[6] = "lan6",
		.port_names[7] = "lan7",
		.port_names[8] = "lan8",
		/*
		 * there is no multi-CPU support,
		 * disable p9 <-> eth0 connection
		 */
		/*.port_names[9] = "cpu",*/
		.port_names[10] = "cpu",
	},
	.compatible = "marvell,mv88e6190",
	.enabled_ports = 0x5fe,
	.netdev = "eth1",
};

static const struct mdio_board_info bdinfo = {
	.bus_id = "ixgbe-mdio-0000:07:00.0",
	.modalias = "mv88e6085",
	.mdio_addr = 0,
	.platform_data = &dsa_mv88e6xxx_pdata,
};

static int __init dsa_mv88e6xxx_bdinfo_init(void)
{
	return mdiobus_register_board_info(&bdinfo, 1);
}
arch_initcall(dsa_mv88e6xxx_bdinfo_init)

MODULE_DESCRIPTION("Platform-based registration driver for Marvell 88e6xxx switches");
MODULE_LICENSE("GPL v2");
