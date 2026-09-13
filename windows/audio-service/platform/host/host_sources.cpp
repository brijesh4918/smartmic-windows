#include "host_sources.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "smartmic/logging.h"

namespace smartmic::host {
namespace {
constexpr double kTwoPi = 6.28318530717958647692;

uint32_t readLE32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t readLE16(const unsigned char* p) {
    return static_cast<uint16_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8));
}
void writeLE32(std::FILE* f, uint32_t v) {
    unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
                          static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 24)};
    std::fwrite(b, 1, 4, f);
}
void writeLE16(std::FILE* f, uint16_t v) {
    unsigned char b[2] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8)};
    std::fwrite(b, 1, 2, f);
}
}  // namespace

// --- ToneSource ------------------------------------------------------------

ToneSource::ToneSource(std::string id, float hz, float amplitude)
    : id_(std::move(id)), hz_(hz), amp_(amplitude) {}

bool ToneSource::start() { started_ = true; return true; }
void ToneSource::stop() { started_ = false; }

SourceStatus ToneSource::read(float* out, size_t samples) {
    if (failed_) {
        std::memset(out, 0, samples * sizeof(float));
        return SourceStatus::Failed;
    }
    const double inc = kTwoPi * static_cast<double>(hz_) / static_cast<double>(kSampleRate);
    for (size_t i = 0; i < samples; ++i) {
        out[i] = amp_ * static_cast<float>(std::sin(phase_));
        phase_ += inc;
        if (phase_ > kTwoPi) phase_ -= kTwoPi;
    }
    return started_ ? SourceStatus::Ok : SourceStatus::Underrun;
}

// --- SilenceSource ---------------------------------------------------------

SourceStatus SilenceSource::read(float* out, size_t samples) {
    std::memset(out, 0, samples * sizeof(float));
    return SourceStatus::Ok;
}

// --- WavFileSource ---------------------------------------------------------

WavFileSource::WavFileSource(std::string id, std::string path, bool loop)
    : id_(std::move(id)), path_(std::move(path)), loop_(loop) {}

bool WavFileSource::start() {
    fp_ = std::fopen(path_.c_str(), "rb");
    if (!fp_) {
        logError("WavFileSource", "cannot open " + path_);
        return false;
    }
    unsigned char hdr[12];
    if (std::fread(hdr, 1, 12, fp_) != 12 || std::memcmp(hdr, "RIFF", 4) != 0 ||
        std::memcmp(hdr + 8, "WAVE", 4) != 0) {
        logError("WavFileSource", "not a RIFF/WAVE file: " + path_);
        return false;
    }
    unsigned char chunk[8];
    while (std::fread(chunk, 1, 8, fp_) == 8) {
        const uint32_t size = readLE32(chunk + 4);
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            std::vector<unsigned char> fmt(size);
            if (std::fread(fmt.data(), 1, size, fp_) != size) return false;
            channels_ = readLE16(fmt.data() + 2);
            rate_ = readLE32(fmt.data() + 4);
            const uint16_t bits = readLE16(fmt.data() + 14);
            if (bits != 16) {
                logError("WavFileSource", "only 16-bit PCM is supported");
                return false;
            }
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            dataStart_ = std::ftell(fp_);
            dataBytes_ = static_cast<long>(size);
            break;
        } else {
            std::fseek(fp_, static_cast<long>(size + (size & 1u)), SEEK_CUR);
        }
    }
    if (dataStart_ == 0) return false;
    resampler_.reset(rate_);
    consumed_ = 0;
    return true;
}

void WavFileSource::stop() {
    if (fp_) { std::fclose(fp_); fp_ = nullptr; }
}

bool WavFileSource::refill(size_t needed) {
    constexpr size_t kChunkFrames = 1024;
    std::vector<int16_t> raw(kChunkFrames * channels_);
    std::vector<float> mono;
    while (pending_.size() - pendingPos_ < needed) {
        if (consumed_ >= dataBytes_) {
            if (!loop_) return false;
            std::fseek(fp_, dataStart_, SEEK_SET);
            consumed_ = 0;
        }
        const size_t want = std::min<size_t>(raw.size(),
                                             static_cast<size_t>(dataBytes_ - consumed_) / sizeof(int16_t));
        const size_t got = std::fread(raw.data(), sizeof(int16_t), want, fp_);
        if (got == 0) { if (!loop_) return false; consumed_ = dataBytes_; continue; }
        consumed_ += static_cast<long>(got * sizeof(int16_t));

        const size_t frames = got / channels_;
        mono.resize(frames);
        for (size_t f = 0; f < frames; ++f) {
            int acc = 0;
            for (uint16_t c = 0; c < channels_; ++c) acc += raw[f * channels_ + c];
            mono[f] = static_cast<float>(acc) / (32768.0f * static_cast<float>(channels_));
        }
        resampler_.process(mono.data(), mono.size(), pending_);
    }
    return true;
}

SourceStatus WavFileSource::read(float* out, size_t samples) {
    if (!fp_) {
        std::memset(out, 0, samples * sizeof(float));
        return SourceStatus::Failed;
    }
    const bool ok = refill(samples);
    const size_t have = pending_.size() - pendingPos_;
    const size_t n = std::min(samples, have);
    std::memcpy(out, pending_.data() + pendingPos_, n * sizeof(float));
    if (n < samples) std::memset(out + n, 0, (samples - n) * sizeof(float));
    pendingPos_ += n;
    if (pendingPos_ > 1u << 16) {
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<long>(pendingPos_));
        pendingPos_ = 0;
    }
    return ok && n == samples ? SourceStatus::Ok : SourceStatus::Underrun;
}

// --- WavFileSink -----------------------------------------------------------

WavFileSink::WavFileSink(std::string id, std::string path)
    : id_(std::move(id)), path_(std::move(path)) {}

WavFileSink::~WavFileSink() { stop(); }

bool WavFileSink::start() {
    fp_ = std::fopen(path_.c_str(), "wb");
    if (!fp_) {
        logError("WavFileSink", "cannot create " + path_);
        return false;
    }
    std::fwrite("RIFF", 1, 4, fp_);
    writeLE32(fp_, 0);  // patched in finalise()
    std::fwrite("WAVEfmt ", 1, 8, fp_);
    writeLE32(fp_, 16);
    writeLE16(fp_, 1);               // PCM
    writeLE16(fp_, static_cast<uint16_t>(kChannels));
    writeLE32(fp_, kSampleRate);
    writeLE32(fp_, kSampleRate * kChannels * 2);
    writeLE16(fp_, static_cast<uint16_t>(kChannels * 2));
    writeLE16(fp_, 16);
    std::fwrite("data", 1, 4, fp_);
    writeLE32(fp_, 0);  // patched in finalise()
    framesWritten_ = 0;
    return true;
}

bool WavFileSink::write(const float* in, size_t samples) {
    if (!fp_) return false;
    std::vector<int16_t> pcm(samples);
    for (size_t i = 0; i < samples; ++i) {
        const float v = std::clamp(in[i], -1.0f, 1.0f);
        pcm[i] = static_cast<int16_t>(std::lround(v * 32767.0f));
    }
    const size_t n = std::fwrite(pcm.data(), sizeof(int16_t), samples, fp_);
    framesWritten_ += static_cast<uint32_t>(n);
    return n == samples;
}

void WavFileSink::finalise() {
    if (!fp_) return;
    const uint32_t dataBytes = framesWritten_ * 2u;
    std::fseek(fp_, 4, SEEK_SET);
    writeLE32(fp_, 36 + dataBytes);
    std::fseek(fp_, 40, SEEK_SET);
    writeLE32(fp_, dataBytes);
}

void WavFileSink::stop() {
    if (!fp_) return;
    finalise();
    std::fclose(fp_);
    fp_ = nullptr;
}

// --- MemorySink ------------------------------------------------------------

bool MemorySink::write(const float* in, size_t samples) {
    ++writes_;
    if (failing_) return false;
    samples_.insert(samples_.end(), in, in + samples);
    return true;
}

}  // namespace smartmic::host
