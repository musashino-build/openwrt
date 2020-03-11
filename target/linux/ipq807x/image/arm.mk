define Device/buffalo_wxr-5950ax12
	$(call Device/FitImage)
	SOC := ipq8078
	DEVICE_VENDOR := Buffalo
	DEVICE_MODEL := WXR-5950AX12
	BLOCKSIZE := 128k
	PAGESIZE := 2048
	KERNEL_IN_UBI := 1
	IMAGES := sysupgrade.bin
endef
TARGET_DEVICES += buffalo_wxr-5950ax12
