/*
 * SPDX-FileCopyrightText: 2026 The halogenOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "custom.media.codec.features-service.spacewar"

#include "CodecFeatures.h"

namespace aidl {
namespace custom {
namespace media {
namespace codec {

ndk::ScopedAStatus CodecFeatures::supportsThumbnailBlockModel(bool* _aidl_return) {
    *_aidl_return = false;
    return ndk::ScopedAStatus::ok();
}

}  // namespace codec
}  // namespace media
}  // namespace custom
}  // namespace aidl
