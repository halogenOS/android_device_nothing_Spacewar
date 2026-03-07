/*
 * Copyright (C) 2020 The Android Open Source Project
 * Copyright (C) 2024 The LineageOS Project
 * Copyright (C) 2024 The halogenOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <android-base/logging.h>

#include "Session.h"
#include "Legacy2Aidl.h"

#include "CancellationSignal.h"

#undef LOG_TAG
#define LOG_TAG "NothingUdfpsHalSession"

namespace aidl::android::hardware::biometrics::fingerprint {

void onClientDeath(void* cookie) {
    LOG(INFO) << "FingerprintService has died";
    Session* session = static_cast<Session*>(cookie);
    if (session && !session->isClosed()) {
        session->close();
    }
}

Session::Session(fingerprint_device_t* device, int userId,
            std::shared_ptr<ISessionCallback> cb, LockoutTracker lockoutTracker,
            WorkerThread* worker)
            : mDevice(device),
              mLockoutTracker(lockoutTracker),
              mWorker(worker),
              mUserId(userId),
              mCb(cb) {
    CHECK_GE(mUserId, 0) << "invalid user ID";
    CHECK(mWorker) << "invalid mWorker";
    CHECK(mCb) << "invalid mCb";
    mDeathRecipient = AIBinder_DeathRecipient_new(onClientDeath);

    char path[256];
    snprintf(path, sizeof(path), "/data/vendor_de/%d/fpdata/", userId);
    mDevice->set_active_group(mDevice, mUserId, path);
}

binder_status_t Session::linkToDeath(AIBinder* binder) {
    return AIBinder_linkToDeath(binder, mDeathRecipient, this);
}

void Session::scheduleStateOrCrash(SessionState state) {
    mScheduledState = state;
}

void Session::enterStateOrCrash(SessionState state) {
    CHECK(mScheduledState == state);
    mCurrentState = state;
    mScheduledState = SessionState::IDLING;
}

void Session::enterIdling() {
    if (mCurrentState != SessionState::CLOSED) {
        mCurrentState = SessionState::IDLING;
    }
}

bool Session::isClosed() {
    return mClosed;
}

ndk::ScopedAStatus Session::generateChallenge() {
    scheduleStateOrCrash(SessionState::GENERATING_CHALLENGE);

    mWorker->schedule(Callable::from([this] {
        enterStateOrCrash(SessionState::GENERATING_CHALLENGE);
        uint64_t challenge = mDevice->pre_enroll(mDevice);
        if (!mCb->onChallengeGenerated(challenge).isOk()) {
            LOG(ERROR) << "Failed to invoke onChallengeGenerated callback";
        }
        enterIdling();
    }));

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::revokeChallenge(int64_t challenge) {
    scheduleStateOrCrash(SessionState::REVOKING_CHALLENGE);

    mWorker->schedule(Callable::from([this, challenge] {
        enterStateOrCrash(SessionState::REVOKING_CHALLENGE);
        mDevice->post_enroll(mDevice);
        if (!mCb->onChallengeRevoked(challenge).isOk()) {
            LOG(ERROR) << "Failed to invoke onChallengeRevoked callback";
        }
        enterIdling();
    }));

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::enroll(const keymaster::HardwareAuthToken& hat,
                                   std::shared_ptr<common::ICancellationSignal>* out) {
    scheduleStateOrCrash(SessionState::ENROLLING);

    std::promise<void> cancellationPromise;
    auto cancFuture = cancellationPromise.get_future();

    mWorker->schedule(Callable::from([this, hat, cancFuture = std::move(cancFuture)] {
        enterStateOrCrash(SessionState::ENROLLING);
        if (shouldCancel(cancFuture)) {
            cancel();
            if (!mCb->onError(Error::CANCELED, 0 /* vendorCode */).isOk()) {
                LOG(ERROR) << "Failed to invoke onError callback";
            }
        } else {
            hw_auth_token_t authToken;
            translate(hat, authToken);
            int error = mDevice->enroll(mDevice, &authToken, mUserId, 60);
            if (error) {
                LOG(ERROR) << "enroll failed: " << error;
                if (!mCb->onError(Error::UNABLE_TO_PROCESS, error).isOk()) {
                    LOG(ERROR) << "Failed to invoke onError callback";
                }
            }
        }
        enterIdling();
    }));

    *out = SharedRefBase::make<CancellationSignal>(std::move(cancellationPromise));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::authenticate(int64_t operationId,
                                         std::shared_ptr<ICancellationSignal>* out) {
    scheduleStateOrCrash(SessionState::AUTHENTICATING);

    std::promise<void> cancPromise;
    auto cancFuture = cancPromise.get_future();

    mWorker->schedule(Callable::from([this, operationId, cancFuture = std::move(cancFuture)] {
        enterStateOrCrash(SessionState::AUTHENTICATING);
        if (shouldCancel(cancFuture)) {
            cancel();
            if (!mCb->onError(Error::CANCELED, 0 /* vendorCode */).isOk()) {
                LOG(ERROR) << "Failed to invoke onError callback";
            }
        } else {
            mUiReady = false;
            int error = mDevice->authenticate(mDevice, operationId, mUserId);
            if (error) {
                LOG(ERROR) << "authenticate failed: " << error;
                if (!mCb->onError(Error::UNABLE_TO_PROCESS, error).isOk()) {
                    LOG(ERROR) << "Failed to invoke onError callback";
                }
            }
        }
        enterIdling();
    }));

    *out = SharedRefBase::make<CancellationSignal>(std::move(cancPromise));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::detectInteraction(std::shared_ptr<ICancellationSignal>* out) {
    scheduleStateOrCrash(SessionState::DETECTING_INTERACTION);

    std::promise<void> cancellationPromise;
    auto cancFuture = cancellationPromise.get_future();

    mWorker->schedule(Callable::from([this, cancFuture = std::move(cancFuture)] {
        enterStateOrCrash(SessionState::DETECTING_INTERACTION);
        if (shouldCancel(cancFuture)) {
            cancel();
            if (!mCb->onError(Error::CANCELED, 0 /* vendorCode */).isOk()) {
                LOG(ERROR) << "Failed to invoke onError callback";
            }
        } else {
            LOG(DEBUG) << "Detect interaction is not supported";
            if (!mCb->onError(Error::UNABLE_TO_PROCESS, 0 /* vendorCode */).isOk()) {
                LOG(ERROR) << "Failed to invoke onError callback";
            }
        }
        enterIdling();
    }));

    *out = SharedRefBase::make<CancellationSignal>(std::move(cancellationPromise));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::enumerateEnrollments() {
    scheduleStateOrCrash(SessionState::ENUMERATING_ENROLLMENTS);

    mWorker->schedule(Callable::from([this] {
        enterStateOrCrash(SessionState::ENUMERATING_ENROLLMENTS);
        int error = mDevice->enumerate(mDevice);
        if (error) {
            LOG(ERROR) << "enumerate failed: " << error;
        }
        enterIdling();
    }));

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::removeEnrollments(const std::vector<int32_t>& enrollmentIds) {
    scheduleStateOrCrash(SessionState::REMOVING_ENROLLMENTS);

    mWorker->schedule(Callable::from([this, enrollmentIds] {
        enterStateOrCrash(SessionState::REMOVING_ENROLLMENTS);
        for (int32_t fid : enrollmentIds) {
            int error = mDevice->remove(mDevice, mUserId, fid);
            if (error) {
                LOG(ERROR) << "remove failed: " << error;
            }
        }
        enterIdling();
    }));

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::getAuthenticatorId() {
    scheduleStateOrCrash(SessionState::GETTING_AUTHENTICATOR_ID);

    mWorker->schedule(Callable::from([this] {
        enterStateOrCrash(SessionState::GETTING_AUTHENTICATOR_ID);
        uint64_t auth_id = mDevice->get_authenticator_id(mDevice);
        if (!mCb->onAuthenticatorIdRetrieved(auth_id).isOk()) {
            LOG(ERROR) << "Failed to invoke onAuthenticatorIdRetrieved callback";
        }
        enterIdling();
    }));

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::invalidateAuthenticatorId() {
    scheduleStateOrCrash(SessionState::INVALIDATING_AUTHENTICATOR_ID);

    mWorker->schedule(Callable::from([this] {
        enterStateOrCrash(SessionState::INVALIDATING_AUTHENTICATOR_ID);
        uint64_t auth_id = mDevice->get_authenticator_id(mDevice);
        if (!mCb->onAuthenticatorIdInvalidated(auth_id).isOk()) {
            LOG(ERROR) << "Failed to invoke onAuthenticatorIdInvalidated callback";
        }
        enterIdling();
    }));

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::resetLockout(const keymaster::HardwareAuthToken& /* hat */) {
    scheduleStateOrCrash(SessionState::RESETTING_LOCKOUT);

    mWorker->schedule(Callable::from([this] {
        enterStateOrCrash(SessionState::RESETTING_LOCKOUT);
        clearLockout(true);
        mIsLockoutTimerAborted = true;
        enterIdling();
    }));

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::close() {
    mWorker->schedule(Callable::from([this] {
        mDevice->goodixExtCmd(mDevice, 0, 0);
        mUiReady = false;
        mUiCv.notify_all();
    }));
    mCurrentState = SessionState::CLOSED;
    if (!mCb->onSessionClosed().isOk()) {
        LOG(ERROR) << "Failed to invoke onSessionClosed callback";
    }
    AIBinder_DeathRecipient_delete(mDeathRecipient);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::onPointerDown(int32_t /*pointerId*/, int32_t x, int32_t y, float minor,
                                          float major) {
    mWorker->schedule(Callable::from([this, x, y, minor, major] {
        // Wait for UI to be ready using condition variable with timeout
        {
            std::unique_lock<std::mutex> lk(mUiMutex);
            (void)mUiCv.wait_for(lk, std::chrono::milliseconds(400),
                                 [this]{ return mUiReady.load(); });
        }
        mDevice->goodixExtCmd(mDevice, 1, 0);
        checkSensorLockout();
    }));

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::onPointerUp(int32_t /*pointerId*/) {
    mWorker->schedule(Callable::from([this] {
        mUiReady = false;
        mUiCv.notify_all();
        mDevice->goodixExtCmd(mDevice, 0, 0);
    }));

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::onUiReady() {
    // Directly set UI ready from binder thread without scheduling
    {
        std::lock_guard<std::mutex> lk(mUiMutex);
        mUiReady = true;
    }
    mUiCv.notify_all();
    enterIdling();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::authenticateWithContext(
        int64_t operationId, const common::OperationContext& /*context*/,
        std::shared_ptr<common::ICancellationSignal>* out) {
    return authenticate(operationId, out);
}

ndk::ScopedAStatus Session::enrollWithContext(const keymaster::HardwareAuthToken& hat,
                                              const common::OperationContext& /*context*/,
                                              std::shared_ptr<common::ICancellationSignal>* out) {
    return enroll(hat, out);
}

ndk::ScopedAStatus Session::detectInteractionWithContext(
        const common::OperationContext& /*context*/,
        std::shared_ptr<common::ICancellationSignal>* out) {
    return detectInteraction(out);
}

ndk::ScopedAStatus Session::onPointerDownWithContext(const PointerContext& context) {
    return onPointerDown(context.pointerId, context.x, context.y, context.minor, context.major);
}

ndk::ScopedAStatus Session::onPointerUpWithContext(const PointerContext& context) {
    return onPointerUp(context.pointerId);
}

ndk::ScopedAStatus Session::onContextChanged(const common::OperationContext& /*context*/) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::onPointerCancelWithContext(const PointerContext& /*context*/) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::setIgnoreDisplayTouches(bool /*shouldIgnore*/) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::cancel() {
    mWorker->schedule(Callable::from([this] {
        mDevice->goodixExtCmd(mDevice, 0, 0);
        mUiReady = false;
        mUiCv.notify_all();
        int ret = mDevice->cancel(mDevice);
        if (ret == 0) {
            if (!mCb->onError(Error::CANCELED, 0 /* vendorCode */).isOk()) {
                LOG(ERROR) << "Failed to invoke onError callback";
            }
        }
        enterIdling();
    }));

    return ndk::ScopedAStatus::ok();
}

// Translate from errors returned by traditional HAL (see fingerprint.h) to
// AIDL-compliant Error
Error Session::VendorErrorFilter(int32_t error, int32_t* vendorCode) {
    if (vendorCode == NULL) {
        int fakeVendorCode = 0;
        vendorCode = &fakeVendorCode;
    }
    switch (error) {
        case FINGERPRINT_ERROR_HW_UNAVAILABLE:
            return Error::HW_UNAVAILABLE;
        case FINGERPRINT_ERROR_UNABLE_TO_PROCESS:
            return Error::UNABLE_TO_PROCESS;
        case FINGERPRINT_ERROR_TIMEOUT:
            return Error::TIMEOUT;
        case FINGERPRINT_ERROR_NO_SPACE:
            return Error::NO_SPACE;
        case FINGERPRINT_ERROR_CANCELED:
            return Error::CANCELED;
        case FINGERPRINT_ERROR_UNABLE_TO_REMOVE:
            return Error::UNABLE_TO_REMOVE;
        case FINGERPRINT_ERROR_LOCKOUT: {
            *vendorCode = FINGERPRINT_ERROR_LOCKOUT;
            return Error::VENDOR;
        }
        default:
            if (error >= FINGERPRINT_ERROR_VENDOR_BASE) {
                // vendor specific code.
                *vendorCode = error - FINGERPRINT_ERROR_VENDOR_BASE;
                return Error::VENDOR;
            }
    }
    LOG(ERROR) << "Unknown error from fingerprint vendor library: " << error;
    return Error::UNABLE_TO_PROCESS;
}

// Translate acquired messages returned by traditional HAL (see fingerprint.h)
// to AIDL-compliant AcquiredInfo
AcquiredInfo Session::VendorAcquiredFilter(int32_t info, int32_t* vendorCode) {
    if (vendorCode == NULL) {
        int fakeVendorCode = 0;
        vendorCode = &fakeVendorCode;
    }
    switch (info) {
        case FINGERPRINT_ACQUIRED_GOOD:
            return AcquiredInfo::GOOD;
        case FINGERPRINT_ACQUIRED_PARTIAL:
            return AcquiredInfo::PARTIAL;
        case FINGERPRINT_ACQUIRED_INSUFFICIENT:
            return AcquiredInfo::INSUFFICIENT;
        case FINGERPRINT_ACQUIRED_IMAGER_DIRTY:
            return AcquiredInfo::SENSOR_DIRTY;
        case FINGERPRINT_ACQUIRED_TOO_SLOW:
            return AcquiredInfo::TOO_SLOW;
        case FINGERPRINT_ACQUIRED_TOO_FAST:
            return AcquiredInfo::TOO_FAST;
        default:
            if (info >= FINGERPRINT_ACQUIRED_VENDOR_BASE) {
                // vendor specific code.
                *vendorCode = info - FINGERPRINT_ACQUIRED_VENDOR_BASE;
                LOG(DEBUG) << "Vendor specific code, vendorCode: " << *vendorCode
                           << ", info: " << info;
                return AcquiredInfo::VENDOR;
            }
    }
    LOG(ERROR) << "Unknown acquiredmsg from fingerprint vendor library: " << info;
    return AcquiredInfo::INSUFFICIENT;
}

bool Session::checkSensorLockout() {
    LockoutMode lockoutMode = mLockoutTracker.getMode();
    if (lockoutMode == LockoutMode::PERMANENT) {
        LOG(ERROR) << "Fail: lockout permanent";
        if (!mCb->onLockoutPermanent().isOk()) {
            LOG(ERROR) << "Failed to invoke onLockoutPermanent callback";
        }
        mIsLockoutTimerAborted = true;
        return true;
    } else if (lockoutMode == LockoutMode::TIMED) {
        int64_t timeLeft = mLockoutTracker.getLockoutTimeLeft();
        LOG(ERROR) << "Fail: lockout timed: " << timeLeft;
        if (!mCb->onLockoutTimed(timeLeft).isOk()) {
            LOG(ERROR) << "Failed to invoke onLockoutTimed callback";
        }
        if (!mIsLockoutTimerStarted) startLockoutTimer(timeLeft);
        return true;
    }
    return false;
}

void Session::clearLockout(bool clearAttemptCounter) {
    mLockoutTracker.reset(clearAttemptCounter);
    if (!mCb->onLockoutCleared().isOk()) {
        LOG(ERROR) << "Failed to invoke onLockoutCleared callback";
    }
}

void Session::startLockoutTimer(int64_t timeout) {
    mIsLockoutTimerAborted = false;
    std::function<void()> action =
            std::bind(&Session::lockoutTimerExpired, this);
    std::thread([timeout, action]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
        action();
    }).detach();

    mIsLockoutTimerStarted = true;
}

void Session::lockoutTimerExpired() {
    if (!mIsLockoutTimerAborted)
        clearLockout(false);

    mIsLockoutTimerStarted = false;
    mIsLockoutTimerAborted = false;
}

void Session::notify(const fingerprint_msg_t* msg) {
    switch (msg->type) {
        case FINGERPRINT_ERROR: {
            int32_t vendorCode = 0;
            Error result = VendorErrorFilter(msg->data.error, &vendorCode);
            LOG(DEBUG) << "onError(" << (int8_t) result << ", " << vendorCode << ");";
            enterIdling();
            if (!mCb->onError(result, vendorCode).isOk()) {
                LOG(ERROR) << "Failed to invoke onError callback";
            }
            mUiReady = false;
            mUiCv.notify_all();
        } break;
        case FINGERPRINT_ACQUIRED: {
            int32_t vendorCode = 0;
            AcquiredInfo result =
                    VendorAcquiredFilter(msg->data.acquired.acquired_info, &vendorCode);
            if (result == AcquiredInfo::GOOD) {
                mLockoutTracker.reset(true);
            }
            enterIdling();
            if (result != AcquiredInfo::VENDOR) {
                if (!mCb->onAcquired(result, vendorCode).isOk()) {
                    LOG(ERROR) << "Failed to invoke onAcquired callback";
                }
            }
        } break;
        case FINGERPRINT_TEMPLATE_ENROLLING: {
            LOG(DEBUG) << "onEnrollResult(fid=" << msg->data.enroll.finger.fid
                       << ", gid=" << msg->data.enroll.finger.gid
                       << ", rem=" << msg->data.enroll.samples_remaining << ")";
            if (!mCb->onEnrollmentProgress(msg->data.enroll.finger.fid,
                                          msg->data.enroll.samples_remaining).isOk()) {
                LOG(ERROR) << "Failed to invoke onEnrollmentProgress callback";
            }
        } break;
        case FINGERPRINT_TEMPLATE_REMOVED: {
            LOG(DEBUG) << "onRemove(fid=" << msg->data.removed.finger.fid
                       << ", gid=" << msg->data.removed.finger.gid
                       << ", rem=" << msg->data.removed.remaining_templates << ")";
            std::vector<int> enrollments;
            enrollments.push_back(msg->data.removed.finger.fid);
            if (!mCb->onEnrollmentsRemoved(enrollments).isOk()) {
                LOG(ERROR) << "Failed to invoke onEnrollmentsRemoved callback";
            }
        } break;
        case FINGERPRINT_AUTHENTICATED: {
            LOG(DEBUG) << "onAuthenticated(fid=" << msg->data.authenticated.finger.fid
                       << ", gid=" << msg->data.authenticated.finger.gid << ")";
            enterIdling();
            if (msg->data.authenticated.finger.fid != 0) {
                const hw_auth_token_t hat = msg->data.authenticated.hat;
                HardwareAuthToken authToken;
                translate(hat, authToken);

                if (!mCb->onAuthenticationSucceeded(msg->data.authenticated.finger.fid, authToken).isOk()) {
                    LOG(ERROR) << "Failed to invoke onAuthenticationSucceeded callback";
                }
                mLockoutTracker.reset(true);
            } else {
                if (!mCb->onAuthenticationFailed().isOk()) {
                    LOG(ERROR) << "Failed to invoke onAuthenticationFailed callback";
                }
                mLockoutTracker.addFailedAttempt();
                checkSensorLockout();
            }
            mUiReady = false;
            mUiCv.notify_all();
        } break;
        case FINGERPRINT_TEMPLATE_ENUMERATING: {
            LOG(DEBUG) << "onEnumerate(fid=" << msg->data.enumerated.finger.fid
                       << ", gid=" << msg->data.enumerated.finger.gid
                       << ", rem=" << msg->data.enumerated.remaining_templates << ")";
            static std::vector<int> enrollments;
            enrollments.push_back(msg->data.enumerated.finger.fid);
            if (msg->data.enumerated.remaining_templates == 0) {
                if (!mCb->onEnrollmentsEnumerated(enrollments).isOk()) {
                    LOG(ERROR) << "Failed to invoke onEnrollmentsEnumerated callback";
                }
                enrollments.clear();
            }
        } break;
    }
}

} // namespace aidl::android::hardware::biometrics::fingerprint
