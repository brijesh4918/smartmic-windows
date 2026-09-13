// Bounded single-producer / single-consumer ring.
//
// This is the portable twin of the shared ring described in
// docs/architecture/05-driver-interface.md: same monotonic-index scheme, same
// overrun policy, same "never block, never spin" contract. Keeping the two
// identical in behaviour means the Phase 1 tests exercise the semantics the
// Phase 5 driver will rely on.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

namespace smartmic {

template <typename T>
class RingBuffer {
public:
    // capacity is rounded up to a power of two so wrapping is a mask.
    explicit RingBuffer(size_t capacity) : capacity_(roundUpPow2(capacity)), mask_(capacity_ - 1), data_(capacity_) {}

    size_t capacity() const { return capacity_; }

    size_t available() const {
        const uint64_t w = write_.load(std::memory_order_acquire);
        const uint64_t r = read_.load(std::memory_order_acquire);
        return static_cast<size_t>(w - r);
    }

    size_t freeSpace() const { return capacity_ - available(); }

    uint64_t overrunCount() const { return overruns_.load(std::memory_order_relaxed); }
    uint64_t underrunCount() const { return underruns_.load(std::memory_order_relaxed); }

    // Producer side. Writes all `count` items. If the ring is too full, the
    // oldest items are dropped (the reader's index is advanced) so the data in
    // the ring always stays the most recent -- late audio is worse than lost
    // audio. Returns the number of items dropped.
    size_t write(const T* src, size_t count) {
        if (count == 0) return 0;
        size_t dropped = 0;
        if (count > capacity_) {  // pathological: keep only the tail
            dropped = count - capacity_;
            src += dropped;
            count = capacity_;
        }
        const uint64_t w = write_.load(std::memory_order_relaxed);
        const uint64_t r = read_.load(std::memory_order_acquire);
        const size_t used = static_cast<size_t>(w - r);
        if (used + count > capacity_) {
            const size_t overflow = used + count - capacity_;
            read_.store(r + overflow, std::memory_order_release);
            dropped += overflow;
        }
        copyIn(w, src, count);
        write_.store(w + count, std::memory_order_release);
        if (dropped) overruns_.fetch_add(dropped, std::memory_order_relaxed);
        return dropped;
    }

    // Consumer side. Reads up to `count` items; returns how many were actually
    // read. Never blocks. A short read increments the underrun counter -- the
    // caller is responsible for filling the shortfall with silence.
    size_t read(T* dst, size_t count) {
        const uint64_t r = read_.load(std::memory_order_relaxed);
        const uint64_t w = write_.load(std::memory_order_acquire);
        const size_t avail = static_cast<size_t>(w - r);
        const size_t n = avail < count ? avail : count;
        copyOut(r, dst, n);
        read_.store(r + n, std::memory_order_release);
        if (n < count) underruns_.fetch_add(count - n, std::memory_order_relaxed);
        return n;
    }

    // Convenience for audio: always produces `count` items, zero-filling any
    // shortfall. Returns the number of real items read.
    size_t readOrSilence(T* dst, size_t count) {
        const size_t n = read(dst, count);
        if (n < count) std::memset(dst + n, 0, (count - n) * sizeof(T));
        return n;
    }

    void clear() {
        read_.store(write_.load(std::memory_order_acquire), std::memory_order_release);
    }

private:
    static size_t roundUpPow2(size_t v) {
        size_t p = 1;
        while (p < v) p <<= 1;
        return p;
    }

    void copyIn(uint64_t at, const T* src, size_t count) {
        const size_t off = static_cast<size_t>(at & mask_);
        const size_t first = (count < capacity_ - off) ? count : capacity_ - off;
        std::memcpy(data_.data() + off, src, first * sizeof(T));
        if (count > first) std::memcpy(data_.data(), src + first, (count - first) * sizeof(T));
    }

    void copyOut(uint64_t at, T* dst, size_t count) {
        const size_t off = static_cast<size_t>(at & mask_);
        const size_t first = (count < capacity_ - off) ? count : capacity_ - off;
        std::memcpy(dst, data_.data() + off, first * sizeof(T));
        if (count > first) std::memcpy(dst + first, data_.data(), (count - first) * sizeof(T));
    }

    const size_t capacity_;
    const size_t mask_;
    std::vector<T> data_;
    std::atomic<uint64_t> write_{0};
    std::atomic<uint64_t> read_{0};
    std::atomic<uint64_t> overruns_{0};
    std::atomic<uint64_t> underruns_{0};
};

}  // namespace smartmic
