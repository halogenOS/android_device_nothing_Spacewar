/*
 * Copyright (C) 2021 The Android Open Source Project
 * Copyright (C) 2024 The halogenOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "util/CancellationSignal.h"

#include <android-base/logging.h>
#include <chrono>

namespace aidl::android::hardware::biometrics {

CancellationSignal::CancellationSignal(std::promise<void>&& cancellationPromise)
    : mCancellationPromise(std::move(cancellationPromise)) {}

ndk::ScopedAStatus CancellationSignal::cancel() {
    mCancellationPromise.set_value();
    return ndk::ScopedAStatus::ok();
}

bool shouldCancel(const std::future<void>& f) {
    CHECK(f.valid());
    return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

}  // namespace aidl::android::hardware::biometrics
