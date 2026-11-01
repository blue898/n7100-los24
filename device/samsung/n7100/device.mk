#
# Samsung Galaxy Note II (GT-N7100) on the mainline Linux kernel
#

DEVICE_PATH := device/samsung/n7100

# 2 GB RAM: Dalvik heap sizes (without them the 16 MB default makes SystemUI die of OOM)
$(call inherit-product, frameworks/native/build/phone-xhdpi-2048-dalvik-heap.mk)
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := xhdpi

# Hardware/software features. Without handheld_core_hardware.xml there is no
# android.software.app_widgets, so system_server skips AppWidgetService and Launcher3 crashes on start
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/handheld_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/handheld_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml

# Wi-Fi (brcmfmac + wpa_supplicant over nl80211)
PRODUCT_PACKAGES += \
    android.hardware.wifi-service \
    wpa_supplicant
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    $(DEVICE_PATH)/rootdir/wifi/wpa_supplicant.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant.conf \
    external/wireless-regdb/regulatory.db:$(TARGET_COPY_OUT_VENDOR)/firmware/regulatory.db \
    external/wireless-regdb/regulatory.db.p7s:$(TARGET_COPY_OUT_VENDOR)/firmware/regulatory.db.p7s

# Bluetooth: the kernel's hci_uart/btbcm driver owns the BCM4334; AOSP's default HAL talks to
# hci0 over an HCI user channel
PRODUCT_PACKAGES += \
    android.hardware.bluetooth-service.default
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml

# Lights: key backlight + notification LED; vibrator: max77693 haptic motor
PRODUCT_PACKAGES += \
    android.hardware.lights-service.n7100 \
    android.hardware.vibrator-service.n7100

# Sensors: SSP sensor hub (accelerometer, gyroscope, magnetometer, barometer, light, proximity)
PRODUCT_PACKAGES += \
    android.hardware.sensors-service.n7100

# Camera: rear S5C73M3 through FIMC0 (camera/, a fork of AOSP's external camera HAL)
PRODUCT_PACKAGES += \
    android.hardware.camera.provider-service.n7100
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.camera.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.xml \
    frameworks/native/data/etc/android.hardware.camera.autofocus.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.autofocus.xml

# Telephony: XMM6262 3G modem (kernel modem_if). cbd boots it; libsec-ril (stock, its
# libril.so dependency patched to libril-n7100.so) runs inside our AIDL radio HAL. AOSP's rild
# (telephony_vendor.mk) stays idle: without vendor.rild.libpath it only sleeps
PRODUCT_PACKAGES += \
    android.hardware.radio-service.n7100 \
    libril-n7100 \
    libsecril-compat \
    libstdc++_vendor
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/proprietary/bin/cbd:$(TARGET_COPY_OUT_VENDOR)/bin/cbd \
    $(DEVICE_PATH)/proprietary/lib/libsec-ril.so:$(TARGET_COPY_OUT_VENDOR)/lib/libsec-ril.so \
    $(DEVICE_PATH)/proprietary/lib/libsecnativefeature.so:$(TARGET_COPY_OUT_VENDOR)/lib/libsecnativefeature.so \
    $(DEVICE_PATH)/proprietary/lib/libstlport.so:$(TARGET_COPY_OUT_VENDOR)/lib/libstlport.so \
    $(DEVICE_PATH)/proprietary/lib/libsqlite.so:$(TARGET_COPY_OUT_VENDOR)/lib/libsqlite.so \
    $(DEVICE_PATH)/proprietary/lib/libexpat.so:$(TARGET_COPY_OUT_VENDOR)/lib/libexpat.so \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.gsm.xml
PRODUCT_VENDOR_PROPERTIES += \
    ro.telephony.default_network=0

# GPS: Broadcom BCM4752 on UART1. Samsung's gpsd (stock, NEEDED entries patched, see
# proprietary/README) drives it; our AIDL GNSS HAL wraps the legacy HAL that talks to gpsd
PRODUCT_PACKAGES += \
    android.hardware.gnss-service.n7100 \
    libgps-compat
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/proprietary/bin/gpsd:$(TARGET_COPY_OUT_VENDOR)/bin/gpsd \
    $(DEVICE_PATH)/proprietary/lib/hw/gps.default.so:$(TARGET_COPY_OUT_VENDOR)/lib/hw/gps.default.so \
    $(DEVICE_PATH)/proprietary/etc/gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/gps.xml \
    $(DEVICE_PATH)/proprietary/etc/gps.cer:$(TARGET_COPY_OUT_VENDOR)/etc/gps.cer \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml

# Framework configuration overlay (auto-brightness curve)
PRODUCT_PACKAGES += \
    FrameworksResN7100
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.barometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.barometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.proximity.xml

# ALSA tools for audio bring-up
PRODUCT_PACKAGES += \
    tinymix.vendor \
    tinyplay.vendor \
    tinycap.vendor

# Key layouts: mainline reports the Home key as KEY_OK
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/rootdir/keylayout/gpio-keys.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/gpio-keys.kl \
    $(DEVICE_PATH)/rootdir/keylayout/tm2-touchkey.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/tm2-touchkey.kl \
    $(DEVICE_PATH)/rootdir/idc/sec_e-pen.idc:$(TARGET_COPY_OUT_VENDOR)/usr/idc/sec_e-pen.idc

# Init, fstab (first-stage mount of system/vendor), ueventd
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/rootdir/fstab.n7100:$(TARGET_COPY_OUT_RAMDISK)/fstab.n7100 \
    $(DEVICE_PATH)/rootdir/fstab.n7100:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.n7100 \
    $(DEVICE_PATH)/rootdir/init.n7100.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.n7100.rc \
    $(DEVICE_PATH)/rootdir/init.n7100.usb.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.n7100.usb.rc \
    $(DEVICE_PATH)/rootdir/ueventd.n7100.rc:$(TARGET_COPY_OUT_VENDOR)/etc/ueventd.rc

# n7100tool: hardware watchdog keeper (static ARM binary from n7100tool/; its dwc2
# workaround is the kernel's keep_l0 now)
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/prebuilt/bin/n7100tool:$(TARGET_COPY_OUT_VENDOR)/bin/n7100tool \
    $(DEVICE_PATH)/rootdir/bin/n7100-modules.sh:$(TARGET_COPY_OUT_VENDOR)/bin/n7100-modules.sh \
    $(DEVICE_PATH)/rootdir/bin/n7100-audio.sh:$(TARGET_COPY_OUT_VENDOR)/bin/n7100-audio.sh \
    $(DEVICE_PATH)/prebuilt/modules.netfilter:$(TARGET_COPY_OUT_VENDOR)/etc/n7100-modules.netfilter

# Firmware: BCM4334 Wi-Fi/Bluetooth (from postmarketOS firmware-samsung-midas)
PRODUCT_COPY_FILES += \
    $(call find-copy-subdir-files,*,$(DEVICE_PATH)/prebuilt/firmware,$(TARGET_COPY_OUT_VENDOR)/firmware)

# Graphics: Mesa (aospext), drm_hwcomposer (HWC3), minigbm allocator + AIDL mapper
PRODUCT_PACKAGES += \
    libEGL_mesa \
    libGLESv1_CM_mesa \
    libGLESv2_mesa \
    libgallium_dri \
    libgbm_mesa \
    dri_gbm \
    android.hardware.composer.hwc3-service.drm \
    android.hardware.graphics.allocator-service.minigbm \
    mapper.minigbm

PRODUCT_VENDOR_PROPERTIES += \
    ro.hardware.egl=mesa \
    ro.opengles.version=131072 \
    ro.surface_flinger.max_frame_buffer_acquired_buffers=2 \
    debug.renderengine.backend=skiaglthreaded

# Audio: AOSP's AIDL example HAL as vendor APEX (as on Cuttlefish). audioserver crashes without any
# audio HAL; the audio policy comes from the HAL itself (no audio_policy_configuration.xml)
PRODUCT_PACKAGES += \
    com.android.hardware.audio

# the AIDL effect HAL looks for its config outside the APEX
PRODUCT_COPY_FILES += \
    hardware/interfaces/audio/aidl/default/audio_effects_config.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects_config.xml

# The AIDL HAL creates its modules (primary -> IModule/default, r_submix) from the audio policy
# configuration; without one it registers none and audioserver, then system_server, block forever
AUDIO_POLICY_CONFIG := frameworks/av/services/audiopolicy/config
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/rootdir/audio/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    $(AUDIO_POLICY_CONFIG)/primary_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/primary_audio_policy_configuration.xml \
    $(AUDIO_POLICY_CONFIG)/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    $(AUDIO_POLICY_CONFIG)/bluetooth_audio_policy_configuration_7_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_audio_policy_configuration_7_0.xml \
    $(AUDIO_POLICY_CONFIG)/usb_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/usb_audio_policy_configuration.xml \
    $(AUDIO_POLICY_CONFIG)/stub_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/stub_audio_policy_configuration.xml \
    $(AUDIO_POLICY_CONFIG)/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    $(AUDIO_POLICY_CONFIG)/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    $(AUDIO_POLICY_CONFIG)/surround_sound_configuration_5_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/surround_sound_configuration_5_0.xml

# Generic AOSP HAL implementations
PRODUCT_PACKAGES += \
    com.android.hardware.gatekeeper.nonsecure \
    android.hardware.health-service.example \
    android.hardware.power-service.n7100 \
    android.hardware.security.keymint-service.nonsecure \
    android.hardware.usb-service.example

# The prebuilt kernel is an XZ zImage with appended DTB that extract_kernel.py can't read, and
# mainline 7.1 isn't in Android's kernel compatibility matrices anyway
PRODUCT_OTA_ENFORCE_VINTF_KERNEL_REQUIREMENTS := false

# Development builds (N7100_DEVELOPMENT=true in the environment, together with
# patches/development/vendor_lineage-debuggable-userdebug.patch): the build host's adb key
# (adb_keys, not in the repository), adb root after a data wipe, no system suspend
ifeq ($(N7100_DEVELOPMENT),true)
PRODUCT_ADB_KEYS := $(DEVICE_PATH)/adb_keys
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/rootdir/init.n7100.dev.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.n7100.dev.rc
endif

# Codec2: find the software codec store through AIDL. The default ("hidl") looks it up via
# hwservicemanager, which this device doesn't run, so MediaCodecList ends up without any software
# decoders (no ringtones, alarms, system sounds or video)
PRODUCT_VENDOR_PROPERTIES += \
    media.c2.hal.selection=aidl

# Mainline kernels have no ashmem (removed in 5.18): shared memory via memfd
PRODUCT_SYSTEM_PROPERTIES += \
    sys.use_memfd=true

# (persist.sys.usb.config=adb comes from the userdebug system props; vendor may not set it)
PRODUCT_VENDOR_PROPERTIES += \
    ro.sf.lcd_density=280

# Performance: the idle-maintenance dexopt compiles apps fully ("speed") instead of only
# their profiled hot code; installs stay quick (speed-profile)
PRODUCT_SYSTEM_PROPERTIES += \
    pm.dexopt.bg-dexopt=speed \
    dalvik.vm.dex2oat-threads=4
