#
# Samsung Galaxy Note II (GT-N7100) on the mainline Linux kernel
#

# Phone: the XMM6262 runs on the modem_if port, libsec-ril behind our AIDL radio HAL
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

$(call inherit-product, device/samsung/n7100/device.mk)

# Like LineageOS' own builds, userdebug isn't debuggable (vendor/lineage/config/common.mk sets
# PRODUCT_NOT_DEBUGGABLE_IN_USERDEBUG). Development builds patch that line out
# (patches/development/vendor_lineage-debuggable-userdebug.patch; it can't be cleared from here) and set
# N7100_DEVELOPMENT=true (device.mk).

PRODUCT_DEVICE := n7100
PRODUCT_NAME := lineage_n7100
PRODUCT_BRAND := samsung
PRODUCT_MANUFACTURER := samsung
PRODUCT_MODEL := GT-N7100

TARGET_SCREEN_HEIGHT := 1280
TARGET_SCREEN_WIDTH := 720
TARGET_BOOTANIMATION_HALF_RES := true
