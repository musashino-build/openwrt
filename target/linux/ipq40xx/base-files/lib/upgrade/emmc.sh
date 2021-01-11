#!/bin/sh

. /lib/functions.sh

find_mmc_part() {
	local FINDNAME=$1
	local DEVNAME PARTNAME

	if grep -q "$FINDNAME" /proc/mtd; then
		echo "" && return 0
	fi

	for DEVNAME in /sys/block/mmcblk0/mmcblk*p*; do
		PARTNAME=$(grep PARTNAME ${DEVNAME}/uevent | cut -f2 -d'=')
		[ "$PARTNAME" = "$FINDNAME" ] && echo "/dev/$(basename $DEVNAME)" && return 0
	done
}

get_active_part() {
	local root rootfs

	if read cmdline < /proc/cmdline; then
		case "$cmdline" in
			*rootfsname=*)
				rootfs="${cmdline##*rootfsname=}"
				rootfs="${rootfsname%% *}"
			;;
		esac
		echo $rootfs
	fi
}

do_flash_partition() {
	local bin=$1
	local mmcblk=$2

	if [ -e "$mmcblk" ]; then
		#dd bs=4K if=/dev/zero of=${mmcblk}
		echo dd bs=4K if=${bin} of=${mmcblk}
		dd bs=4K if=${bin} of=${mmcblk}
	fi
}

do_emmc_upgrade_tar() {
	local tar_file=$1
	local kernel_dev=$2
	local rootfs_dev=$3
	local kernel_bin=/tmp/kernel.bin
	local rootfs_bin=/tmp/rootfs.bin

	local board_dir=$(tar tf $tar_file | grep -m 1 '^sysupgrade-.*/$')
	board_dir=${board_dir%/}

	echo "flashing kernel to ${kernel_dev}"
	tar xf $tar_file ${board_dir}/kernel -O > $kernel_bin
	do_flash_partition $kernel_bin $kernel_dev

	echo "flashing rootfs to ${rootfs_dev}"
	tar xf $tar_file ${board_dir}/root -O > $rootfs_bin
	do_flash_partition $rootfs_bin $rootfs_dev

	echo upgrade complete!
	echo umount and rebooting...
	sync
	umount -a
	reboot -f
}

emmc_do_upgrade() {
	local tar_file=$1
	local kernel_dev
	local rootfs_dev
	local board=$(board_name)
	local rootfs="$(get_active_part)"

	case "$board" in
		sony,ncp-hg100)
			kernel_dev=$(find_mmc_part 0:HLOS_1)
			rootfs_dev=$(find_mmc_part rootfs_1)
			case "${rootfs}" in
				"rootfs"|\
				"")
					local BOOTCONFIG=$(find_mmc_part 0:BOOTCONFIG)
					local BOOTCONFIG1=$(find_mmc_part 0:BOOTCONFIG1)
					local bootpart=`dd if=${BOOTCONFIG} bs=1 count=1 skip=108 2> /dev/null |hexdump -e '"%d"'`
					if [ ${bootpart} -eq 0 ]; then
						echo force change "BOOTCONFIG"
						echo -en '\x01' | dd of=${BOOTCONFIG} bs=1 count=1 seek=88
						echo -en '\x01' | dd of=${BOOTCONFIG} bs=1 count=1 seek=108
						echo -en '\x01' | dd of=${BOOTCONFIG1} bs=1 count=1 seek=88
						echo -en '\x01' | dd of=${BOOTCONFIG1} bs=1 count=1 seek=108
					fi
				;;
				*)
					echo no change boot partition settings
				;;
			esac
			;;
		*)
			return 1
		;;
	esac

	do_emmc_upgrade_tar $tar_file $kernel_dev $rootfs_dev

	return 0
}
