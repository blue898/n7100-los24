/*
 * What Samsung's 2014 libsec-ril.so and its libstlport.so need that Android 17's vendor
 * libraries no longer have. Replaces LineageOS' libcutils-v29.so (an Android 10 libcutils,
 * which doesn't load against today's libbase): libsec-ril's NEEDED entry for it points
 * here; property_*, socket_local_* and record_stream_* come from libcutils/librilutils.
 */
#include <stdlib.h>
#include <string.h>
#include <utils/Unicode.h>

// from libcutils' old jstring.h: a malloc'ed UTF-16 copy of a UTF-8 string
extern "C" __attribute__((visibility("default"))) char16_t* strdup8to16(const char* s,
                                                                        size_t* outLen) {
    if (!s) return nullptr;
    size_t len = strlen(s);
    ssize_t n = utf8_to_utf16_length(reinterpret_cast<const uint8_t*>(s), len);
    if (n < 0) n = 0;
    auto* out = static_cast<char16_t*>(malloc((n + 1) * sizeof(char16_t)));
    if (!out) return nullptr;
    utf8_to_utf16(reinterpret_cast<const uint8_t*>(s), len, out, n + 1);
    if (outLen) *outLen = n;
    return out;
}

/*
 * ARM EHABI catch matching of the old gabi++/libsupc++ runtime, which libstlport calls.
 * libsec-ril doesn't throw across these frames; answer "no match" (ctm_failed).
 */
extern "C" __attribute__((visibility("default"))) int __cxa_type_match(void*, const void*, bool,
                                                                       void**) {
    return 0;
}
