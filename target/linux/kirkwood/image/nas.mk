define Build/boot-image-ext3
	$(eval boot_kernel=$(if $(word 1,$(1)),$(word 1,$(1)),uImage))
	$(eval boot_initrd=$(word 2,$(1)))

	$(RM) -rf $@.bootdir $@.boot
	mkdir -p $@.bootdir/$(dir $(boot_kernel))

	$(CP) $(IMAGE_KERNEL) $@.bootdir/$(boot_kernel)
	$(if $(boot_initrd),\
		$(CP) $(KDIR)/$(DEVICE_NAME).initrd $@.bootdir/$(boot_initrd))

	genext2fs --block-size 1Ki \
		--size-in-blocks $$((1024 * $(CONFIG_TARGET_KERNEL_PARTSIZE))) \
		--root $@.bootdir $@.boot

	# convert it to revision 1 - needed for u-boot ext2load
	$(STAGING_DIR_HOST)/bin/tune2fs -j $@.boot
	$(STAGING_DIR_HOST)/bin/e2fsck -pDf $@.boot > /dev/null
endef

define Build/boot-image-fat
	$(eval boot_kernel=$(if $(word 1,$(1)),$(word 1,$(1)),uImage))
	$(eval boot_initrd=$(word 2,$(1)))

	rm -f $@.boot
	mkfs.fat -C $@.boot $$(( $(CONFIG_TARGET_KERNEL_PARTSIZE) * 1024 ))

	mmd -i $@.boot ::$(patsubst %/,%,$(dir $(boot_kernel))) || :
	mcopy -i $@.boot $(IMAGE_KERNEL) ::$(boot_kernel)
	$(if $(boot_initrd),\
		-mcopy -i $@.boot $(KDIR)/$(DEVICE_NAME).initrd ::$(boot_initrd))
endef

define Build/disk-image
	( \
		set $$(ptgen -o $@.new \
			-l 1024 -h 16 -s 63 $(1) \
			-t 0x83 -N kernel -r -p $(CONFIG_TARGET_KERNEL_PARTSIZE)M \
			-t 0x83 -N rootfs -r -p $(CONFIG_TARGET_ROOTFS_PARTSIZE)M);\
		\
		mv $@.new $@; \
		\
		dd if=/dev/zero of=$@ bs=1k seek=$$(( $$3 / 1024)) \
			count=$$(( $$4 / 1024)) conv=notrunc; \
		dd if=$@.boot of=$@ bs=1k \
			seek=$$(( $$1 / 1024)) conv=notrunc; \
		dd if=$(IMAGE_ROOTFS) of=$@ bs=1k \
			seek=$$(( $$3 / 1024)) conv=notrunc; \
	)
endef

define Device/iodata_landisk
  DEVICE_VENDOR := I-O DATA
  DEVICE_PACKAGES := kmod-iodata-landisk-reboot kmod-rtc-rs5c372a
  FILESYSTEMS := ext4
  COMPILE := $$(DEVICE_NAME).initrd
  COMPILE/$$(DEVICE_NAME).initrd := pad-extra 4
  IMAGES := disk.img.gz
endef

define Device/iodata_hdl-a-sata
  $(Device/iodata_landisk)
  DEVICE_MODEL := HDL-A
  DEVICE_VARIANT := (SATA)
  IMAGE/disk.img.gz := boot-image-ext3 uImage.l2a initrd.l2a | \
	disk-image -g | gzip | append-metadata
endef
TARGET_DEVICES += iodata_hdl-a-sata

define Device/iodata_hdl-a-usb
  $(Device/iodata_landisk)
  DEVICE_MODEL := HDL-A
  DEVICE_VARIANT := (USB)
  IMAGE/disk.img.gz := boot-image-fat l2a/uImage.l2a l2a/initrd.l2a | \
	disk-image | gzip | append-metadata
endef
TARGET_DEVICES += iodata_hdl-a-usb

define Device/iodata_hdl2-a-sata
  $(Device/iodata_landisk)
  DEVICE_MODEL := HDL2-A
  DEVICE_VARIANT := (SATA)
  IMAGE/disk.img.gz := boot-image-ext3 uImage.l2a initrd.l2a | \
	disk-image -g | gzip | append-metadata
endef
TARGET_DEVICES += iodata_hdl2-a-sata

define Device/iodata_hdl2-a-usb
  $(Device/iodata_landisk)
  DEVICE_MODEL := HDL2-A
  DEVICE_VARIANT := (USB)
  IMAGE/disk.img.gz := boot-image-fat l2a/uImage.l2a l2a/initrd.l2a | \
	disk-image | gzip | append-metadata
endef
TARGET_DEVICES += iodata_hdl2-a-usb
