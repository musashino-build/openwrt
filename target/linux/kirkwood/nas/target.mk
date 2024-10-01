SUBTARGET:=nas
DEVICE_TYPE:=nas
BOARDNAME:=Devices which boot from SATA (NAS)
FEATURES+=boot-part rootfs-part
DEFAULT_PACKAGES+=e2fsprogs kmod-fs-ext4 kmod-fs-f2fs kmod-hwmon-drivetemp mkf2fs partx-utils

define Target/Description
	Build firmware images for NAS devices which boot from SATA.
endef
