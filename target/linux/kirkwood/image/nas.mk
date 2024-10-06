define Build/boot-image-ext3
	$(eval boot_kernel=$(if $(word 1,$(1)),$(word 1,$(1)),uImage))
	$(eval boot_initrd=$(word 2,$(1)))

	$(RM) -rf $@.bootdir
	mkdir -p $@.bootdir/$(dir $(boot_kernel))

	$(CP) $(IMAGE_KERNEL) $@.bootdir/$(boot_kernel)
	$(if $(boot_initrd),\
		$(CP) $(KDIR)/$(DEVICE_NAME).initrd $@.bootdir/$(boot_initrd))

	genext2fs --block-size $(BLOCKSIZE:%k=%Ki) \
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

	mmd -i $@.boot ::$(patsubst %/,%,$(dir $(boot_kernel))) || true
	mcopy -i $@.boot $(IMAGE_KERNEL) ::$(boot_kernel)
	$(if $(boot_initrd),\
		-mcopy -i $@.boot $(KDIR)/$(DEVICE_NAME).initrd ::$(boot_initrd))
endef

define Build/disk-image
	$(eval result=$(shell ptgen $(filter-out disk-image,$(1)) -o $@.new -l 1024 \
		-t 0x83 -N kernel -r -p $(CONFIG_TARGET_KERNEL_PARTSIZE)M \
		-t 0x83 -N rootfs -r -p $(CONFIG_TARGET_ROOTFS_PARTSIZE)M))

	mv $@.new $@

	dd if=$@.boot of=$@ bs=1k \
		seek=$$(($(word 1,$(result)) / 1024)) conv=notrunc
	dd if=$(IMAGE_ROOTFS) of=$@ bs=1k \
		seek=$$(($(word 3,$(result)) / 1024)) conv=notrunc
endef
