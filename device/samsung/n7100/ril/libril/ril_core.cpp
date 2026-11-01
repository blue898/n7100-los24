/*
 * Host for Samsung's legacy RIL, see ril_core.h. Request quirks of libsec-ril
 * (formerly LineageOS' libsecril-shim) live in the radio service, not here.
 */
#define LOG_TAG "RILC"
// RIL_Env and friends: the interface between libril and a vendor RIL
#define RIL_SHLIB

#include "ril_core.h"

#include <dlfcn.h>
#include <inttypes.h>
#include <log/log.h>
#include <string.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace n7100ril {

std::unique_ptr<Payload> Payload::ints(std::vector<int> v) {
    auto p = std::make_unique<Payload>();
    p->mInts = std::move(v);
    p->data = p->mInts.data();
    p->len = p->mInts.size() * sizeof(int);
    return p;
}

std::unique_ptr<Payload> Payload::strings(const std::vector<std::optional<std::string>>& v) {
    auto p = std::make_unique<Payload>();
    for (const auto& s : v) p->mPtrs.push_back(s ? p->keep(*s) : nullptr);
    p->data = p->mPtrs.data();
    p->len = p->mPtrs.size() * sizeof(char*);
    return p;
}

std::unique_ptr<Payload> Payload::string(const std::string& s) {
    auto p = std::make_unique<Payload>();
    p->data = p->keep(s);
    p->len = sizeof(char*);
    return p;
}

namespace {

constexpr uint32_t kTokenMagic = 0x6e373130;  // "n710"

struct Token {
    uint32_t magic = kTokenMagic;
    int id;
    ResponseHandler done;
};

struct Request {
    int id;
    std::unique_ptr<Payload> payload;
    ResponseHandler done;
};

const RIL_RadioFunctions* gFuncs;
UnsolHandler gUnsol;

std::mutex gQueueLock;
std::condition_variable gQueueCv;
std::deque<Request> gQueue;

std::mutex gTokenLock;
std::set<Token*> gTokens;

struct Timed {
    RIL_TimedCallback cb;
    void* param;
};
std::mutex gTimerLock;
std::condition_variable gTimerCv;
std::multimap<std::chrono::steady_clock::time_point, Timed> gTimers;

void requestLoop() {
    for (;;) {
        Request r;
        {
            std::unique_lock lk(gQueueLock);
            gQueueCv.wait(lk, [] { return !gQueue.empty(); });
            r = std::move(gQueue.front());
            gQueue.pop_front();
        }
        auto* t = new Token{.id = r.id, .done = std::move(r.done)};
        {
            std::lock_guard lk(gTokenLock);
            gTokens.insert(t);
        }
        ALOGD("> %s", requestName(r.id));
        gFuncs->onRequest(r.id, r.payload->data, r.payload->len, t);
    }
}

void timerLoop() {
    std::unique_lock lk(gTimerLock);
    for (;;) {
        if (gTimers.empty()) {
            gTimerCv.wait(lk);
            continue;
        }
        auto it = gTimers.begin();
        if (it->first > std::chrono::steady_clock::now()) {
            gTimerCv.wait_until(lk, it->first);
            continue;
        }
        Timed t = it->second;
        gTimers.erase(it);
        lk.unlock();
        t.cb(t.param);
        lk.lock();
    }
}

void onRequestComplete(RIL_Token token, RIL_Errno e, void* response, size_t len) {
    auto* t = static_cast<Token*>(token);
    {
        std::lock_guard lk(gTokenLock);
        if (!t || !gTokens.erase(t) || t->magic != kTokenMagic) {
            ALOGE("completion for unknown token %p", token);
            return;
        }
    }
    ALOGD("< %s e=%d len=%zu", requestName(t->id), e, len);
    if (t->done) t->done(e, response, len);
    t->magic = 0;
    delete t;
}

void onUnsolicitedResponse(int id, const void* data, size_t len) {
    ALOGD("[UNSL] %d len=%zu", id, len);
    if (gUnsol) gUnsol(id, data, len);
}

void requestTimedCallback(RIL_TimedCallback cb, void* param, const struct timeval* rel) {
    auto when = std::chrono::steady_clock::now();
    if (rel) when += std::chrono::seconds(rel->tv_sec) + std::chrono::microseconds(rel->tv_usec);
    std::lock_guard lk(gTimerLock);
    gTimers.emplace(when, Timed{cb, param});
    gTimerCv.notify_one();
}

void onRequestAck(RIL_Token) {}

const RIL_Env gEnv = {
        .OnRequestComplete = onRequestComplete,
        .OnUnsolicitedResponse = onUnsolicitedResponse,
        .RequestTimedCallback = requestTimedCallback,
        .OnRequestAck = onRequestAck,
};

/*
 * libsec-ril waits on pthread_cond_timedwait_relative_np(MAX_TIMEOUT = 0x7FFFFFFF s);
 * bionic converts that to an absolute time, which overflows. 0x01FFFFFF s is a year.
 * (from LineageOS' libsecril-shim)
 */
void patchMaxTimeout(void* lib) {
    auto* maxTimeout = static_cast<uint32_t*>(dlsym(lib, "MAX_TIMEOUT"));
    if (maxTimeout && *maxTimeout == 0x7FFFFFFF) {
        *maxTimeout = 0x01FFFFFF;
        ALOGI("patched MAX_TIMEOUT");
    }
}

}  // namespace

bool start(const char* libPath, const std::vector<std::string>& args, UnsolHandler unsol) {
    gUnsol = std::move(unsol);
    std::thread(requestLoop).detach();
    std::thread(timerLoop).detach();

    void* lib = dlopen(libPath, RTLD_NOW | RTLD_GLOBAL);
    if (!lib) {
        ALOGE("dlopen %s: %s", libPath, dlerror());
        return false;
    }
    patchMaxTimeout(lib);
    auto init = reinterpret_cast<const RIL_RadioFunctions* (*)(const RIL_Env*, int, char**)>(
            dlsym(lib, "RIL_Init"));
    if (!init) {
        ALOGE("no RIL_Init in %s", libPath);
        return false;
    }
    static std::vector<std::string> sArgs = args;
    static std::vector<char*> sArgv;
    for (auto& a : sArgs) sArgv.push_back(a.data());
    sArgv.push_back(nullptr);
    gFuncs = init(&gEnv, sArgs.size(), sArgv.data());
    if (!gFuncs) {
        ALOGE("RIL_Init failed");
        return false;
    }
    ALOGI("%s: RIL version %d (%s)", libPath, gFuncs->version,
          gFuncs->getVersion ? gFuncs->getVersion() : "?");
    return true;
}

void request(int id, std::unique_ptr<Payload> payload, ResponseHandler done) {
    if (!gFuncs) {
        if (done) done(RIL_E_RADIO_NOT_AVAILABLE, nullptr, 0);
        return;
    }
    std::lock_guard lk(gQueueLock);
    gQueue.push_back(Request{id, payload ? std::move(payload) : Payload::none(), std::move(done)});
    gQueueCv.notify_one();
}

int radioState() {
    return gFuncs ? gFuncs->onStateRequest() : RADIO_STATE_UNAVAILABLE;
}

const char* requestName(int id) {
    static const std::map<int, const char*> kNames = {
            {RIL_REQUEST_GET_SIM_STATUS, "GET_SIM_STATUS"},
            {RIL_REQUEST_ENTER_SIM_PIN, "ENTER_SIM_PIN"},
            {RIL_REQUEST_ENTER_SIM_PUK, "ENTER_SIM_PUK"},
            {RIL_REQUEST_GET_CURRENT_CALLS, "GET_CURRENT_CALLS"},
            {RIL_REQUEST_DIAL, "DIAL"},
            {RIL_REQUEST_GET_IMSI, "GET_IMSI"},
            {RIL_REQUEST_HANGUP, "HANGUP"},
            {RIL_REQUEST_LAST_CALL_FAIL_CAUSE, "LAST_CALL_FAIL_CAUSE"},
            {RIL_REQUEST_SIGNAL_STRENGTH, "SIGNAL_STRENGTH"},
            {RIL_REQUEST_VOICE_REGISTRATION_STATE, "VOICE_REGISTRATION_STATE"},
            {RIL_REQUEST_DATA_REGISTRATION_STATE, "DATA_REGISTRATION_STATE"},
            {RIL_REQUEST_OPERATOR, "OPERATOR"},
            {RIL_REQUEST_RADIO_POWER, "RADIO_POWER"},
            {RIL_REQUEST_SEND_SMS, "SEND_SMS"},
            {RIL_REQUEST_SETUP_DATA_CALL, "SETUP_DATA_CALL"},
            {RIL_REQUEST_SIM_IO, "SIM_IO"},
            {RIL_REQUEST_SEND_USSD, "SEND_USSD"},
            {RIL_REQUEST_GET_IMEI, "GET_IMEI"},
            {RIL_REQUEST_GET_IMEISV, "GET_IMEISV"},
            {RIL_REQUEST_ANSWER, "ANSWER"},
            {RIL_REQUEST_DEACTIVATE_DATA_CALL, "DEACTIVATE_DATA_CALL"},
            {RIL_REQUEST_QUERY_FACILITY_LOCK, "QUERY_FACILITY_LOCK"},
            {RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE, "QUERY_NETWORK_SELECTION_MODE"},
            {RIL_REQUEST_BASEBAND_VERSION, "BASEBAND_VERSION"},
            {RIL_REQUEST_DATA_CALL_LIST, "DATA_CALL_LIST"},
            {RIL_REQUEST_SCREEN_STATE, "SCREEN_STATE"},
            {RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE, "SET_PREFERRED_NETWORK_TYPE"},
            {RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE, "GET_PREFERRED_NETWORK_TYPE"},
            {RIL_REQUEST_GET_SMSC_ADDRESS, "GET_SMSC_ADDRESS"},
    };
    auto it = kNames.find(id);
    if (it != kNames.end()) return it->second;
    static thread_local char buf[32];
    snprintf(buf, sizeof(buf), "REQUEST_%d", id);
    return buf;
}

}  // namespace n7100ril

// The symbols libsec-ril.so imports from libril.so
extern "C" __attribute__((visibility("default"))) void RIL_onRequestComplete(RIL_Token t, RIL_Errno e,
                                                                             void* response,
                                                                             size_t len) {
    n7100ril::onRequestComplete(t, e, response, len);
}

extern "C" __attribute__((visibility("default"))) void RIL_onUnsolicitedResponse(int id,
                                                                                 const void* data,
                                                                                 size_t len) {
    n7100ril::onUnsolicitedResponse(id, data, len);
}

extern "C" __attribute__((visibility("default"))) const char* requestToString(int request) {
    return n7100ril::requestName(request);
}
