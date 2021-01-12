#!/bin/sh

. /lib/functions.sh

update_bootconfig() {
	local offset=$1
	local index="$2"
	local cfgpart=$(find_mmc_part "0:BOOTCONFIG")
	local cur_index

	if [ -z "$cfgpart" ]; then
		echo "Failed to get the partition: \"0:BOOTCONFIG\""
		return 1
	fi

	cur_index=$(dd if=${cfgpart} bs=1 count=1 skip=$offset 2> /dev/null | hexdump -e '"%d"')

	if [ ${index} != ${cur_index} ]; then
		echo "force change \"BOOTCONFIG\""
		echo -en "\x0${index}" | \
			dd of=${cfgpart} bs=1 count=1 seek=$offset conv=notrunc 2>/dev/null
	fi

	# also update 0:BOOTCONFIG1 if exists
	cfgpart=$(find_mmc_part "0:BOOTCONFIG1")
	[ -z "$cfgpart" ] && return

	if [ ${index} != ${cur_index} ]; then
		echo -en "\x0${index}" | \
			dd of=${cfgpart} bs=1 count=1 seek=$offset conv=notrunc 2>/dev/null
	fi
}

sony_emmc_do_upgrade() {
	local tar_file=$1
	local kernel_dev
	local rootfs_dev
	local board_dir

	kernel_dev=$(find_mmc_part "0:HLOS")
	rootfs_dev=$(find_mmc_part "rootfs")

	if [ -z "$kernel_dev" -o -z "$rootfs_dev" ]; then
		echo "The partition name for kernel or rootfs is not specified or failed to get the mmc device."
		exit 1
	fi

	# keep sure its unbound
	losetup --detach-all || {
		echo "Failed to detach all loop devices, skip this try"
		reboot -f
	}

	# use first partitions of kernel/rootfs for NCP-HG100
	# - offset  88 (0x58): 0:HLOS (kernel)
	# - offset 108 (0x6c): rootfs
	update_bootconfig 88 0 || exit 1
	update_bootconfig 108 0 || exit 1

	board_dir=$(tar tf $tar_file | grep -m 1 '^sysupgrade-.*/$')
	board_dir=${board_dir%/}

	echo "Flashing kernel to ${kernel_dev}"
	tar xf $tar_file ${board_dir}/kernel -O > $kernel_dev

	echo "Flashing rootfs to ${rootfs_dev}"
	tar xf $tar_file ${board_dir}/root -O > $rootfs_dev

	local offset=$(tar xf $tar_file ${board_dir}/root -O | wc -c)
	# calculate padded offset by 65536 (0x10000) for rootfs_data
	# detection by fstools on booting, if (offset % 65536) != 0
	[ "$((offset % 65536))" != "0" ] && \
		offset=$(( (offset / 65536 + 1) * 65536))

	# mount rootfs to loop device with offset for rootfs_data creation
	local loopdev="$(losetup -f)"
	losetup -o $offset $loopdev $rootfs_dev || {
		echo "Failed to mount rootfs to $loopdev"
		reboot -f
	}

	echo "Format new rootfs_data at position $offset"
	mkfs.ext4 -F -L rootfs_data $loopdev

	if [ -e "$UPGRADE_BACKUP" ]; then
		mkdir /tmp/new_root
		mount -t ext4 $loopdev /tmp/new_root && {
			echo "Saving configurations to rootfs_data at position $offset"
			cp "$UPGRADE_BACKUP" "/tmp/new_root/$BACKUP_FILE"
			umount /tmp/new_root
		}
	fi

	echo "sysupgrade successful"

	# cleanup
	losetup -d $loopdev >/dev/null 2>&1
	sync
	umount -a
	reboot -f
}
