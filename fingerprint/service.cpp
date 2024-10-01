/*
 * Copyright (C) 2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Fingerprint.h"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android-base/logging.h>

using ::aidl::android::hardware::biometrics::fingerprint::Fingerprint;

int main() {
    LOG(INFO) << "Fingerprint HAL started";
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    std::shared_ptr<Fingerprint> hal = ndk::SharedRefBase::make<Fingerprint>();
    auto binder = hal->asBinder();

    const std::string instance = std::string() + Fingerprint::descriptor + "/default";
    binder_status_t status = AServiceManager_addService(binder.get(), instance.c_str());
    CHECK(status == STATUS_OK);

    LOG(INFO) << "Service has been added";

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE; // should not reach
}