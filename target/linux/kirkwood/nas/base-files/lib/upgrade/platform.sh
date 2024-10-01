RAMFS_COPY_BIN=''
RAMFS_COPY_DATA=''

PART_NAME=firmware
REQUIRE_IMAGE_METADATA=1

platform_check_image() {
	local board="$(board_name)"

	case "$board" in
	*)
		return 0
		;;
	esac
}

platform_do_upgrade() {
	local board="$(board_name)"

	case "$board" in
	*)
		nand_do_upgrade "$1"
		;;
	esac
}

platform_copy_config() {
	local board="$(board_name)"

	case "$board" in
	*)
		return 0
		;;
	esac
}
