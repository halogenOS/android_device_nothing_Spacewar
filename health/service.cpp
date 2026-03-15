/*
 * Copyright (C) 2026 The halogenOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ChargingControl.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

using aidl::vendor::lineage::health::ChargingControl;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    auto svc = ndk::SharedRefBase::make<ChargingControl>();
    std::string instance = std::string(ChargingControl::descriptor) + "/default";
    auto status = AServiceManager_addService(svc->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
