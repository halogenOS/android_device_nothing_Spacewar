/*
 * Copyright (C) 2024 The LineageOS Project
 * Copyright (C) 2024 The halogenOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <aidl/android/hardware/biometrics/fingerprint/BnFingerprint.h>
#include "LockoutTracker.h"
#include "Session.h"
#include "thread/WorkerThread.h"

using ::aidl::android::hardware::biometrics::fingerprint::ISession;
using ::aidl::android::hardware::biometrics::fingerprint::ISessionCallback;
using ::aidl::android::hardware::biometrics::fingerprint::SensorProps;
using ::aidl::android::hardware::biometrics::fingerprint::FingerprintSensorType;

namespace aidl::android::hardware::biometrics::fingerprint {

class Fingerprint : public BnFingerprint {
public:
    Fingerprint();
    ~Fingerprint();
    ndk::ScopedAStatus getSensorProps(std::vector<SensorProps>* _aidl_return) override;
    ndk::ScopedAStatus createSession(int32_t sensorId, int32_t userId,
                                     const std::shared_ptr<ISessionCallback>& cb,
                                     std::shared_ptr<ISession>* out) override;
private:
    static fingerprint_device_t* openHal();
    static void notify(const fingerprint_msg_t* msg);
    std::shared_ptr<Session> mSession;
    LockoutTracker mLockoutTracker;
    FingerprintSensorType mSensorType;
    int mMaxEnrollmentsPerUser;
    bool mSupportsGestures;
    fingerprint_device_t* mDevice;
    WorkerThread mWorker;
};
} // namespace aidl::android::hardware::biometrics::fingerprint