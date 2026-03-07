/*
 * SPDX-FileCopyrightText: 2026 The halogenOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CodecFeatures.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

using aidl::custom::media::codec::CodecFeatures;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    std::shared_ptr<CodecFeatures> service = ndk::SharedRefBase::make<CodecFeatures>();

    const std::string instance = std::string(CodecFeatures::descriptor) + "/default";
    binder_exception_t status = AServiceManager_addService(service->asBinder().get(), instance.c_str());
    CHECK_EQ(status, EX_NONE);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
