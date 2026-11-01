/*
 * Host for Samsung's legacy RIL (libsec-ril.so from the stock N7100 firmware).
 *
 * Replaces AOSP's libril: loads the vendor RIL, hands it a RIL_Env, serializes
 * requests on one thread and routes completions, unsolicited responses and timed
 * callbacks back. Exports what libsec-ril.so imports from libril.so
 * (RIL_onRequestComplete, requestToString); the blob's NEEDED entry points here.
 */
#pragma once

#include <telephony/ril.h>

#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Radio states of RIL versions < 7, which libsec-ril still reports
enum {
    RADIO_STATE_SIM_NOT_READY = 2,
    RADIO_STATE_SIM_LOCKED_OR_ABSENT = 3,
    RADIO_STATE_SIM_READY = 4,
    RADIO_STATE_RUIM_NOT_READY = 5,
    RADIO_STATE_RUIM_READY = 6,
    RADIO_STATE_RUIM_LOCKED_OR_ABSENT = 7,
    RADIO_STATE_NV_NOT_READY = 8,
    RADIO_STATE_NV_READY = 9,
};

namespace n7100ril {

// Request data in the layout the legacy RIL expects, kept alive until onRequest returns
class Payload {
  public:
    void* data = nullptr;
    size_t len = 0;

    static std::unique_ptr<Payload> none() { return std::make_unique<Payload>(); }
    static std::unique_ptr<Payload> ints(std::vector<int> v);
    // char** of v.size() entries; nullopt -> NULL
    static std::unique_ptr<Payload> strings(const std::vector<std::optional<std::string>>& v);
    // a single char*, as libril's dispatchString
    static std::unique_ptr<Payload> string(const std::string& s);

    // a zeroed struct T owned by the payload
    template <class T>
    T* make() {
        mRaw.assign(sizeof(T), 0);
        data = mRaw.data();
        len = sizeof(T);
        return reinterpret_cast<T*>(mRaw.data());
    }
    // a stable C string owned by the payload
    char* keep(const std::string& s) {
        mStrings.push_back(s);
        return mStrings.back().data();
    }
    char* keepOrNull(const std::string& s) { return s.empty() ? nullptr : keep(s); }

  private:
    std::list<std::string> mStrings;
    std::vector<char*> mPtrs;
    std::vector<int> mInts;
    std::vector<uint8_t> mRaw;
};

using ResponseHandler = std::function<void(RIL_Errno e, void* response, size_t len)>;
using UnsolHandler = std::function<void(int id, const void* data, size_t len)>;

// Start the vendor RIL; unsolicited responses go to `unsol` (on RIL threads)
bool start(const char* libPath, const std::vector<std::string>& args, UnsolHandler unsol);

// Queue a request; `done` runs on whatever thread the RIL completes it on
void request(int id, std::unique_ptr<Payload> payload, ResponseHandler done);

// Current radio state as the legacy RIL reports it (RADIO_STATE_*)
int radioState();

const char* requestName(int id);

}  // namespace n7100ril
