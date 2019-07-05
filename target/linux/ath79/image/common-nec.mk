DEVICE_VARS += KERNEL_INITRAMFS_PREFIX

define Build/add-nec-initramfs-image
  $(eval board=$(1))
  # lzma-loader
  rm -rf $@.src
  $(MAKE) -C lzma-loader PKG_BUILD_DIR="$@.src" \
    TARGET_DIR="$(dir $@)" LOADER_NAME="$(notdir $@)" \
    LOADER_DATA="$@" BOARD="$(board)" \
    LZMA_TEXT_START=0x82000000 LOADADDR=0x80060000 \
    compile loader.bin
  mv "$@.bin" "$@.necimg"
  rm -rf $@.src
  # padding image
  dd if=$@.necimg of=$@.necimg.pad bs=4 conv=sync
  # add nec header
  ( \
    nec_fw_size=$$(printf '%08x' "$$(($$(stat -c%s $@.necimg.pad) + 0x18))"); \
    echo -ne $$(echo "0002FFFD$${nec_fw_size}00000018000000008006000080060000" | \
      sed 's/../\\x&/g'); \
    dd if=$@.necimg.pad; \
  ) > $@.necimg
  # calcurate and add checksum
  ( \
    cksum=$$( \
      dd if=$@.necimg ibs=4 skip=1 | od -A n -t u2 | tr -s ' ' '\n' | \
        awk '{s+=$$0}END{printf "%04x", 0xffff-(s%0x100000000)%0xffff}'); \
    echo -ne "\x$${cksum:2:2}\x$${cksum:0:2}" | \
      dd of=$@.necimg conv=notrunc bs=1 seek=12 count=2; \
  )
  mv $@.necimg $(BIN_DIR)/$(KERNEL_INITRAMFS_PREFIX)-necimg.bin
endef

define Device/nec-netbsd-aterm
  DEVICE_VENDOR := NEC
  KERNEL_INITRAMFS := kernel-bin | append-dtb | lzma | \
    add-nec-initramfs-image $(subst nec_,,$(1)) | uImage lzma
  DEVICE_PACKAGES := kmod-usb2
endef
