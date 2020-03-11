include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/image.mk

IPQ807X_KERNEL_LOADADDR = 0x41080000
#DEVICE_DTS_CONFIG = "config@hk01"

define Device/Default
	PROFILES := Default
	KERNEL_DEPENDS = $$(wildcard $$(DEVICE_DTS_DIR)/$$(DEVICE_DTS).dts)
	KERNEL_INITRAMFS_PREFIX := $$(IMG_PREFIX)-$(1)-initramfs
	KERNEL_PREFIX := $$(IMAGE_PREFIX)
	KERNEL_LOADADDR = $(IPQ807X_KERNEL_LOADADDR)
	DEVICE_DTS_DIR := $(DTS_DIR)/qcom
	DEVICE_DTS = $$(SOC)-$(subst _,-,$(1))
	SUPPORTED_DEVICES := $(subst _,$(comma),$(1))
	IMAGE/sysupgrade.bin = sysupgrade-tar | append-metadata
	IMAGE/sysupgrade.bin/squashfs :=
endef

$(eval $(call BuildImage))
