#
# Copyright (C) 2025 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Include the common OEM chipset BoardConfig.
include device/xiaomi/sm8635-common/BoardConfigCommon.mk

# Bootloader
TARGET_BOOTLOADER_BOARD_NAME := muyu

# DTB / DTBO
BOARD_PREBUILT_DTBIMAGE_DIR := $(KERNEL_PATH)/dtbs/
BOARD_PREBUILT_DTBOIMAGE := $(KERNEL_PATH)/dtbo.img

# Keyboard
DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE += \
    device/xiaomi/muyu/keyboard/framework_compatibility_matrix.xml

# Kernel
TARGET_NO_KERNEL_OVERRIDE := true
LOCAL_KERNEL := $(KERNEL_PATH)/kernel
PRODUCT_COPY_FILES += \
	$(LOCAL_KERNEL):kernel

# Kernel modules
DLKM_MODULES_PATH := $(KERNEL_PATH)/modules/vendor
RAMDISK_MODULES_PATH := $(KERNEL_PATH)/modules/ramdisk

BOARD_VENDOR_KERNEL_MODULES := $(wildcard $(DLKM_MODULES_PATH)/*.ko)
BOARD_VENDOR_KERNEL_MODULES_LOAD := $(patsubst %,$(DLKM_MODULES_PATH)/%,$(shell cat $(DLKM_MODULES_PATH)/modules.load))
BOARD_VENDOR_KERNEL_MODULES_BLOCKLIST_FILE := $(DLKM_MODULES_PATH)/modules.blocklist

BOARD_VENDOR_RAMDISK_KERNEL_MODULES := $(wildcard $(RAMDISK_MODULES_PATH)/*.ko)
BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD := $(patsubst %,$(RAMDISK_MODULES_PATH)/%,$(shell cat $(RAMDISK_MODULES_PATH)/modules.load))
BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD  := $(patsubst %,$(RAMDISK_MODULES_PATH)/%,$(shell cat $(RAMDISK_MODULES_PATH)/modules.load.recovery))
BOARD_VENDOR_RAMDISK_KERNEL_MODULES_BLOCKLIST_FILE := $(RAMDISK_MODULES_PATH)/modules.blocklist

# Properties
TARGET_ODM_PROP += $(DEVICE_PATH)/configs/properties/odm.prop
TARGET_SYSTEM_PROP += $(DEVICE_PATH)/configs/properties/system.prop
TARGET_VENDOR_PROP += $(DEVICE_PATH)/configs/properties/vendor.prop

# SEPolicy
BOARD_VENDOR_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy/vendor
SYSTEM_EXT_PUBLIC_SEPOLICY_DIRS += device/xiaomi/muyu/sepolicy/public
SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += device/xiaomi/muyu/sepolicy/private

# Workaround to make lineage's soong generator work
TARGET_KERNEL_SOURCE := $(KERNEL_PATH)/kernel-headers

# Inherit the proprietary files
include vendor/xiaomi/muyu/BoardConfigVendor.mk
