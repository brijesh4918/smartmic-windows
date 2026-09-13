#include "smartmic/media/opus_codec.h"

#include <opus.h>

#include <algorithm>

#include "smartmic/logging.h"

namespace smartmic::media {
namespace {
constexpr char kComponent[] = "OpusCodec";
// Generous: a 20 ms mono frame at any sane bitrate is well under this.
constexpr size_t kMaxPacketBytes = 1500;
}

OpusEncoderWrapper::OpusEncoderWrapper() = default;
OpusEncoderWrapper::~OpusEncoderWrapper() { close(); }

bool OpusEncoderWrapper::open(const EncoderConfig& cfg) {
    close();
    int err = 0;
    enc_ = opus_encoder_create(static_cast<opus_int32>(kSampleRate), static_cast<int>(kChannels),
                               OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !enc_) {
        logError(kComponent, std::string("opus_encoder_create failed: ") + opus_strerror(err));
        enc_ = nullptr;
        return false;
    }
    cfg_ = cfg;
    opus_encoder_ctl(enc_, OPUS_SET_BITRATE(cfg.bitrateBps));
    opus_encoder_ctl(enc_, OPUS_SET_INBAND_FEC(cfg.useInbandFec ? 1 : 0));
    opus_encoder_ctl(enc_, OPUS_SET_PACKET_LOSS_PERC(cfg.expectedPacketLossPct));
    opus_encoder_ctl(enc_, OPUS_SET_DTX(cfg.dtx ? 1 : 0));
    opus_encoder_ctl(enc_, OPUS_SET_COMPLEXITY(cfg.complexity));
    opus_encoder_ctl(enc_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    return true;
}

void OpusEncoderWrapper::close() {
    if (enc_) { opus_encoder_destroy(enc_); enc_ = nullptr; }
}

void OpusEncoderWrapper::setPacketLossPercent(int pct) {
    if (!enc_) return;
    cfg_.expectedPacketLossPct = std::clamp(pct, 0, 100);
    opus_encoder_ctl(enc_, OPUS_SET_PACKET_LOSS_PERC(cfg_.expectedPacketLossPct));
}

bool OpusEncoderWrapper::encode(const float* pcm, size_t samples, std::vector<uint8_t>& out) {
    if (!enc_ || samples != kFrameSamples) return false;
    out.resize(kMaxPacketBytes);
    const opus_int32 n = opus_encode_float(enc_, pcm, static_cast<int>(samples), out.data(),
                                           static_cast<opus_int32>(out.size()));
    if (n < 0) {
        logError(kComponent, std::string("opus_encode_float failed: ") + opus_strerror(n));
        out.clear();
        return false;
    }
    out.resize(static_cast<size_t>(n));
    return true;
}

OpusDecoderWrapper::OpusDecoderWrapper() = default;
OpusDecoderWrapper::~OpusDecoderWrapper() { close(); }

bool OpusDecoderWrapper::open() {
    close();
    int err = 0;
    dec_ = opus_decoder_create(static_cast<opus_int32>(kSampleRate), static_cast<int>(kChannels), &err);
    if (err != OPUS_OK || !dec_) {
        logError(kComponent, std::string("opus_decoder_create failed: ") + opus_strerror(err));
        dec_ = nullptr;
        return false;
    }
    return true;
}

void OpusDecoderWrapper::close() {
    if (dec_) { opus_decoder_destroy(dec_); dec_ = nullptr; }
}

bool OpusDecoderWrapper::decode(const uint8_t* data, size_t len, float* pcm, size_t samples) {
    if (!dec_ || samples != kFrameSamples) return false;
    const int n = opus_decode_float(dec_, data, static_cast<opus_int32>(len), pcm,
                                    static_cast<int>(samples), 0);
    if (n < 0) {
        // A corrupt packet is concealed, not propagated as garbage audio.
        return conceal(pcm, samples);
    }
    if (static_cast<size_t>(n) < samples) {
        std::fill(pcm + n, pcm + samples, 0.0f);
    }
    return true;
}

bool OpusDecoderWrapper::conceal(float* pcm, size_t samples) {
    if (!dec_ || samples != kFrameSamples) return false;
    const int n = opus_decode_float(dec_, nullptr, 0, pcm, static_cast<int>(samples), 0);
    if (n < 0) {
        std::fill(pcm, pcm + samples, 0.0f);
        return false;
    }
    return true;
}

}  // namespace smartmic::media
