/*
 * Copyright (C) 2026 The halogenOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/custom/hardware/biometrics/fingerprint/udfps/BnUdfpsExtension.h>

namespace aidl::custom::hardware::biometrics::fingerprint::udfps {

class UdfpsExtension : public BnUdfpsExtension {
  public:
    ndk::ScopedAStatus getUdfpsDimZOrder(int32_t z, int32_t* _aidl_return) override;
    ndk::ScopedAStatus getUdfpsZOrder(int32_t z, bool touched, int32_t* _aidl_return) override;
    ndk::ScopedAStatus getUdfpsUsageBits(int64_t usageBits, bool touched, int64_t* _aidl_return) override;
};

}  // namespace aidl::custom::hardware::biometrics::fingerprint::udfps
