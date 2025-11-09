#
# Copyright (C) 2008-2012 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=mv88e6xxx-plat
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0

include $(INCLUDE_DIR)/package.mk

define KernelPackage/mv88e6xxx-plat
  SUBMENU:=Network Devices
  TITLE:=Platform registration support for mv88e6xxx DSA driver
  DEPENDS:=+kmod-dsa-mv88e6xxx
  FILES:=$(PKG_BUILD_DIR)/mv88e6xxx_bdinfo.ko
  AUTOLOAD:=$(call AutoLoad,30,mv88e6xxx_bdinfo,1)
  KCONFIG:=
endef

define KernelPackage/mv88e6xxx-plat/description
  Register mv88e6xxx switches via platform support of mv88e6xxx DSA driver
  instead of Device Tree.
endef

define Build/Compile
	$(KERNEL_MAKE) M="$(PKG_BUILD_DIR)" modules
endef

$(eval $(call KernelPackage,mv88e6xxx-plat))
