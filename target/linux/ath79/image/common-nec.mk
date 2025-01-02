DEVICE_VARS += NEC_FW_TYPE

define Build/loader-dummy
	$(call Build/loader-common,BOARD="$(DEVICE_NAME)" )
endef

define Build/nec-usbaterm-fw
  $(STAGING_DIR_HOST)/bin/nec-usbatermfw $@.new -t $(NEC_FW_TYPE) $(1)
  mv $@.new $@
endef

define Build/nec-systemfs-header
  $(eval dlen=$(shell stat -c%s $@))
  ( \
    printf "Firmware\x00\xff\xff\xff\xff\xff\xff\xff";
    printf "$$(printf "00000000%08x30534654ffffffff" $(dlen) | sed 's/../\\x&/g')";\
    dd if=/dev/zero bs=32 count=1 2>/dev/null | tr "\0" "\377"; \
    cat $@; \
  ) > $@.new
  mv $@.new $@
endef

define Device/nec-netbsd-aterm
  DEVICE_VENDOR := NEC
  LOADER_TYPE := bin
  KERNEL := kernel-bin | append-dtb | lzma | loader-kernel | uImage none
  KERNEL_INITRAMFS := kernel-bin | append-dtb | lzma | loader-kernel | uImage none
  LOADER_FLASH_OFFS := 0x50040
  COMPILE := loader-$(1)_wdt.bin loader-$(1)_flboot.bin
  COMPILE/loader-$(1)_wdt.bin := loader-dummy
  COMPILE/loader-$(1)_flboot.bin := loader-okli-compile
  IMAGE/sysupgrade.bin := \
	nec-usbaterm-fw -f 0x0003 -d $$(KDIR)/loader-$(1)_wdt.bin \
		-d $$(KDIR)/loader-$(1)_flboot.bin | \
	nec-systemfs-header | pad-to $$$$(BLOCKSIZE) | pad-extra 64 | \
	append-kernel | pad-to $$$$(BLOCKSIZE) | append-rootfs | pad-rootfs | \
	check-size | append-metadata
  ARTIFACTS := uboot.bin
ifneq ($(CONFIG_TARGET_ROOTFS_INITRAMFS),)
  ARTIFACTS += initramfs-factory.bin
  ARTIFACT/initramfs-factory.bin := append-image-stage initramfs-kernel.bin | \
	pad-to 4 skip=16 | \
	nec-usbaterm-fw -f 0x0003 -d $$(KDIR)/loader-$(1).bin -d $$$$@ | check-size
endif
  UBOOT_PATH := $$(STAGING_DIR_IMAGE)/$$(SOC)_nec_aterm-u-boot.bin
  ARTIFACT/uboot.bin := append-uboot | check-size 128k
  DEVICE_PACKAGES := kmod-usb2 -uboot-envtools
endef
