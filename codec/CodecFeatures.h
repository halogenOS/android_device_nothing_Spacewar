/*
 * SPDX-FileCopyrightText: 2026 The halogenOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/custom/media/codec/BnCodecFeatures.h>

namespace aidl {
namespace custom {
namespace media {
namespace codec {

class CodecFeatures : public BnCodecFeatures {
  public:
    ndk::ScopedAStatus supportsThumbnailBlockModel(bool* _aidl_return) override;
};

}  // namespace codec
}  // namespace media
}  // namespace custom
}  // namespace aidl
