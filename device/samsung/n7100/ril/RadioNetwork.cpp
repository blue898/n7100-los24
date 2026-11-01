#include "RadioServices.h"

#include <aidl/android/hardware/radio/network/IndicationFilter.h>

#include <climits>
#include <map>

namespace android::hardware::radio::n7100 {

using RAF = aidl_radio::RadioAccessFamily;
using aidl_radio::RadioTechnology;

static constexpr int kUnavail = INT_MAX;

static int raf(RAF f) {
    return static_cast<int>(f);
}
static const int kRaf2g = raf(RAF::GSM) | raf(RAF::GPRS) | raf(RAF::EDGE);
static const int kRaf3g =
        raf(RAF::UMTS) | raf(RAF::HSDPA) | raf(RAF::HSUPA) | raf(RAF::HSPA) | raf(RAF::HSPAP);

static bool is3g(int rat) {
    switch (static_cast<RadioTechnology>(rat)) {
        case RadioTechnology::UMTS:
        case RadioTechnology::HSDPA:
        case RadioTechnology::HSUPA:
        case RadioTechnology::HSPA:
        case RadioTechnology::HSPAP:
            return true;
        default:
            return false;
    }
}

/*
 * Every field "unavailable", then GSM/WCDMA from the legacy GW signal strength.
 * libsec-ril packs it strangely: the ASU is in the low byte, and when that's out
 * of range the value is in CDMA_SignalStrength.dbm (from LineageOS' libsecril-shim).
 */
anet::SignalStrength RadioNetwork::toSignalStrength(void* r, size_t len) {
    anet::SignalStrength ss{};
    ss.gsm = {kUnavail, kUnavail, kUnavail};
    ss.wcdma = {kUnavail, kUnavail, kUnavail, kUnavail};
    ss.cdma = {kUnavail, kUnavail};
    ss.evdo = {kUnavail, kUnavail, kUnavail};
    ss.lte = {kUnavail, kUnavail, kUnavail, kUnavail, kUnavail, kUnavail, kUnavail};
    ss.tdscdma = {kUnavail, kUnavail, kUnavail};
    ss.nr.ssRsrp = ss.nr.ssRsrq = ss.nr.ssSinr = kUnavail;
    ss.nr.csiRsrp = ss.nr.csiRsrq = ss.nr.csiSinr = kUnavail;
    ss.nr.csiCqiTableIndex = kUnavail;
    if (!r || len < sizeof(RIL_SignalStrength_v5)) return ss;

    auto* s = static_cast<RIL_SignalStrength_v5*>(r);
    int asu = s->GW_SignalStrength.signalStrength & 0xFF;
    if (asu < 0 || (asu > 31 && s->GW_SignalStrength.signalStrength != 99))
        asu = s->CDMA_SignalStrength.dbm;
    if (asu < 0 || (asu > 31 && asu != 99)) asu = 99;
    int ber = s->GW_SignalStrength.bitErrorRate;
    if (ber < 0 || (ber > 7 && ber != 99)) ber = 99;

    int rat;
    {
        std::lock_guard lk(state().lock);
        rat = state().dataRat;
    }
    // the framework reads the RAT-specific part; fill both, WCDMA without RSCP/EcNo
    ss.gsm = {.signalStrength = asu, .bitErrorRate = ber, .timingAdvance = kUnavail};
    ss.wcdma = {.signalStrength = asu, .bitErrorRate = ber, .rscp = kUnavail, .ecno = kUnavail};
    (void)rat;
    return ss;
}

ScopedAStatus RadioNetwork::getSignalStrength(int32_t serial) {
    ril(RIL_REQUEST_SIGNAL_STRENGTH, Payload::none(), [this, serial](RIL_Errno e, void* r, size_t len) {
        if (mResp) mResp->getSignalStrengthResponse(rsp(serial, e), toSignalStrength(r, len));
    });
    return ScopedAStatus::ok();
}

static std::pair<std::string, std::string> mccMnc(const std::string& numeric) {
    if (numeric.size() < 5) return {"", ""};
    return {numeric.substr(0, 3), numeric.substr(3)};
}

/*
 * VOICE_REGISTRATION_STATE: [0] state [1] LAC [2] CID (hex) [3] RAT [13] reject cause;
 * libsec-ril leaves the RAT out (the shim forced UMTS), so take the data RAT.
 * DATA_REGISTRATION_STATE: [0] state [1] LAC [2] CID [3] RAT [4] reject cause.
 */
anet::RegStateResult RadioNetwork::toRegState(const std::vector<std::string>& s, bool voice) {
    anet::RegStateResult res{};
    auto at = [&s](size_t i) { return i < s.size() ? s[i] : std::string(); };
    int reg = atoiOr(at(0), 4);
    int rat = atoiOr(at(3), 0);
    std::string numeric;
    {
        std::lock_guard lk(state().lock);
        if (!voice && rat) state().dataRat = rat;
        if (voice && !rat) rat = state().dataRat ? state().dataRat : int(RadioTechnology::UMTS);
        numeric = state().operatorNumeric;
    }
    res.regState = static_cast<anet::RegState>(reg);
    res.rat = static_cast<RadioTechnology>(rat);
    res.reasonForDenial =
            static_cast<anet::RegistrationFailCause>(atoiOr(at(voice ? 13 : 4), 0));
    bool registered = reg == 1 || reg == 5;
    if (registered) res.registeredPlmn = numeric;

    int lac = atoiOr(at(1), -1, 16), cid = atoiOr(at(2), -1, 16);
    auto [mcc, mnc] = mccMnc(numeric);
    if (is3g(rat)) {
        anet::CellIdentityWcdma w{};
        w.mcc = mcc;
        w.mnc = mnc;
        w.lac = lac < 0 ? kUnavail : lac;
        w.cid = cid < 0 ? kUnavail : cid;
        w.psc = kUnavail;
        w.uarfcn = kUnavail;
        res.cellIdentity = anet::CellIdentity::make<anet::CellIdentity::wcdma>(w);
    } else if (rat) {
        anet::CellIdentityGsm g{};
        g.mcc = mcc;
        g.mnc = mnc;
        g.lac = lac < 0 ? kUnavail : lac;
        g.cid = cid < 0 ? kUnavail : cid;
        g.arfcn = kUnavail;
        g.bsic = static_cast<int8_t>(0xFF);
        res.cellIdentity = anet::CellIdentity::make<anet::CellIdentity::gsm>(g);
    }
    return res;
}

ScopedAStatus RadioNetwork::getVoiceRegistrationState(int32_t serial) {
    ril(RIL_REQUEST_VOICE_REGISTRATION_STATE, Payload::none(),
        [this, serial](RIL_Errno e, void* r, size_t len) {
            if (mResp)
                mResp->getVoiceRegistrationStateResponse(rsp(serial, e),
                                                         toRegState(stringsOf(r, len), true));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getDataRegistrationState(int32_t serial) {
    ril(RIL_REQUEST_DATA_REGISTRATION_STATE, Payload::none(),
        [this, serial](RIL_Errno e, void* r, size_t len) {
            if (mResp)
                mResp->getDataRegistrationStateResponse(rsp(serial, e),
                                                        toRegState(stringsOf(r, len), false));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getVoiceRadioTechnology(int32_t serial) {
    int rat;
    {
        std::lock_guard lk(state().lock);
        rat = state().dataRat ? state().dataRat : int(RadioTechnology::UMTS);
    }
    if (mResp) mResp->getVoiceRadioTechnologyResponse(ok(serial), static_cast<RadioTechnology>(rat));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getOperator(int32_t serial) {
    ril(RIL_REQUEST_OPERATOR, Payload::none(), [this, serial](RIL_Errno e, void* r, size_t len) {
        auto s = stringsOf(r, len);
        s.resize(3);
        if (e == RIL_E_SUCCESS) {
            std::lock_guard lk(state().lock);
            state().operatorNumeric = s[2];
        }
        if (mResp) mResp->getOperatorResponse(rsp(serial, e), s[0], s[1], s[2]);
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getNetworkSelectionMode(int32_t serial) {
    ril(RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE, Payload::none(),
        [this, serial](RIL_Errno e, void* r, size_t len) {
            if (mResp) mResp->getNetworkSelectionModeResponse(rsp(serial, e), intAt(r, len, 0) == 1);
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setNetworkSelectionModeAutomatic(int32_t serial) {
    ril(RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC, Payload::none(),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->setNetworkSelectionModeAutomaticResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setNetworkSelectionModeManual(int32_t serial,
                                                          const std::string& operatorNumeric,
                                                          aidl_radio::AccessNetwork) {
    ril(RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL, Payload::string(operatorNumeric),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->setNetworkSelectionModeManualResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

// 4 strings per network (long, short, numeric, status), 5 on libsec-ril (+ RAT)
ScopedAStatus RadioNetwork::getAvailableNetworks(int32_t serial) {
    ril(RIL_REQUEST_QUERY_AVAILABLE_NETWORKS, Payload::none(),
        [this, serial](RIL_Errno e, void* r, size_t len) {
            auto s = stringsOf(r, len);
            static const std::map<std::string, int> kStatus = {
                    {"unknown", 0}, {"available", 1}, {"current", 2}, {"forbidden", 3}};
            size_t stride = (s.size() % 5 == 0 && s.size() >= 5 && kStatus.count(s[3])) ? 5 : 4;
            std::vector<anet::OperatorInfo> nets;
            for (size_t i = 0; i + stride <= s.size(); i += stride) {
                auto it = kStatus.find(s[i + 3]);
                nets.push_back({.alphaLong = s[i],
                                .alphaShort = s[i + 1],
                                .operatorNumeric = s[i + 2],
                                .status = it == kStatus.end() ? 0 : it->second});
            }
            if (mResp) mResp->getAvailableNetworksResponse(rsp(serial, e), nets);
        });
    return ScopedAStatus::ok();
}

// No CELL_INFO_LIST in libsec-ril: the serving cell from the registration state
ScopedAStatus RadioNetwork::getCellInfoList(int32_t serial) {
    ril(RIL_REQUEST_VOICE_REGISTRATION_STATE, Payload::none(),
        [this, serial](RIL_Errno e, void* r, size_t len) {
            std::vector<anet::CellInfo> cells;
            if (e == RIL_E_SUCCESS) {
                auto reg = toRegState(stringsOf(r, len), true);
                anet::CellInfo ci{};
                ci.registered = true;
                ci.connectionStatus = anet::CellConnectionStatus::PRIMARY_SERVING;
                anet::SignalStrength ss = toSignalStrength(nullptr, 0);
                if (reg.cellIdentity.getTag() == anet::CellIdentity::wcdma) {
                    anet::CellInfoWcdma w{reg.cellIdentity.get<anet::CellIdentity::wcdma>(), ss.wcdma};
                    ci.ratSpecificInfo = anet::CellInfoRatSpecificInfo::make<
                            anet::CellInfoRatSpecificInfo::wcdma>(w);
                    cells.push_back(ci);
                } else if (reg.cellIdentity.getTag() == anet::CellIdentity::gsm) {
                    anet::CellInfoGsm g{reg.cellIdentity.get<anet::CellIdentity::gsm>(), ss.gsm};
                    ci.ratSpecificInfo =
                            anet::CellInfoRatSpecificInfo::make<anet::CellInfoRatSpecificInfo::gsm>(g);
                    cells.push_back(ci);
                }
            }
            if (mResp) mResp->getCellInfoListResponse(rsp(serial, e), cells);
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setCellInfoListRate(int32_t serial, int32_t) {
    if (mResp) mResp->setCellInfoListRateResponse(ok(serial));
    return ScopedAStatus::ok();
}

/*
 * Legacy network type: 0 GSM/WCDMA (WCDMA preferred), 1 GSM only, 2 WCDMA only,
 * 3 GSM/WCDMA (auto)
 */
ScopedAStatus RadioNetwork::getAllowedNetworkTypesBitmap(int32_t serial) {
    ril(RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE, Payload::none(),
        [this, serial](RIL_Errno e, void* r, size_t len) {
            int type = intAt(r, len, 0);
            int bitmap = type == 1 ? kRaf2g : type == 2 ? kRaf3g : kRaf2g | kRaf3g;
            if (mResp) mResp->getAllowedNetworkTypesBitmapResponse(rsp(serial, e), bitmap);
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setAllowedNetworkTypesBitmap(int32_t serial, int32_t bitmap) {
    bool g = bitmap & kRaf2g, w = bitmap & kRaf3g;
    int type = (g && w) || (!g && !w) ? 0 : g ? 1 : 2;
    ril(RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE, Payload::ints({type}),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->setAllowedNetworkTypesBitmapResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setBarringPassword(int32_t serial, const std::string& facility,
                                               const std::string& oldPassword,
                                               const std::string& newPassword) {
    ril(RIL_REQUEST_CHANGE_BARRING_PASSWORD, Payload::strings({facility, oldPassword, newPassword}),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->setBarringPasswordResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

/*
 * The framework drops SIGNAL_STRENGTH from the filter when the screen is off; old
 * RILs only know SCREEN_STATE, which makes libsec-ril stop the periodic signal and
 * registration reports (and the HSIC traffic that wakes the AP for them).
 */
ScopedAStatus RadioNetwork::setIndicationFilter(int32_t serial, int32_t filter) {
    int screenOn = (filter & static_cast<int>(anet::IndicationFilter::SIGNAL_STRENGTH)) ? 1 : 0;
    ril(RIL_REQUEST_SCREEN_STATE, Payload::ints({screenOn}), nullptr);
    if (mResp) mResp->setIndicationFilterResponse(ok(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setLinkCapacityReportingCriteria(int32_t serial, int32_t, int32_t,
                                                             int32_t, const std::vector<int32_t>&,
                                                             const std::vector<int32_t>&,
                                                             aidl_radio::AccessNetwork) {
    if (mResp) mResp->setLinkCapacityReportingCriteriaResponse(ok(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setSignalStrengthReportingCriteria(
        int32_t serial, const std::vector<anet::SignalThresholdInfo>&) {
    if (mResp) mResp->setSignalStrengthReportingCriteriaResponse(ok(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setSystemSelectionChannels(int32_t serial, bool,
                                                       const std::vector<anet::RadioAccessSpecifier>&) {
    if (mResp) mResp->setSystemSelectionChannelsResponse(ok(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setLocationUpdates(int32_t serial, bool enable) {
    ril(RIL_REQUEST_SET_LOCATION_UPDATES, Payload::ints({enable ? 1 : 0}),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->setLocationUpdatesResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setSuppServiceNotifications(int32_t serial, bool enable) {
    ril(RIL_REQUEST_SET_SUPP_SVC_NOTIFICATION, Payload::ints({enable ? 1 : 0}),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->setSuppServiceNotificationsResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

}  // namespace android::hardware::radio::n7100
