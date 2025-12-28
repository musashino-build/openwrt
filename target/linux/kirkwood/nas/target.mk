SUBTARGET:=nas
DEVICE_TYPE:=nas
BOARDNAME:=Devices which boot from SATA (NAS)
FEATURES+=ext4 boot-part rootfs-part
DEFAULT_PACKAGES+=kmod-fs-ext4 kmod-hwmon-drivetemp e2fsprogs partx-utils

define Target/Description
	Build firmware images for NAS devices which boot from SATA.
endef
