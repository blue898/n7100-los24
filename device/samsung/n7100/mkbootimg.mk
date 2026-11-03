# Boot and recovery images (BOARD_CUSTOM_BOOTIMG_MK replaces the build system's rules for both).
#
# boot.img: as the build system makes it.
# recovery.img: two-stage. The 8 MiB RECOVERY partition holds the kernel (6 MiB) but not the
# recovery ramdisk next to it (9 MiB, xz), so the image carries the kernel with a loader as its
# ramdisk (recovery/loader.c). The recovery ramdisk goes to the vendor partition uncompressed
# (n7100_recovery_ramdisk, Android.mk), where the loader unpacks it from at boot.

$(INSTALLED_BOOTIMAGE_TARGET): $(MKBOOTIMG) $(INTERNAL_BOOTIMAGE_FILES) $(call bootimage-to-kernel,$(INSTALLED_BOOTIMAGE_TARGET))
	$(call pretty,"Target boot image: $@")
	$(MKBOOTIMG) --kernel $(call bootimage-to-kernel,$@) $(INTERNAL_BOOTIMAGE_ARGS) \
	    $(INTERNAL_MKBOOTIMG_VERSION_ARGS) $(BOARD_MKBOOTIMG_ARGS) --output $@
	$(call assert-max-image-size,$@,$(BOARD_BOOTIMAGE_PARTITION_SIZE))

N7100_RECOVERY_LOADER := $(TARGET_RECOVERY_ROOT_OUT)/system/bin/n7100_recovery_loader
N7100_RECOVERY_LOADER_DIR := $(call intermediates-dir-for,PACKAGING,n7100_recovery_loader)
N7100_RECOVERY_LOADER_RAMDISK := $(N7100_RECOVERY_LOADER_DIR)/ramdisk.cpio.xz

$(N7100_RECOVERY_LOADER_RAMDISK): $(N7100_RECOVERY_LOADER) $(MKBOOTFS) $(COMPRESSION_COMMAND_DEPS)
	rm -rf $(N7100_RECOVERY_LOADER_DIR)/root
	mkdir -p $(N7100_RECOVERY_LOADER_DIR)/root
	cp $(N7100_RECOVERY_LOADER) $(N7100_RECOVERY_LOADER_DIR)/root/init
	$(MKBOOTFS) $(N7100_RECOVERY_LOADER_DIR)/root | $(COMPRESSION_COMMAND) > $@

$(INSTALLED_RECOVERYIMAGE_TARGET): $(MKBOOTIMG) $(recovery_kernel) $(N7100_RECOVERY_LOADER_RAMDISK)
	$(call pretty,"Target recovery image (kernel and recovery loader): $@")
	$(MKBOOTIMG) $(subst --ramdisk $(recovery_ramdisk),--ramdisk $(N7100_RECOVERY_LOADER_RAMDISK),$(INTERNAL_RECOVERYIMAGE_ARGS)) \
	    $(INTERNAL_MKBOOTIMG_VERSION_ARGS) $(BOARD_RECOVERY_MKBOOTIMG_ARGS) --output $@
	$(call assert-max-image-size,$@,$(BOARD_RECOVERYIMAGE_PARTITION_SIZE))
