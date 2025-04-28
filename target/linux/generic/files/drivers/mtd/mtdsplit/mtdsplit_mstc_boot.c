// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * a mtdsplit parser using "bootnum" value in the "persist" partition
 * for the devices manufactured by MSTC (MitraStar Technology Corp.)
 */
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/types.h>
#include <linux/byteorder/generic.h>
#include <linux/slab.h>
#include <linux/libfdt.h>
#include <linux/of_fdt.h>
#include <dt-bindings/mtd/partitions/uimage.h>

#include "mtdsplit.h"

#define PERSIST_BOOTNUM_OFFSET	0x4

/*
 * Legacy format image header,
 * all data in network byte order (aka natural aka bigendian).
 */
 struct uimage_header {
	uint32_t	ih_magic;	/* Image Header Magic Number	*/
	uint32_t	ih_hcrc;	/* Image Header CRC Checksum	*/
	uint32_t	ih_time;	/* Image Creation Timestamp	*/
	uint32_t	ih_size;	/* Image Data Size		*/
	uint32_t	ih_load;	/* Data	 Load  Address		*/
	uint32_t	ih_ep;		/* Entry Point Address		*/
	uint32_t	ih_dcrc;	/* Image Data CRC Checksum	*/
	uint8_t		ih_os;		/* Operating System		*/
	uint8_t		ih_arch;	/* CPU architecture		*/
	uint8_t		ih_type;	/* Image Type			*/
	uint8_t		ih_comp;	/* Compression Type		*/
	uint8_t		ih_name[IH_NMLEN];	/* Image Name		*/
};

/* check whether the current mtd device is active or not */
static int
mstcboot_is_active(struct mtd_info *mtd)
{
	struct device_node *np = mtd_get_of_node(mtd);
	struct device_node *persist_np;
	size_t retlen;
	u_char bootnum;
	u32 bootnum_dt, persist_offset;
	int ret;

	ret = of_property_read_u32(np, "mstc,bootnum", &bootnum_dt);
	if (ret)
		return ret;

	persist_np = of_parse_phandle(np, "mstc,persist", 0);
	if (!persist_np)
		return -ENODATA;
	/* is "persist" under the same node? */
	if (persist_np->parent != np->parent)
		return -EINVAL;
	ret = of_property_read_u32(persist_np, "reg", &persist_offset);
	of_node_put(persist_np);
	if (ret)
		return ret;
	ret = mtd_read(mtd->parent, persist_offset + PERSIST_BOOTNUM_OFFSET,
		       1, &retlen, &bootnum);
	if (ret)
		return ret;
	if (retlen != 1)
		return -EIO;

	if (bootnum == 1 || bootnum == 2)
		return (bootnum == bootnum_dt) ? 1 : 0;

	pr_err("invalid bootnum detected within persist! (0x%x)\n", bootnum);
	return -EINVAL;
}

static int
mtdsplit_mstcboot_parse(struct mtd_info *mtd,
			const struct mtd_partition **pparts,
			struct mtd_part_parser_data *data)
{
	struct device_node *np = mtd_get_of_node(mtd);
	u32 offset = 0;
	size_t retlen;
	size_t kern_len = 0;
	size_t rootfs_offset;
	struct mtd_partition *parts;
	enum mtdsplit_part_type type;
	u_char buf[0x40];
	int ret, nr_parts, index = 0;


	ret = mstcboot_is_active(mtd);
	if (ret < 0)
		return ret;
	else if (ret == 0)
		return -ENODEV;

	of_property_read_u32(np, "mstc,kernel-offset", &offset);

	ret = mtd_read(mtd, offset, sizeof(struct uimage_header), &retlen, buf);
	if (ret)
		return ret;
	if (retlen != sizeof(struct uimage_header))
		return -EIO;

	if (be32_to_cpu(*(u32 *)buf) == OF_DT_HEADER) {
		/* Flattened Image Tree (FIT) */
		struct fdt_header *fdthdr = (void *)buf;

		kern_len = offset + be32_to_cpu(fdthdr->totalsize);
	} else {
		/* Legacy uImage */
		struct uimage_header *uimghdr = (void *)buf;
		u32 magic_dt;

		of_property_read_u32(np, "mstc,kernel-magic", &magic_dt);

		if (be32_to_cpu(uimghdr->ih_magic) == IH_MAGIC ||
		    (magic_dt && be32_to_cpu(uimghdr->ih_magic) == magic_dt))
			kern_len = offset + sizeof(*uimghdr)
					+ be32_to_cpu(uimghdr->ih_size);
	}

	nr_parts = (kern_len > 0) ? 2 : 1;

	ret = mtd_find_rootfs_from(mtd, kern_len, mtd->size, &rootfs_offset, &type);
	if (ret) {
		pr_debug("no rootfs after kernel in \"%s\"\n", mtd->name);
		return ret;
	}

	parts = kcalloc(nr_parts, sizeof(*parts), GFP_KERNEL);
	if (!parts)
		return -ENOMEM;

	if (kern_len) {
		parts[index].name = KERNEL_PART_NAME;
		parts[index].offset = 0;
		parts[index++].size = rootfs_offset;
	}

	parts[index].name = (type == MTDSPLIT_PART_TYPE_UBI)
				? UBI_PART_NAME : ROOTFS_PART_NAME;
	parts[index].offset = rootfs_offset;
	parts[index].size = mtd->size - rootfs_offset;

	*pparts = parts;
	return nr_parts;
}

static const struct of_device_id mtdsplit_mstcboot_of_match_table[] = {
	{ .compatible = "mstc,boot" },
	{},
};
MODULE_DEVICE_TABLE(of, mtdsplit_mstcboot_of_match_table);

static struct mtd_part_parser mtdsplit_mstcboot_parser = {
	.owner = THIS_MODULE,
	.name = "mstc-boot",
	.of_match_table = mtdsplit_mstcboot_of_match_table,
	.parse_fn = mtdsplit_mstcboot_parse,
	.type = MTD_PARSER_TYPE_FIRMWARE,
};
module_mtd_part_parser(mtdsplit_mstcboot_parser)
