// Sliding replay/reorder window over the per-session `seq` counter.
//
// The control channel runs over an unordered, unreliable transport (ADR-011),
// so reordering is normal and must not be treated as an attack -- but a message
// that has already been accepted must never be accepted twice, or a captured
// START_PTT could reopen the microphone later (threat model T5).
#pragma once

#include <bitset>
#include <cstdint>

namespace smartmic::protocol {

template <size_t Width = 256>
class ReplayWindow {
public:
    // Returns true if `seq` is fresh and should be processed. Idempotent for a
    // given seq: the second call returns false.
    bool accept(uint64_t seq) {
        if (!initialised_) {
            initialised_ = true;
            highest_ = seq;
            seen_.reset();
            seen_.set(0);
            return true;
        }
        if (seq > highest_) {
            const uint64_t shift = seq - highest_;
            if (shift >= Width) {
                seen_.reset();
            } else {
                seen_ <<= static_cast<size_t>(shift);
            }
            highest_ = seq;
            seen_.set(0);
            return true;
        }
        const uint64_t behind = highest_ - seq;
        if (behind >= Width) return false;   // too old to prove it is not a replay
        const auto bit = static_cast<size_t>(behind);
        if (seen_.test(bit)) return false;   // already seen
        seen_.set(bit);
        return true;
    }

    uint64_t highest() const { return highest_; }
    bool initialised() const { return initialised_; }
    void reset() { initialised_ = false; highest_ = 0; seen_.reset(); }

private:
    bool initialised_ = false;
    uint64_t highest_ = 0;
    std::bitset<Width> seen_;   // bit i == (highest_ - i) has been accepted
};

}  // namespace smartmic::protocol
