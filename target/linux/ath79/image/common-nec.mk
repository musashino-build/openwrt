DEVICE_VARS += NEC_FW_TYPE

define Build/nec-append-uboot
  cat $(STAGING_DIR_IMAGE)/$(SOC)_nec_aterm-u-boot.bin >> $@
endef

define Build/nec-usbaterm-fw
  $(STAGING_DIR_HOST)/bin/nec-usbatermfw $@.new -t $(NEC_FW_TYPE) $(1)
  mv $@.new $@
endef

define Build/remove-uimage-header
  dd if=$@ of=$@.new iflag=skip_bytes skip=64 2>/dev/null
  mv $@.new $@
endef

define Device/nec-netbsd-aterm
  DEVICE_VENDOR := NEC
  LOADER_TYPE := bin
  KERNEL := kernel-bin | append-dtb | lzma | loader-kernel | uImage none
  KERNEL_INITRAMFS := kernel-bin | append-dtb | lzma | loader-kernel | uImage none
  ARTIFACTS := uboot.bin
ifneq ($(CONFIG_TARGET_ROOTFS_INITRAMFS),)
  COMPILE := loader-$(1).bin
  COMPILE/loader-$(1).bin := loader-okli-compile
  ARTIFACTS += initramfs-factory.bin initramfs-necboot.bin
  ARTIFACT/initramfs-factory.bin := append-image-stage initramfs-kernel.bin | \
	remove-uimage-header | \
	nec-usbaterm-fw -f 0x0003 -d $$(KDIR)/loader-$(1).bin -d $$$$@ | check-size
  ARTIFACT/initramfs-necboot.bin := append-image-stage initramfs-kernel.bin | \
	remove-uimage-header | nec-usbaterm-fw -d $$$$@
endif
  ARTIFACT/uboot.bin := nec-append-uboot | check-size 128k
  DEVICE_PACKAGES := kmod-usb2 -uboot-envtools
endef
