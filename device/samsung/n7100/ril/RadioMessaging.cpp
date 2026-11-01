#include "RadioServices.h"

namespace android::hardware::radio::n7100 {

void RadioMessaging::sendGsmSms(int32_t serial, const amsg::GsmSmsMessage& message,
                                bool expectMore) {
    // libsec-ril has no SEND_SMS_EXPECT_MORE (shim): plain SEND_SMS for both
    ril(RIL_REQUEST_SEND_SMS,
        Payload::strings({message.smscPdu.empty() ? std::nullopt : std::optional(message.smscPdu),
                          message.pdu}),
        [this, serial, expectMore](RIL_Errno e, void* r, size_t len) {
            amsg::SendSmsResult res{.messageRef = 0, .ackPDU = "", .errorCode = -1};
            if (r && len >= sizeof(RIL_SMS_Response)) {
                auto* s = static_cast<RIL_SMS_Response*>(r);
                res = {.messageRef = s->messageRef,
                       .ackPDU = str(s->ackPDU),
                       .errorCode = s->errorCode};
            }
            if (!mResp) return;
            if (expectMore)
                mResp->sendSmsExpectMoreResponse(rsp(serial, e), res);
            else
                mResp->sendSmsResponse(rsp(serial, e), res);
        });
}

ScopedAStatus RadioMessaging::sendSms(int32_t serial, const amsg::GsmSmsMessage& message) {
    sendGsmSms(serial, message, false);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::sendSmsExpectMore(int32_t serial, const amsg::GsmSmsMessage& message) {
    sendGsmSms(serial, message, true);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::acknowledgeLastIncomingGsmSms(int32_t serial, bool success,
                                                           amsg::SmsAcknowledgeFailCause cause) {
    ril(RIL_REQUEST_SMS_ACKNOWLEDGE, Payload::ints({success ? 1 : 0, static_cast<int>(cause)}),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->acknowledgeLastIncomingGsmSmsResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::getSmscAddress(int32_t serial) {
    ril(RIL_REQUEST_GET_SMSC_ADDRESS, Payload::none(), [this, serial](RIL_Errno e, void* r, size_t) {
        if (mResp) mResp->getSmscAddressResponse(rsp(serial, e), str(static_cast<char*>(r)));
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::setSmscAddress(int32_t serial, const std::string& smsc) {
    ril(RIL_REQUEST_SET_SMSC_ADDRESS, Payload::string(smsc), [this, serial](RIL_Errno e, void*, size_t) {
        if (mResp) mResp->setSmscAddressResponse(rsp(serial, e));
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::writeSmsToSim(int32_t serial, const amsg::SmsWriteArgs& args) {
    auto p = std::make_unique<Payload>();
    auto* w = p->make<RIL_SMS_WriteArgs>();
    w->status = args.status;
    w->pdu = p->keep(args.pdu);
    w->smsc = p->keepOrNull(args.smsc);
    ril(RIL_REQUEST_WRITE_SMS_TO_SIM, std::move(p), [this, serial](RIL_Errno e, void* r, size_t len) {
        if (mResp) mResp->writeSmsToSimResponse(rsp(serial, e), intAt(r, len, 0, -1));
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::deleteSmsOnSim(int32_t serial, int32_t index) {
    ril(RIL_REQUEST_DELETE_SMS_ON_SIM, Payload::ints({index}), [this, serial](RIL_Errno e, void*, size_t) {
        if (mResp) mResp->deleteSmsOnSimResponse(rsp(serial, e));
    });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::reportSmsMemoryStatus(int32_t serial, bool available) {
    ril(RIL_REQUEST_REPORT_SMS_MEMORY_STATUS, Payload::ints({available ? 1 : 0}),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->reportSmsMemoryStatusResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::setGsmBroadcastConfig(
        int32_t serial, const std::vector<amsg::GsmBroadcastSmsConfigInfo>& configInfo) {
    auto p = std::make_unique<Payload>();
    struct Configs {
        RIL_GSM_BroadcastSmsConfigInfo* ptrs[32];
        RIL_GSM_BroadcastSmsConfigInfo infos[32];
    };
    auto* c = p->make<Configs>();
    size_t n = std::min<size_t>(configInfo.size(), 32);
    for (size_t i = 0; i < n; i++) {
        c->infos[i] = {.fromServiceId = configInfo[i].fromServiceId,
                       .toServiceId = configInfo[i].toServiceId,
                       .fromCodeScheme = configInfo[i].fromCodeScheme,
                       .toCodeScheme = configInfo[i].toCodeScheme,
                       .selected = configInfo[i].selected};
        c->ptrs[i] = &c->infos[i];
    }
    p->len = n * sizeof(RIL_GSM_BroadcastSmsConfigInfo*);
    ril(RIL_REQUEST_GSM_SET_BROADCAST_SMS_CONFIG, std::move(p),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->setGsmBroadcastConfigResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

// legacy: 0 activates, 1 turns off
ScopedAStatus RadioMessaging::setGsmBroadcastActivation(int32_t serial, bool activate) {
    ril(RIL_REQUEST_GSM_SMS_BROADCAST_ACTIVATION, Payload::ints({activate ? 0 : 1}),
        [this, serial](RIL_Errno e, void*, size_t) {
            if (mResp) mResp->setGsmBroadcastActivationResponse(rsp(serial, e));
        });
    return ScopedAStatus::ok();
}

}  // namespace android::hardware::radio::n7100
