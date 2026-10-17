#
# Copyright (C) 2025 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)

# Inherit some common PixelOS stuff.
$(call inherit-product, vendor/custom/config/common_full_tablet_wifionly.mk)
TARGET_SCREEN_WIDTH := 3200
TARGET_SCREEN_HEIGHT := 2136
TARGET_BOOT_ANIMATION_RES := 1440

# Inherit from device makefile.
$(call inherit-product, device/xiaomi/muyu/device.mk)

PRODUCT_DEVICE := muyu
PRODUCT_BRAND := Xiaomi
PRODUCT_NAME := custom_muyu
PRODUCT_MANUFACTURER := Xiaomi
PRODUCT_MODEL := 24091RPADG

PRODUCT_GMS_CLIENTID_BASE := android-xiaomi

PRODUCT_BUILD_PROP_OVERRIDES += \
    BuildDesc="missi-user 16 BP2A.250605.031.A3 OS3.0.7.0.WOYMIXM release-keys" \
    BuildFingerprint=Xiaomi/muyu_global/muyu:16/BP2A.250605.031.A3/OS3.0.7.0.WOYMIXM:user/release-keys

PRODUCT_SYSTEM_NAME := muyu_global
PRODUCT_SYSTEM_DEVICE := muyu

PRODUCT_BUILD_PROP_OVERRIDES += \
    DeviceName=$(PRODUCT_SYSTEM_DEVICE) \
    DeviceProduct=$(PRODUCT_SYSTEM_NAME)
