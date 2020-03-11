include $(TOPDIR)/rules.mk

ARCH:=arm
BOARD:=ipq807x
BOARDNAME:=Qualcomm Atheros IPQ807x
FEATURES:=squashfs ramdisk source-only
KERNELNAME:=Image dtbs
SUBTARGETS:=arm arm64

KERNEL_PATCHVER:=5.4

include $(INCLUDE_DIR)/target.mk

$(eval $(call BuildTarget))
