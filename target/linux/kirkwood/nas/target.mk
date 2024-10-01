SUBTARGET:=nas
DEVICE_TYPE:=nas
BOARDNAME:=Devices which boot from SATA (NAS)
FEATURES+=boot-part ext4 rootfs-part
DEFAULT_PACKAGES+=e2fsprogs kmod-hwmon-drivetemp partx-utils

define Target/Description
	Build firmware images for NAS devices which boot from SATA.
endef
