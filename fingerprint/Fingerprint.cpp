/*
 * Copyright (C) 2020 The Android Open Source Project
 * Copyright (C) 2024 The LineageOS Project
 * Copyright (C) 2024 The halogenOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Fingerprint.h"
#include "Session.h"

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>

#include <fingerprint.sysprop.h>

#include <inttypes.h>
#include <poll.h>
#include <unistd.h>

using namespace ::android::fingerprint::nothing;

using ::android::base::ParseInt;
using ::android::base::Split;

#undef LOG_TAG
#define LOG_TAG "NothingUdfpsHalSession"

namespace aidl::android::hardware::biometrics::fingerprint {

namespace {
constexpr size_t MAX_WORKER_QUEUE_SIZE = 5;
constexpr int SENSOR_ID = 0;
constexpr common::SensorStrength SENSOR_STRENGTH = common::SensorStrength::STRONG;
constexpr int MAX_ENROLLMENTS_PER_USER = 5;
constexpr bool SUPPORTS_NAVIGATION_GESTURES = false;
constexpr char HW_COMPONENT_ID[] = "fingerprintSensor";
constexpr char HW_VERSION[] = "nothing/goodix/fp_hal";
constexpr char FW_VERSION[] = "1.01";
constexpr char SERIAL_NUMBER[] = "00000001";
constexpr char SW_COMPONENT_ID[] = "matchingAlgorithm";
constexpr char SW_VERSION[] = "halogenOS/15/0";
}  // namespace

static Fingerprint* sInstance;

Fingerprint::Fingerprint()
    : mSensorType(FingerprintSensorType::UNKNOWN),
      mMaxEnrollmentsPerUser(MAX_ENROLLMENTS_PER_USER),
      mSupportsGestures(SUPPORTS_NAVIGATION_GESTURES),
      mDevice(nullptr),
      mWorker(MAX_WORKER_QUEUE_SIZE)  {

    sInstance = this; // keep track of the most recent instance

    mDevice = openHal();
    if (!mDevice) {
        LOG(ERROR) << "Can't open HAL module";
    }

    std::string sensorTypeProp = FingerprintHalProperties::type().value_or("");
    if (sensorTypeProp == "" || sensorTypeProp == "default" || sensorTypeProp == "rear")
        mSensorType = FingerprintSensorType::REAR;
    else if (sensorTypeProp == "udfps")
        mSensorType = FingerprintSensorType::UNDER_DISPLAY_ULTRASONIC;
    else if (sensorTypeProp == "udfps_optical")
        mSensorType = FingerprintSensorType::UNDER_DISPLAY_OPTICAL;
    else if (sensorTypeProp == "side")
        mSensorType = FingerprintSensorType::POWER_BUTTON;
    else if (sensorTypeProp == "home")
        mSensorType = FingerprintSensorType::HOME_BUTTON;
    else
        mSensorType = FingerprintSensorType::UNKNOWN;
}

fingerprint_device_t* Fingerprint::openHal() {
    const hw_module_t* hw_mdl = nullptr;

    LOG(DEBUG) << "Opening fingerprint hal library...";
    if (hw_get_module_by_class(FINGERPRINT_HARDWARE_MODULE_ID, NULL, &hw_mdl) != 0) {
        LOG(ERROR) << "Can't open fingerprint HW Module";
        return nullptr;
    }

    if (!hw_mdl) {
        LOG(ERROR) << "No valid fingerprint module";
        return nullptr;
    }

    auto module = reinterpret_cast<const fingerprint_module_t*>(hw_mdl);
    if (!module->common.methods->open) {
        LOG(ERROR) << "No valid open method";
        return nullptr;
    }

    hw_device_t* device = nullptr;
    if (module->common.methods->open(hw_mdl, nullptr, &device) != 0) {
        LOG(ERROR) << "Can't open fingerprint methods";
        return nullptr;
    }

    auto fp_device = reinterpret_cast<fingerprint_device_t*>(device);
    if (fp_device->set_notify(fp_device, Fingerprint::notify) != 0) {
        LOG(ERROR) << "Can't register fingerprint module callback";
        return nullptr;
    }

    return fp_device;
}

Fingerprint::~Fingerprint() {
    int err;
    LOG(VERBOSE) << "~Fingerprint()";

    if (mDevice == nullptr) {
        LOG(ERROR) << "No valid device";
        return;
    }

    if (0 != (err = mDevice->common.close(reinterpret_cast<hw_device_t*>(mDevice)))) {
        LOG(ERROR) << "Can't close fingerprint module, error: " << err;
        return;
    }

    mDevice = nullptr;
}

void Fingerprint::notify(const fingerprint_msg_t* msg) {
    Fingerprint* thisPtr = sInstance;

    if (thisPtr == nullptr || thisPtr->mSession == nullptr || thisPtr->mSession->isClosed()) {
        LOG(ERROR) << "Receiving callbacks before a session is opened.";
        return;
    }

    thisPtr->mSession->notify(msg);
}

ndk::ScopedAStatus Fingerprint::getSensorProps(std::vector<SensorProps>* out) {
    std::vector<common::ComponentInfo> componentInfo = {
            {HW_COMPONENT_ID, HW_VERSION, FW_VERSION, SERIAL_NUMBER, "" /* softwareVersion */},
            {SW_COMPONENT_ID, "" /* hardwareVersion */, "" /* firmwareVersion */,
            "" /* serialNumber */, SW_VERSION}};
    common::CommonProps commonProps = {SENSOR_ID, SENSOR_STRENGTH,
                                       mMaxEnrollmentsPerUser, componentInfo};

    SensorLocation sensorLocation;
    std::string loc = FingerprintHalProperties::sensor_location().value_or("");
    std::vector<std::string> dim = Split(loc, "|");

    if (dim.size() >= 3 && dim.size() <= 4) {
        ParseInt(dim[0], &sensorLocation.sensorLocationX);
        ParseInt(dim[1], &sensorLocation.sensorLocationY);
        ParseInt(dim[2], &sensorLocation.sensorRadius);
        if (dim.size() >= 4)
            sensorLocation.display = dim[3];
    } else if(loc.length() > 0) {
        LOG(WARNING) << "Invalid sensor location input (x|y|radius|display): " << loc;
    }

    LOG(INFO) << "Sensor type: " << ::android::internal::ToString(mSensorType)
              << " location: " << sensorLocation.toString();

    *out = {{commonProps,
             mSensorType,
             {sensorLocation},
             mSupportsGestures,
             false,
             false,
             false,
             std::nullopt}};

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Fingerprint::createSession(int32_t /*sensorId*/, int32_t userId,
                                              const std::shared_ptr<ISessionCallback>& cb,
                                              std::shared_ptr<ISession>* out) {
    CHECK(mSession == nullptr || mSession->isClosed()) << "Open session already exists!";

    LOG(INFO) << "Creating session for user ID " << userId;

    mSession = SharedRefBase::make<Session>(mDevice, userId, cb, mLockoutTracker, &mWorker);
    *out = mSession;

    mSession->linkToDeath(cb->asBinder().get());

    return ndk::ScopedAStatus::ok();
}

} // namespace aidl::android::hardware::biometrics::fingerprint