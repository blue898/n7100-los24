#pragma once

#include <aidl/android/hardware/radio/AccessNetwork.h>
#include <aidl/android/hardware/radio/RadioAccessFamily.h>
#include <aidl/android/hardware/radio/RadioError.h>
#include <aidl/android/hardware/radio/RadioIndicationType.h>
#include <aidl/android/hardware/radio/RadioResponseInfo.h>
#include <aidl/android/hardware/radio/RadioTechnology.h>
#include <android-base/logging.h>
#include <telephony/ril.h>

#include <string>
#include <vector>

#include "libril/ril_core.h"

// ril.h macros that collide with constants of the AIDL types
#undef CDMA_ALPHA_INFO_BUFFER_LENGTH
#undef CDMA_NUMBER_INFO_BUFFER_LENGTH
#undef NUM_SERVICE_CLASSES
#undef SS_INFO_MAX

namespace android::hardware::radio::n7100 {

namespace aidl_radio = ::aidl::android::hardware::radio;

constexpr auto kUnsol = aidl_radio::RadioIndicationType::UNSOLICITED;

inline aidl_radio::RadioResponseInfo rsp(int32_t serial, RIL_Errno e) {
    return {.type = aidl_radio::RadioResponseType::SOLICITED,
            .serial = serial,
            .error = static_cast<aidl_radio::RadioError>(e)};
}

inline aidl_radio::RadioResponseInfo notSupported(int32_t serial) {
    return rsp(serial, RIL_E_REQUEST_NOT_SUPPORTED);
}

inline aidl_radio::RadioResponseInfo ok(int32_t serial) {
    return rsp(serial, RIL_E_SUCCESS);
}

inline void logUnsupported(const char* what) {
    LOG(DEBUG) << what << ": not supported";
}

inline std::string str(const char* s) {
    return s ? s : "";
}

// legacy responses: int[] and char*[]
inline int intAt(void* r, size_t len, size_t i, int def = 0) {
    return (r && len >= (i + 1) * sizeof(int)) ? static_cast<int*>(r)[i] : def;
}

inline std::vector<std::string> stringsOf(void* r, size_t len) {
    std::vector<std::string> v;
    if (!r) return v;
    auto** s = static_cast<char**>(r);
    for (size_t i = 0; i < len / sizeof(char*); i++) v.push_back(str(s[i]));
    return v;
}

inline int atoiOr(const std::string& s, int def, int base = 10) {
    if (s.empty()) return def;
    char* end;
    long v = strtol(s.c_str(), &end, base);
    return *end ? def : static_cast<int>(v);
}

inline std::vector<uint8_t> hexToBytes(const char* hex) {
    std::vector<uint8_t> out;
    if (!hex) return out;
    size_t n = strlen(hex) / 2;
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1) break;
        out.push_back(v);
    }
    return out;
}

using n7100ril::Payload;

inline void ril(int id, std::unique_ptr<Payload> p, n7100ril::ResponseHandler done) {
    n7100ril::request(id, std::move(p), std::move(done));
}

}  // namespace android::hardware::radio::n7100
