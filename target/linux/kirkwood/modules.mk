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

define KernelPackage/iodata-landisk-reboot
  SUBMENU:=$(OTHER_MENU)
  TITLE:=I-O DATA LAN DISK series reboot driver
  DEPENDS:=@TARGET_kirkwood
  KCONFIG:= CONFIG_POWER_RESET_IODATA_LANDISK
  FILES:= \
	$(LINUX_DIR)/drivers/power/reset/iodata-landisk-reboot.ko
  AUTOLOAD:=$(call AutoProbe,iodata-landisk-reboot,1)
endef

define KernelPackage/iodata-landisk-reboot/description
 Reboot/Power-off support for I-O DATA LAN DISK series NAS.
endef

$(eval $(call KernelPackage,iodata-landisk-reboot))
