#
# Copyright (C) 2025 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

DEVICE_PATH := device/xiaomi/muyu
KERNEL_PATH := $(DEVICE_PATH)-kernel

# Inherit from the common OEM chipset makefile.
$(call inherit-product, device/xiaomi/sm8635-common/common.mk)

# Fingerprint
$(call soong_config_set,XIAOMI_BIOMETRICS_FINGERPRINT,USE_NEW_IMPL,true)
$(call soong_config_set,XIAOMI_BIOMETRICS_FINGERPRINT,IMPL_VER,V2)

PRODUCT_PACKAGES += \
    android.hardware.biometrics.fingerprint-service.xiaomi

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.fingerprint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.fingerprint.xml

# Keyboard
PRODUCT_PACKAGES += \
    Vendor_15d9_Product_00a3.idc \
    xiaomi-keyboard

# Overlays
PRODUCT_PACKAGES += \
    FrameworksResMuyu \
    SettingsProviderResMuyu

# Rootdir
PRODUCT_PACKAGES += \
    init.muyu.rc

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(LOCAL_PATH)

# Inherit the proprietary files
$(call inherit-product, vendor/xiaomi/muyu/muyu-vendor.mk)
