#
# Copyright (C) 2006-2020 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#
include $(TOPDIR)/rules.mk

PKG_NAME:=firmware-utils
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL=https://github.com/musashino-build/firmware-utils.git
PKG_SOURCE_DATE:=2024-06-20
PKG_SOURCE_VERSION:=79feb8664626dba5d017dbc369cdf2a06c861b33
PKG_MIRROR_HASH:=

include $(INCLUDE_DIR)/host-build.mk
include $(INCLUDE_DIR)/cmake.mk

$(eval $(call HostBuild))
