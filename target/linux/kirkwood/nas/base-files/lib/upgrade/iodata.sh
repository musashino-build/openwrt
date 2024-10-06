iodata_disk_check_layout() {
	local fwimg="$1"
	local diskdev="$2" diff _diff=0
	local part start size

	# "-p" option specified
	[ "$UPGRADE_OPT_SAVE_PARTITIONS" = "0" ] &&
		echo 1 && return 0

	sync

	# get partition layout for the current boot disk
	get_partitions "/dev/$diskdev" bootdisk

	# get partition layout for the image
	v "Extracting boot sector from the image"
	get_image_dd "$fwimg" of=/tmp/image.bs count=63 bs=512
	get_partitions /tmp/image.bs image

	# check MBR vs. GPT
	if ! (  part_magic_efi "/dev/$diskdev" &&   part_magic_efi /tmp/image.bs) &&
	   ! (! part_magic_efi "/dev/$diskdev" && ! part_magic_efi /tmp/image.bs); then
		v "Incompatible image specified for the current boot disk! (MBR vs. GPT)"
		echo 1
		return 0
	fi

	# compare and lookup system partitions in the image
	diff="$(grep -F -x -v -f /tmp/partmap.bootdisk /tmp/partmap.image)"
	while read part start size; do
		echo "$diff" | grep -q "^ \{0,1\}$part " &&
			_diff=1 && break
	done < /tmp/partmap.image

	echo "$_diff"
}

iodata_disk_check_image() {
	local diskdev diff

	export_bootdevice && export_partdevice diskdev 0 || {
		v "Unable to determine upgrade device"
		return 1
	}

	diff=$(iodata_disk_check_layout "$1" "$diskdev") || return 1
	rm -f /tmp/image.bs /tmp/partmap.bootdisk /tmp/partmap.image

	if [ "$diff" = "1" ]; then
		v "Partition layout has changed. Full image will be written."
		v "All user partition(s) will be removed by sysupgrade."
		ask_bool 0 "Abort" && return 1
	fi

	return 0
}

gpt_update_crc32() {
	local disk="$1"
	local offset="$2"
	local target="$3"
	local bs=4

	dd if=/dev/zero of="$disk" bs=$bs count=1 seek=$((offset / bs)) \
		conv=fsync,notrunc 2>/dev/null
	dd if="$disk" bs=$bs \
		skip=$((${target#*@} / bs)) \
		count=$((${target%@*} / bs)) 2>/dev/null | \
			gzip -c | tail -c8 | head -c4 > /tmp/crc32.bin
	dd if=/tmp/crc32.bin of="$disk" bs=$bs count=1 seek=$((offset / bs)) \
		conv=fsync,notrunc 2>/dev/null
}

iodata_disk_do_upgrade() {
	local fwimg="$1"
	local diskdev partdev diff gpt
	local part start size
	local bloffs=1 blcnt=2047

	export_bootdevice && export_partdevice diskdev 0 || {
		v "Unable to determine upgrade device"
		return 1
	}

	diff=$(iodata_disk_check_layout "$fwimg" "$diskdev") || {
		umount -a
		reboot -f
	}

	if [ "$diff" = "1" ]; then
		get_image_dd "$fwimg" of="/dev/$diskdev" bs=4096 conv=fsync

		# Separate removal and addtion is necessary; otherwise, partition 1
		# will be missing if it overlaps with the old partition 2
		partx -d - "/dev/$diskdev"
		partx -a - "/dev/$diskdev"

		return 0
	fi

	part_magic_efi "/tmp/image.bs" && gpt=1
	if [ -n "$gpt" ]; then
		v "Writing new GPT header to /dev/$diskdev..."
		get_image_dd "$fwimg" of="/dev/$diskdev" bs=512 skip=1 seek=1 count=1 conv=fsync
		bloffs=34
		blcnt=2014
	fi

	v "Writing new bootloader to /dev/$diskdev"
	get_image_dd "$fwimg" of="/dev/$diskdev" bs=512 skip=$bloffs seek=$bloffs count=$blcnt conv=fsync


	#iterate over each partition from the image and write it to the boot disk
	while read part start size; do
		if export_partdevice partdev $part; then
			if [ -n "$gpt" ]; then
				v "Writing new GPT partition entry of $partdev to /dev/$diskdev..."
				get_image_dd "$fwimg" of="/dev/$diskdev" bs=128 count=1 \
					skip=$((8 + (part - 1))) seek=$((8 + (part - 1))) conv=fsync
			fi

			v "Writing image to /dev/$partdev..."
			get_image_dd "$fwimg" of="/dev/$partdev" ibs=512 obs=1M skip="$start" count="$size" conv=fsync
		else
			v "Unable to find partition $part device, skipped."
		fi
	done < /tmp/partmap.image

	if [ -n "$gpt" ]; then
		v "Updating CRC32 checksums in GPT header..."
		gpt_update_crc32 "/dev/$diskdev" 0x258 "0x4000@0x400" # entry crc32
		gpt_update_crc32 "/dev/$diskdev" 0x210 "0x5c@0x200"   # header crc32
	fi

	v "Writing new MBR UUID to /dev/$diskdev..."
	get_image_dd "$fwimg" of="/dev/$diskdev" bs=1 skip=440 count=4 seek=440 conv=fsync

	sleep 1
}

iodata_disk_copy_config() {
	local part="${1:-1}" partdev

	if export_partdevice partdev $part; then
		mkdir -p /boot
		[ -f /boot/kernel.img ] || mount -o rw,noatime /dev/$partdev /boot
		cp -af "$UPGRADE_BACKUP" "/boot/$BACKUP_FILE"
		sync
		umount /boot
	fi
}
