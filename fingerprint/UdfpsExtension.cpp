/*
 * Copyright (C) 2022-2024 The LineageOS Project
 * Copyright (C) 2026 The halogenOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "custom.hardware.biometrics.fingerprint.udfps-service.nothing"

#include "UdfpsExtension.h"

#if __has_include(<display/drm/sde_drm.h>)
#include <display/drm/sde_drm.h>
#elif __has_include(<drm/sde_drm.h>)
#include <drm/sde_drm.h>
#endif

namespace aidl::custom::hardware::biometrics::fingerprint::udfps {

ndk::ScopedAStatus UdfpsExtension::getUdfpsDimZOrder(int32_t z, int32_t* _aidl_return) {
    *_aidl_return = z;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus UdfpsExtension::getUdfpsZOrder(int32_t z, bool touched, int32_t* _aidl_return) {
    *_aidl_return = touched ? (z | FOD_PRESSED_LAYER_ZORDER) : z;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus UdfpsExtension::getUdfpsUsageBits(int64_t usageBits, bool /*touched*/, int64_t* _aidl_return) {
    *_aidl_return = usageBits;
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::custom::hardware::biometrics::fingerprint::udfps
