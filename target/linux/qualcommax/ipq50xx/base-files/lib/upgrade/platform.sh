PART_NAME=firmware
REQUIRE_IMAGE_METADATA=1

RAMFS_COPY_BIN='fw_printenv fw_setenv head seq'
RAMFS_COPY_DATA='/etc/fw_env.config /var/lock/fw_printenv.lock'

bootconfig_find_entry() {
	local cfgbin="$1"
	local partname="$2"
	local i part parts offset

	parts=$(hexdump -n 4 -s 8 -e '1/4 "%d"' "$cfgbin")
	# partition count: <= 10
	[ -z "$parts" ] || [ "$parts" = "0" ] || [ "$parts" -gt "10" ] && \
		return 1

	for i in $(seq 1 $parts); do
		offset=$((0xc + 0x14 * (i - 1)))
		part=$(dd if="$cfgbin" iflag=skip_bytes \
			skip=$offset bs=16 count=1 2>/dev/null)
		if [ "$part" = "$partname" ]; then
			printf "0x%08x" $offset
			return
		fi
	done

	return 1
}

# Read or update an entry in Qualcomm bootconfig partition
#
# parameters:
#   $1: partition name of bootconfig (ex.: "0:bootconfig", "0:bootconfig1", etc)
#   $2: entry name in bootconfig (ex.: "0:hlos", "rootfs", etc)
#   $3: index to set for the entry (0/1)
#
# operations:
#   read : bootconfig_rw_index <bootconfig> <entry>
#   write: bootconfig_rw_index <bootconfig> <entry> <index>
bootconfig_rw_index() {
	local bootcfg="$1"
	local partname="$2"
	local index="$3"
	local mtddev
	local offset
	local current

	case "$index" in
	0|1|"") ;;
	*) echo "invalid bootconfig index specified \"$index\""; return 1 ;;
	esac

	mtddev="$(find_mtd_part $bootcfg)"
	[ -z "$mtddev" ] && \
		return 1

	dd if=$mtddev of=/tmp/${mtddev##*/} bs=1k

	offset=$(bootconfig_find_entry "/tmp/${mtddev##*/}" $partname) || return 1
	current=$(hexdump -n 4 -s $((offset + 0x10)) -e '1/4 "%d"' /tmp/${mtddev##*/})

	[ -z "$index" ] && \
		echo "$current" && return 0

	if [ "$current" != "$index" ]; then
		printf "\x$index" | \
			dd of=$mtddev conv=notrunc bs=1 seek=$((offset + 0x10))
	fi
}

# Qcom U-Boot always sets a name of current active partition to "rootfs" and
# inactive partition is named as "rootfs_1", in smem partition table.
# When the second partition is active, "rootfs" and "rootfs_1" are swapped.
smempart_next_root() {
	local index="$1"
	local root_idx="$(find_mtd_index rootfs)"
	local root1_idx="$(find_mtd_index rootfs_1)"
	local root_offset root1_offset

	[ -z "$root_idx" ] || [ -z "$root1_idx" ] && \
		return 1

	root_offset=$(cat /sys/block/mtdblock$root_idx/device/offset)
	root1_offset=$(cat /sys/block/mtdblock$root1_idx/device/offset)

	case "$index" in
	0)
		[ "$root_offset" -lt "$root1_offset" ] && \
			echo "rootfs" || \
			echo "rootfs_1"
		;;
	1)
		[ "$root_offset" -lt "$root1_offset" ] && \
			echo "rootfs_1" || \
			echo "rootfs"
		;;
	*)
		v "invalid index specified..."
		return 1
		;;
	esac
}

remove_oem_ubi_volume() {
	local oem_volume_name="$1"
	local oem_ubivol
	local mtdnum
	local ubidev

	mtdnum=$(find_mtd_index "$CI_UBIPART")
	if [ ! "$mtdnum" ]; then
		return
	fi

	ubidev=$(nand_find_ubi "$CI_UBIPART")
	if [ ! "$ubidev" ]; then
		ubiattach --mtdn="$mtdnum"
		ubidev=$(nand_find_ubi "$CI_UBIPART")
	fi

	if [ "$ubidev" ]; then
		oem_ubivol=$(nand_find_volume "$ubidev" "$oem_volume_name")
		[ "$oem_ubivol" ] && ubirmvol "/dev/$ubidev" --name="$oem_volume_name"
	fi
}

linksys_mx_do_upgrade() {
	local setenv_script="/tmp/fw_env_upgrade"

	CI_UBIPART="rootfs"
	boot_part="$(fw_printenv -n boot_part)"
	if [ -n "$UPGRADE_OPT_USE_CURR_PART" ]; then
		if [ "$boot_part" -eq "2" ]; then
			CI_KERNPART="alt_kernel"
			CI_UBIPART="alt_rootfs"
		fi
	else
		if [ "$boot_part" -eq "1" ]; then
			echo "boot_part 2" >> $setenv_script
			CI_KERNPART="alt_kernel"
			CI_UBIPART="alt_rootfs"
		else
			echo "boot_part 1" >> $setenv_script
		fi
	fi

	boot_part_ready="$(fw_printenv -n boot_part_ready)"
	if [ "$boot_part_ready" -ne "3" ]; then
		echo "boot_part_ready 3" >> $setenv_script
	fi

	auto_recovery="$(fw_printenv -n auto_recovery)"
	if [ "$auto_recovery" != "yes" ]; then
		echo "auto_recovery yes" >> $setenv_script
	fi

	if [ -f "$setenv_script" ]; then
		fw_setenv -s $setenv_script || {
			echo "failed to update U-Boot environment"
			return 1
		}
	fi
	nand_do_upgrade "$1"
}

platform_check_image() {
	return 0;
}

platform_do_upgrade() {
	case "$(board_name)" in
	elecom,wrc-x3000gs2)
		local delay index

		delay=$(fw_printenv bootdelay)
		[ -z "$delay" ] || [ "$delay" -eq "0" ] && \
			fw_setenv bootdelay 3

		index=$(bootconfig_rw_index "0:bootconfig" rootfs) || \
			v "failed to read bootconfig index..." && \
			nand_do_upgrade_failed
		CI_UBIPART=$(smempart_next_root $index) || \
			v "failed to get next root..." && \
			nand_do_upgrade_failed

		remove_oem_ubi_volume ubi_rootfs
		remove_oem_ubi_volume wifi_fw
		nand_do_upgrade "$1"
		;;
	linksys,mx2000|\
	linksys,mx5500|\
	linksys,spnmx56)
		remove_oem_ubi_volume squashfs
		linksys_mx_do_upgrade "$1"
		;;
	*)
		default_do_upgrade "$1"
		;;
	esac
}
