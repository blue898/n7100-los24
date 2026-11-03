LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),n7100)

# Two-stage recovery (mkbootimg.mk): the uncompressed recovery ramdisk, which the loader in
# recovery.img unpacks from /vendor/etc/recovery
include $(CLEAR_VARS)
LOCAL_MODULE := n7100_recovery_ramdisk
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0
LOCAL_LICENSE_CONDITIONS := notice
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_STEM := ramdisk.cpio
LOCAL_MODULE_RELATIVE_PATH := recovery
LOCAL_VENDOR_MODULE := true
LOCAL_PREBUILT_MODULE_FILE := $(PRODUCT_OUT)/ramdisk-recovery.cpio
include $(BUILD_PREBUILT)

endif
