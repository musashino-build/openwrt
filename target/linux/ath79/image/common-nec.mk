DEVICE_VARS += NEC_FW_TYPE

define Build/nec-append-uboot
  cat $(STAGING_DIR_IMAGE)/$(SOC)_nec_aterm-u-boot.bin >> $@
endef

define Build/nec-boot-header
  $(eval flags=$(if $(1),$(word 1,$(1)),0x0002))
  $(eval addr=$(if $(word 2,$(1)),$(word 2,$(1)),80060000))
  $(eval databin=$(if $(word 3,$(1)),$(word 3,$(1)),$@))
  ( \
    flags=$$(printf "%04x%04x" $(flags) $$((~$(flags) & 0xffff))); \
    nec_fw_size=$$(printf '%08x' "$$(($$(stat -c%s $(databin)) + 0x18))"); \
    printf $$(echo "$${flags}$${nec_fw_size}0000001800000000$(addr)$(addr)" | \
      sed 's/../\\x&/g'); \
    cat $(databin); \
  ) > $(databin).new
  mv $(databin).new $(databin)
  ( \
    cksum=$$( \
      dd if=$(databin) ibs=4 skip=1 | od -A n -t u2 --endian=little | \
        tr -s ' ' '\n' | \
        awk '{s+=$$0}END{printf "%04x", 0xffff-(s%0x100000000)%0xffff}'); \
    printf "\x$${cksum:2:2}\x$${cksum:0:2}" | \
      dd of=$(databin) conv=notrunc bs=2 seek=6 count=1; \
  )
endef

define Build/nec-usbaterm-fw
  ( \
    printf "VERSION: 9.99.99\nOEM1 VERSION: 9.9.99\n"; \
    printf "$(call toupper,$(LINUX_KARCH)) $(VERSION_DIST) Linux-$(LINUX_VERSION)\n"; \
  ) | dd of=$@.copyblk bs=4 conv=sync
  touch $@.endblk
  $(call Build/nec-boot-header,0x0000 00000000 $@.copyblk)
  $(call Build/nec-boot-header,0x0001 00000000 $@.endblk)
  ( \
    printf "USB ATERMWL3050\x00"; \
    printf $$(echo "ffffffffffffffffffffffffffffffff" | sed 's/../\\x&/g'); \
    cat $@.copyblk $@ "$(KDIR)/loader-$(DEVICE_NAME).bin" $@.endblk; \
  ) > $@.new
  printf "Binary Type$(1) File END \r\n" > $@.footer
  ( \
    pad=$$((0x20 - $$(stat -c%s $@.footer))); \
    for i in $$(seq 1 $$pad); do printf "\xff"; done; \
    cat $@.footer; \
  ) >> $@.new
  mv $@.new $@
  rm -f $@.copyblk $@.endblk $@.footer
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
  COMPILE/loader-$(1).bin := loader-okli-compile | pad-to 4 | nec-boot-header
  ARTIFACTS += initramfs-factory.bin initramfs-necboot.bin
  ARTIFACT/initramfs-factory.bin := append-image-stage initramfs-kernel.bin | \
	remove-uimage-header | pad-to 4 | nec-boot-header 0003 | \
	nec-usbaterm-fw $$$$(NEC_FW_TYPE) | check-size
  ARTIFACT/initramfs-necboot.bin := append-image-stage initramfs-kernel.bin | \
	remove-uimage-header | pad-to 4 | nec-boot-header
endif
  ARTIFACT/uboot.bin := nec-append-uboot | check-size 128k
  DEVICE_PACKAGES := kmod-usb2 -uboot-envtools
endef
