#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#

usage() {
	printf "Usage: %s -A arch -C comp -a addr -e entry" "$(basename "$0")"
	printf " -v version -k kernel [-D name -n address -d dtb] -o its_file"

	printf "\n\t-A ==> set architecture to 'arch'"
	printf "\n\t-C ==> set compression type 'comp'"
	printf "\n\t-c ==> set config name 'config'"
	printf "\n\t-a ==> set load address to 'addr' (hex)"
	printf "\n\t-e ==> set entry point to 'entry' (hex)"
	printf "\n\t-f ==> set device tree compatible string"
	printf "\n\t-i ==> include initrd Blob 'initrd'"
	printf "\n\t-v ==> set kernel version to 'version'"
	printf "\n\t-k ==> include kernel image 'kernel'"
	printf "\n\t-D ==> human friendly Device Tree Blob 'name'"
	printf "\n\t-n ==> fdt unit-address 'address'"
	printf "\n\t-d ==> include Device Tree Blob 'dtb'"
	printf "\n\t-H ==> specify hash algo instead of SHA1"
	printf "\n\t-l ==> legacy mode character (@ etc otherwise -)"
	printf "\n\t-o ==> create output file 'its_file'"
	printf "\n\t-s ==> set FDT load address to 'addr' (hex)"
	printf "\n\t\t(can be specified more than once)\n"
	exit 1
}

REFERENCE_CHAR='-'
FDTNUM=1
INITRDNUM=1
HASH=sha1
LOADABLES=
DTADDR=

while getopts ":A:a:c:C:D:d:e:f:i:k:l:n:o:v:s:H:" OPTION
do
	case $OPTION in
		A ) ARCH=$OPTARG;;
		a ) LOAD_ADDR=$OPTARG;;
		c ) CONFIG=$OPTARG;;
		C ) COMPRESS=$OPTARG;;
		D ) DEVICE=$OPTARG;;
		d ) DTB=$OPTARG;;
		e ) ENTRY_ADDR=$OPTARG;;
		f ) COMPATIBLE=$OPTARG;;
		i ) INITRD=$OPTARG;;
		k ) KERNEL=$OPTARG;;
		l ) REFERENCE_CHAR=$OPTARG;;
		n ) FDTNUM=$OPTARG;;
		o ) OUTPUT=$OPTARG;;
		s ) FDTADDR=$OPTARG;;
		H ) HASH=$OPTARG;;
		v ) VERSION=$OPTARG;;
		* ) echo "Invalid option passed to '$0' (options:$*)"
		usage;;
	esac
done

# Make sure user entered all required parameters
if [ -z "${ARCH}" ] || [ -z "${COMPRESS}" ] || [ -z "${LOAD_ADDR}" ] || \
	[ -z "${ENTRY_ADDR}" ] || [ -z "${VERSION}" ] || [ -z "${KERNEL}" ] || \
	[ -z "${OUTPUT}" ] || [ -z "${CONFIG}" ]; then
	usage
fi

ARCH_UPPER=$(echo "$ARCH" | tr '[:lower:]' '[:upper:]')

if [ -n "${COMPATIBLE}" ]; then
	COMPATIBLE_PROP="compatible = \"${COMPATIBLE}\";"
fi

[ "$FDTADDR" ] && {
	DTADDR="$FDTADDR"
}

# Conditionally create fdt information
if [ -n "${DTB}" ]; then
	FDT_NODE="
		fdt${REFERENCE_CHAR}$FDTNUM {
			description = \"${ARCH_UPPER} OpenWrt ${DEVICE} device tree blob\";
			${COMPATIBLE_PROP}
			data = /incbin/(\"${DTB}\");
			type = \"flat_dt\";
			${DTADDR:+load = <${DTADDR}>;}
			arch = \"${ARCH}\";
			compression = \"none\";
			hash {
				algo = \"crc32\";
			};
		};
"
	FDT_PROP="fdt = \"fdt${REFERENCE_CHAR}$FDTNUM\";"
fi

if [ -n "${INITRD}" ]; then
	INITRD_NODE="
		ramdisk {
			description = \"${ARCH_UPPER} OpenWrt ${DEVICE} initrd\";
			${COMPATIBLE_PROP}
			data = /incbin/(\"${INITRD}\");
			type = \"ramdisk\";
			arch = \"${ARCH}\";
			os = \"linux\";
			compression = \"none\";
			load = <0x0>;
			entry = <0x0>;
			hash {
				algo = \"sha1\";
			};
		};
"
	INITRD_PROP="ramdisk=\"ramdisk\";"
fi

# Create a default, fully populated DTS file
DATA="/dts-v1/;

/ {
	description = \"${ARCH_UPPER} OpenWrt FIT (Flattened Image Tree)\";
	#address-cells = <1>;

	build-info {
		build {
			description = \"atl-build-target\";
			target = \"arc\";
		};

		version {
			description = \"atl-build-version\";
			version = <9 9 9>;
			buildid = \"0000000000000000000000000000000000000000\";
		};
	};

	images {
		kernel {
			description = \"${ARCH_UPPER} OpenWrt Linux-${VERSION}\";
			data = /incbin/(\"${KERNEL}\");
			type = \"kernel\";
			arch = \"${ARCH}\";
			os = \"linux\";
			compression = \"${COMPRESS}\";
			load = <${LOAD_ADDR}>;
			entry = <${ENTRY_ADDR}>;
			hash {
				algo = \"sha1\";
			};
		};
${INITRD_NODE}
${FDT_NODE}
	};

	configurations {
		default = \"${CONFIG}\";
		${CONFIG} {
			description = \"${CONFIG} configuration\";
			kernel = \"kernel\";
			${FDT_PROP}
			${LOADABLES:+loadables = ${LOADABLES};}
			${COMPATIBLE_PROP}
			${INITRD_PROP}
		};
	};
};"

# Write .its file to disk
echo "$DATA" > "${OUTPUT}"
