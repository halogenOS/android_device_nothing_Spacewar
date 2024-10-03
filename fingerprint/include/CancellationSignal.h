/*
 * Copyright (C) 2021 The Android Open Source Project
 * Copyright (C) 2024 The halogenOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/biometrics/common/BnCancellationSignal.h>
#include <functional>
#include <future>

namespace aidl::android::hardware::biometrics {

class CancellationSignal : public common::BnCancellationSignal {
  public:
    explicit CancellationSignal(std::promise<void>&& cancellationPromise);

    ndk::ScopedAStatus cancel() override;

  private:
    std::promise<void> mCancellationPromise;
};

// Returns whether the given cancellation future is ready, i.e. whether the operation corresponding
// to this future should be cancelled.
bool shouldCancel(const std::future<void>& cancellationFuture);

}  // namespace aidl::android::hardware::biometrics
