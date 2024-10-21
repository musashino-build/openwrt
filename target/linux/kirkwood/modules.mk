define KernelPackage/ata-marvell-sata
  TITLE:=Marvell Serial ATA support
  DEPENDS:=@TARGET_kirkwood
  KCONFIG:=CONFIG_SATA_MV CONFIG_SATA_PMP=y
  FILES:=$(LINUX_DIR)/drivers/ata/sata_mv.ko
  AUTOLOAD:=$(call AutoLoad,41,sata_mv,1)
  $(call AddDepends/ata)
endef

define KernelPackage/ata-marvell-sata/description
 SATA support for marvell chipsets
endef

$(eval $(call KernelPackage,ata-marvell-sata))

define KernelPackage/input-landisk-r8c-keys
  SUBMENU:=Input modules
  TITLE:=R8C keys driver for I-O DATA LAN DISK series NAS
  DEPENDS:=+kmod-input-core @TARGET_kirkwood_nas
  KCONFIG:=CONFIG_INPUT_LANDISK_R8C_KEYS
  FILES:=$(LINUX_DIR)/drivers/input/misc/landisk-r8c-keys.ko
  AUTOLOAD:=$(call AutoProbe,landisk-r8c-keys,1)
endef

define KernelPackage/input-landisk-r8c-keys/description
 Kernel Support for the buttons provided by R8C MCU on
 I-O DATA LAN DISK series NAS.
endef

$(eval $(call KernelPackage,input-landisk-r8c-keys))

define KernelPackage/leds-landisk-r8c
  SUBMENU:=LED modules
  TITLE:=R8C LEDs driver for I-O DATA LAN DISK series NAS
  DEPENDS:=@TARGET_kirkwood_nas
  KCONFIG:=CONFIG_LEDS_LANDISK_R8C
  FILES:=$(LINUX_DIR)/drivers/leds/leds-landisk-r8c.ko
  AUTOLOAD:=$(call AutoProbe,leds-landisk-r8c,1)
endef

define KernelPackage/leds-landisk-r8c/description
 Kernel support for the LEDs provided by R8C MUC on
 I-O DATA LAN DISK series NAS.
endef

$(eval $(call KernelPackage,leds-landisk-r8c))

define KernelPackage/mvsdio
  SUBMENU:=$(OTHER_MENU)
  TITLE:=Marvell MMC/SD/SDIO host driver
  DEPENDS:=+kmod-mmc @TARGET_kirkwood
  KCONFIG:= CONFIG_MMC_MVSDIO
  FILES:= \
	$(LINUX_DIR)/drivers/mmc/host/mvsdio.ko
  AUTOLOAD:=$(call AutoProbe,mvsdio,1)
endef

define KernelPackage/mvsdio/description
 Kernel support for the Marvell SDIO host driver.
endef

$(eval $(call KernelPackage,mvsdio))
