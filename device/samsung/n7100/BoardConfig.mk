#
# Samsung Galaxy Note II (GT-N7100), Exynos 4412, mainline kernel
#

DEVICE_PATH := device/samsung/n7100

# Architecture
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := cortex-a9
TARGET_CPU_SMP := true

# Platform
TARGET_BOARD_PLATFORM := exynos4
TARGET_BOOTLOADER_BOARD_NAME := n7100
TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true

# Kernel: mainline 7.1 (exynos4-mainline) built outside the tree, zImage with the N7100
# EA8061 DTB appended (Samsung's sboot has no DT support). Android's boot parameters are
# compiled into the kernel (CONFIG_CMDLINE_EXTEND): sboot ignores the boot.img cmdline.
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilt/kernel/zImage-dtb
BOARD_KERNEL_BASE := 0x40000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_IMAGE_NAME := zImage
BOARD_BOOT_HEADER_VERSION := 0
BOARD_MKBOOTIMG_ARGS := --header_version $(BOARD_BOOT_HEADER_VERSION)
# kernel + dtb take ~6.8 MiB of the 8 MiB BOOT partition
BOARD_RAMDISK_USE_XZ := true
BOARD_VENDOR_KERNEL_MODULES := $(wildcard $(DEVICE_PATH)/prebuilt/modules/*.ko)

# Recovery: LineageOS recovery, two-stage (mkbootimg.mk). recovery.img holds the kernel and a
# loader, the recovery ramdisk is in the vendor partition. Of its drivers only the keys' are
# modules.
BOARD_CUSTOM_BOOTIMG_MK := $(DEVICE_PATH)/mkbootimg.mk
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 8388608
# The flash zip updates the recovery with the ROM only where it finds this name in the header
BOARD_RECOVERY_MKBOOTIMG_ARGS := --header_version $(BOARD_BOOT_HEADER_VERSION) --board n7100-2stage
BOARD_RECOVERY_KERNEL_MODULES := \
    $(addprefix $(DEVICE_PATH)/prebuilt/modules/,gpio_keys.ko led-class.ko tm2-touchkey.ko)
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/fstab.n7100
# XRGB8888, which the FIMD scans out; without it minui allocates RGB565 buffers, draws 4 bytes
# per pixel into them anyway and crashes
TARGET_RECOVERY_PIXEL_FORMAT := BGRX_8888
# Flash zips made for TWRP (ours too) run their update-binary with #!/sbin/sh
BOARD_RECOVERY_IMAGE_PREPARE = \
    mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/sbin && ln -sf /system/bin/sh $(TARGET_RECOVERY_ROOT_OUT)/sbin/sh

# Partitions (stock Samsung GPT: SYSTEM, HIDDEN as vendor, USERDATA)
BOARD_FLASH_BLOCK_SIZE := 4096
BOARD_BOOTIMAGE_PARTITION_SIZE := 8388608
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 2147483648
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_VENDORIMAGE_PARTITION_SIZE := 587202560
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_USERDATAIMAGE_PARTITION_SIZE := 11408506880
TARGET_COPY_OUT_VENDOR := vendor
TARGET_USERIMAGES_USE_EXT4 := true
BOARD_ROOT_EXTRA_FOLDERS := efs
# /metadata (aconfigd flag storage) on the OTA partition, see rootdir/fstab.n7100
BOARD_USES_METADATA_PARTITION := true

# Graphics: Mesa lima (Mali-400 MP4) + kmsro for the exynos-drm display, built with
# GloDroid's aospext (external/aospext); drm_hwcomposer + minigbm (dumb buffers on exynos)
BOARD_BUILD_AOSPEXT_MESA3D := true
BOARD_MESA3D_SRC_DIR := external/mesa3d
BOARD_MESA3D_GALLIUM_DRIVERS := lima
BOARD_MESA3D_VULKAN_DRIVERS :=
BOARD_MESA3D_BUILD_LIBGBM := true
# aospext builds meson projects (Mesa) as "debug" by default: -O0, asserts, MESA_DEBUG
AOSPEXT_MESON_BUILDTYPE := release

# Wi-Fi: BCM4334 on mainline brcmfmac, plain nl80211. The Wi-Fi HAL needs a vendor library or
# it refuses to start; the emulator's (goldfish libwifi-hal-emu) only talks generic netlink/nl80211
BOARD_WLAN_DEVICE := emulator
WPA_SUPPLICANT_VERSION := VER_0_8_X
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_HOSTAPD_DRIVER := NL80211
# private "DRIVER ..." commands (COUNTRY, MACADDR) over plain nl80211, see wpa_supplicant_8_lib/
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_n7100
WIFI_HIDL_UNIFIED_SUPPLICANT_SERVICE_RC_ENTRY := true
WIFI_HAL_INTERFACE_COMBINATIONS := {{{STA}, 1}, {{AP}, 1}}

# SELinux: enforcing (sboot passes androidboot.selinux=enforcing)
BOARD_VENDOR_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy/vendor
SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy/private

# VINTF
DEVICE_MANIFEST_FILE := $(DEVICE_PATH)/manifest.xml

BUILD_BROKEN_DUP_RULES := true
BUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES := true
