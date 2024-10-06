iodata_disk_check_image() {
	local file="$1"
	local diskdev partdev diff bsect_cnt=1

	export_bootdevice && export_partdevice diskdev 0 || {
		v "Unable to determine upgrade device"
		return 1
	}

	get_partitions "/dev/$diskdev" bootdisk

	# bsect_cnt: MBR->1, GPT->63
	part_magic_efi "/dev/$diskdev" && bsect_cnt=63
	v "Extract boot sector from the image"
	get_image_dd "$file" of=/tmp/image.bs count="$bsect_cnt" bs=512b

	get_partitions /tmp/image.bs image

	#compare tables
	diff="$(grep -F -x -v -f /tmp/partmap.bootdisk /tmp/partmap.image)"

	rm -f /tmp/image.bs /tmp/partmap.bootdisk /tmp/partmap.image

	if [ -n "$diff" ]; then
		v "Partition layout has changed. Full image will be written."
		ask_bool 0 "Abort" && exit 1
		return 0
	fi
}

iodata_disk_do_upgrade() {
	local file="$1"
	local board=$(board_name)
	local diskdev partdev diff bsect_cnt=1 index=1

	export_bootdevice && export_partdevice diskdev 0 || {
		v "Unable to determine upgrade device"
	return 1
	}

	sync

	if [ "$UPGRADE_OPT_SAVE_PARTITIONS" = "1" ]; then
		get_partitions "/dev/$diskdev" bootdisk

		part_magic_efi "/dev/$diskdev" && bsect_cnt=63
		v "Extract boot sector from the image"
		get_image_dd "$file" of=/tmp/image.bs count="$bsect_cnt" bs=512b

		get_partitions /tmp/image.bs image

		#compare tables
		diff="$(grep -F -x -v -f /tmp/partmap.bootdisk /tmp/partmap.image)"
	else
		diff=1
	fi

	if [ -n "$diff" ]; then
		get_image_dd "$file" of="/dev/$diskdev" bs=4096 conv=fsync

		# Separate removal and addtion is necessary; otherwise, partition 1
		# will be missing if it overlaps with the old partition 2
		partx -d - "/dev/$diskdev"
		partx -a - "/dev/$diskdev"
	else
		if ! part_magic_efi "/dev/$diskdev"; then
			v "Writing bootloader to /dev/$diskdev"
			get_image_dd "$1" of="$diskdev" bs=512 skip=1 seek=1 count=2048 conv=fsync
		fi

		#iterate over each partition from the image and write it to the boot disk
		while read part start size; do
			if export_partdevice partdev $part; then
				v "Writing image to /dev/$partdev..."
				export PART${index}_BLOCKS=$(\
					get_image "$file" 2>/dev/null | \
					dd of="/dev/$partdev" bs="512" \
						skip="$start" count="$size" conv=fsync 2>&1 | \
					grep "records out" | cut -d' ' -f1)

				# prepare overlay area in rootfs partition
				if [ "$index" = "2" ]; then
					# Account for 64KiB ROOTDEV_OVERLAY_ALIGN in libfstools
					[ -n "$PART2_BLOCKS" ] && \
						PART2_BLOCKS=$(( (PART2_BLOCKS + 127) & ~127 ))
					[ -z "$UPGRADE_BACKUP" ] && \
						dd if="/dev/zero" of="/dev/$partdev" \
							bs=512 seek="$PART2_BLOCKS" \
							count=8 2>/dev/null
				fi
			else
				v "Unable to find partition $part device, skipped."
			fi
			index=$((index + 1))
		done < /tmp/partmap.image

		v "Writing new UUID to /dev/$diskdev..."
		get_image_dd "$file" of="/dev/$diskdev" bs=1 skip=440 count=4 seek=440 conv=fsync
	fi

	sleep 1
}

# Note: $UPGRADE_BACKUP in the 1st partition (for kernel, ext3)
#       won't be handled when booting, on I-O DATA HDLx-A series
iodata_disk_copy_config() {
	local partdev

	if [ -n "$PART2_BLOCKS" ] && export_partdevice partdev 2; then
		v "Appending $UPGRADE_BACKUP to /dev/$partdev"
		dd if="$UPGRADE_BACKUP" of="/dev/$partdev" bs=512 seek="$PART2_BLOCKS"
	else
		v "Unable to append $UPGRADE_BACKUP to rootfs partition"
	fi
}
