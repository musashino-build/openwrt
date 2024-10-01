SUBTARGET:=nas
DEVICE_TYPE:=nas
BOARDNAME:=NAS Devices
FEATURES+=ext4 boot-part rootfs-part
DEFAULT_PACKAGES+=kmod-hwmon-drivetemp e2fsprogs partx-utils

define Target/Description
	Build firmware images for NAS devices which boot from SATA.
endef
