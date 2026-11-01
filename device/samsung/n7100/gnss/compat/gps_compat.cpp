/*
 * What Samsung's 2014 gpsd (Broadcom glgps for the BCM4752) needs that Android 17's
 * vendor libraries don't have. gpsd's NEEDED entries for libgui.so and libwrappergps.so
 * point here (proprietary/README):
 *  - android::SensorManager and friends (KitKat libgui): sensor-aided positioning.
 *    No sensors are offered; gpsd still insists on an event queue (ASSERT), so it gets
 *    one whose fd never becomes readable.
 *  - libwrappergps' RIL client (frequency aiding and control-plane AGPS over
 *    libsecril-client): a dummy that connects but never answers, see below.
 *  - SSLv3_client_method, gone from BoringSSL (SUPL over TLS, disabled in gps.xml).
 * The C++ symbols are defined with their mangled names; ARM EABI constructors return
 * `this`, and an sp<> result is written through the hidden result pointer (r0).
 */
#include <fcntl.h>
#include <openssl/ssl.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>
#include <utils/RefBase.h>

#define EXPORT extern "C" __attribute__((visibility("default")))

/* android::Singleton<android::SensorManager>::sLock / sInstance (zeroed = unlocked / none) */
EXPORT __attribute__((aligned(8))) char _ZN7android9SingletonINS_13SensorManagerEE5sLockE[64] = {};
EXPORT void* _ZN7android9SingletonINS_13SensorManagerEE9sInstanceE = nullptr;

/* SensorManager::SensorManager() */
EXPORT void* _ZN7android13SensorManagerC1Ev(void* self) {
    return self;
}

/* ssize_t SensorManager::getSensorList(Sensor const* const** list) const */
EXPORT ssize_t _ZNK7android13SensorManager13getSensorListEPPKPKNS_6SensorE(const void*,
                                                                         const void*** list) {
    if (list) *list = nullptr;
    return 0;
}

/* Sensor const* SensorManager::getDefaultSensor(int type) */
EXPORT const void* _ZN7android13SensorManager16getDefaultSensorEi(void*, int) {
    return nullptr;
}

namespace {
/* stands in for KitKat's SensorEventQueue (: ASensorEventQueue, an empty struct, and
 * RefBase, so a RefBase at offset 0 that gpsd's sp<> counts through libutils): a pipe
 * nobody writes to, as no sensor is ever enabled */
class EventQueue : public android::RefBase {
  public:
    EventQueue() {
        if (pipe2(mFds, O_CLOEXEC | O_NONBLOCK)) mFds[0] = mFds[1] = -1;
    }
    ~EventQueue() override {
        close(mFds[0]);
        close(mFds[1]);
    }
    int fd() const { return mFds[0]; }

  private:
    int mFds[2];
};
}  // namespace

/* sp<SensorEventQueue> SensorManager::createEventQueue() */
EXPORT void _ZN7android13SensorManager16createEventQueueEv(void** result, void*) {
    auto* queue = new EventQueue();
    queue->incStrong(result);
    *result = queue;
}

/* SensorEventQueue: no events, no sensors to enable */
EXPORT ssize_t _ZN7android16SensorEventQueue4readEP12ASensorEventj(void*, void*, size_t) {
    return 0;
}
EXPORT int _ZNK7android16SensorEventQueue5getFdEv(const void* self) {
    return static_cast<const EventQueue*>(static_cast<const android::RefBase*>(self))->fd();
}
EXPORT int _ZNK7android16SensorEventQueue12enableSensorEPKNS_6SensorE(const void*, const void*) {
    return -1;
}
EXPORT int _ZNK7android16SensorEventQueue13disableSensorEPKNS_6SensorE(const void*, const void*) {
    return -1;
}
EXPORT int _ZNK7android16SensorEventQueue12setEventRateEPKNS_6SensorEx(const void*, const void*,
                                                                      long long) {
    return -1;
}

/* Sensor getters (no Sensor objects exist) */
EXPORT float _ZNK7android6Sensor11getMaxValueEv(const void*) { return 0; }
EXPORT int _ZNK7android6Sensor11getMinDelayEv(const void*) { return 0; }
EXPORT float _ZNK7android6Sensor13getPowerUsageEv(const void*) { return 0; }
EXPORT float _ZNK7android6Sensor13getResolutionEv(const void*) { return 0; }
EXPORT int _ZNK7android6Sensor7getTypeEv(const void*) { return 0; }
EXPORT int _ZNK7android6Sensor9getHandleEv(const void*) { return 0; }
EXPORT const void* _ZNK7android6Sensor7getNameEv(const void*) { return nullptr; }  // String8 const&
EXPORT const void* _ZNK7android6Sensor9getVendorEv(const void*) { return nullptr; }

/* libwrappergps: there is no Samsung multiclient RIL socket, but gpsd gives up entirely
 * ("Failed to start RIL client") unless it can open and connect a client. Hand it one
 * that accepts every registration and request and never answers: no frequency aiding
 * from the modem and no control-plane AGPS, standalone GPS works. */
static int sRilClient;
EXPORT void* wrapperOpenClient_RILD() { return &sRilClient; }
EXPORT int wrapperCloseClient_RILD(void*) { return 0; }
EXPORT int wrapperConnect_RILD(void*) { return 0; }
EXPORT int wrapperisConnected_RILD(void*) { return 1; }
EXPORT int wrapperInvokeOemRequestHookRaw(void*, char*, size_t) { return 0; }
EXPORT int wrapperRegisterErrorCallback(void*, void*, void*) { return 0; }
EXPORT int wrapperRegisterRequestCompleteHandler(void*, unsigned, void*) { return 0; }
EXPORT int wrapperRegisterUnsolicitedHandler(void*, unsigned, void*) { return 0; }

EXPORT const SSL_METHOD* SSLv3_client_method(void) {
    return TLS_client_method();
}
