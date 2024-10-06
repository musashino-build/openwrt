RAMFS_COPY_BIN='head'
RAMFS_COPY_DATA=''

PART_NAME=firmware
REQUIRE_IMAGE_METADATA=1

platform_check_image() {
	local board="$(board_name)"

	case "$board" in
	iodata,hdl2-a-sata|\
	iodata,hdl2-a-usb)
		iodata_disk_check_image "$1"
		;;
	*)
		return 0
		;;
	esac
}

platform_pre_upgrade() {
	local board="$(board_name)"

	case "$board" in
	iodata,hdl2-a-sata|\
	iodata,hdl2-a-usb)
		# green, blink
		echo ":sts blink" > /dev/ttyS1
		;;
	*)
		return 0
		;;
	esac
}

platform_do_upgrade() {
	local board="$(board_name)"

	case "$board" in
	iodata,hdl2-a-sata|\
	iodata,hdl2-a-usb)
		iodata_disk_do_upgrade "$1"
		;;
	*)
		nand_do_upgrade "$1"
		;;
	esac
}

platform_copy_config() {
	local board="$(board_name)"

	case "$board" in
	iodata,hdl2-a-sata|\
	iodata,hdl2-a-usb)
		iodata_disk_copy_config 2
		;;
	*)
		return 0
		;;
	esac
}
