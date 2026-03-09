/*
 * Copyright (C) 2026 The halogenOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "UdfpsExtension.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

using aidl::custom::hardware::biometrics::fingerprint::udfps::UdfpsExtension;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    auto service = ndk::SharedRefBase::make<UdfpsExtension>();

    const std::string instance = std::string(UdfpsExtension::descriptor) + "/default";
    binder_exception_t status = AServiceManager_addService(service->asBinder().get(), instance.c_str());
    CHECK_EQ(status, EX_NONE);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
