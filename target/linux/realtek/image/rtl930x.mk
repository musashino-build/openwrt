# SPDX-License-Identifier: GPL-2.0-only

define Build/xikestor-nosimg
  ( \
    patterns="EEDDCC215355EECCDD55807E00000000CDBDDFAEBB9B890170E5CCDDF6FC8364 \
              ECDDCEF1E354FED0BDABDDE1E4B4D583EDFED0CDB655CCA3EDD5C67EDDCC2153 \
              EC4DDC005355CDC32201807EEFBC7566A6C0CC2FFED0EECCDD5501010101C564 \
              9945AB3255807EEF55807EEFBC756689E31D83DDFE558EAB7D55807EFF01AC66 \
              0EC992D973E50101BDE510CE0101BAE83EDD81A1533301019AC510AA01CE8AE1 \
              B1FB00805377000070DC00010000CBB1A030000055A60000CABD01010000C9B2 \
              819001005A21000179BC010078007BB3D49701005355A9FCDDA501BEAFC175C5 \
              8ED7770055D00DAC0155807EEFBC7EE6F16C5200331698CC0101010100007988"; \
    i=0; \
    for ptn in $$patterns $$patterns; do \
      echo "$$i: $$ptn"; \
      dd if=$@ of=$@.blk$${i} bs=$$(($${#ptn} / 2)) count=1 skip=$$i 2>/dev/null; \
      $(STAGING_DIR_HOST)/bin/xorimage -i $@.blk$${i} -o $@.blk$${i}.xor -p $$ptn -x; \
      i=$$((i + 1)); \
    done; \
  )
  cat $$(ls $@.blk*.xor | sort -V) > $@.new
  dd if=$@ ibs=$$((0x200)) skip=1 >> $@.new
  mv $@.new $@
  rm -f $@.blk*
endef

define Device/xikestor_sks8300-8x
  SOC := rtl9303
  DEVICE_VENDOR := XikeStor
  DEVICE_MODEL := SKS8300-8X
  BLOCKSIZE := 64k
  KERNEL_SIZE := 8192k
  IMAGE_SIZE := 30720k
  IMAGE/sysupgrade.bin := pad-extra 256 | append-kernel | xikestor-nosimg | \
	jffs2 nos.img | pad-to $$$$(KERNEL_SIZE) | append-rootfs | pad-rootfs | \
	append-metadata | check-size
  ARTIFACTS := nos.img
  ARTIFACT/nos.img := pad-extra 256 | append-image-stage initramfs-kernel.bin | \
	xikestor-nosimg
endef
TARGET_DEVICES += xikestor_sks8300-8x

define Device/zyxel_xgs1250-12
  SOC := rtl9302
  UIMAGE_MAGIC := 0x93001250
  ZYXEL_VERS := ABWE
  DEVICE_VENDOR := Zyxel
  DEVICE_MODEL := XGS1250-12
  DEVICE_PACKAGES := kmod-hwmon-gpiofan kmod-thermal
  IMAGE_SIZE := 13312k
  KERNEL_INITRAMFS := \
	kernel-bin | \
	append-dtb | \
	gzip | \
	zyxel-vers | \
	uImage gzip
endef
TARGET_DEVICES += zyxel_xgs1250-12
