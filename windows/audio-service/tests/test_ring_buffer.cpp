#include <atomic>
#include <thread>
#include <vector>

#include "smartmic/ring_buffer.h"
#include "test_harness.h"

using namespace smartmic;

SM_TEST(A1, "ring buffer preserves FIFO order") {
    RingBuffer<int> rb(16);
    std::vector<int> in{1, 2, 3, 4, 5};
    CHECK_EQ(rb.write(in.data(), in.size()), 0u);
    CHECK_EQ(rb.available(), 5u);
    std::vector<int> out(5, 0);
    CHECK_EQ(rb.read(out.data(), 5), 5u);
    for (int i = 0; i < 5; ++i) CHECK_EQ(out[static_cast<size_t>(i)], i + 1);
    CHECK_EQ(rb.available(), 0u);
}

SM_TEST(A2, "overrun drops the oldest and counts exactly") {
    RingBuffer<int> rb(8);  // exact power of two
    std::vector<int> in(8);
    for (int i = 0; i < 8; ++i) in[static_cast<size_t>(i)] = i;
    CHECK_EQ(rb.write(in.data(), 8), 0u);

    std::vector<int> more{100, 101, 102};
    CHECK_EQ(rb.write(more.data(), 3), 3u);      // three oldest dropped
    CHECK_EQ(rb.overrunCount(), 3u);
    CHECK_EQ(rb.available(), 8u);

    std::vector<int> out(8, -1);
    CHECK_EQ(rb.read(out.data(), 8), 8u);
    CHECK_EQ(out[0], 3);                          // 0,1,2 gone
    CHECK_EQ(out[5], 100);
    CHECK_EQ(out[7], 102);
}

SM_TEST(A3, "underrun returns a short read and never blocks") {
    RingBuffer<float> rb(64);
    std::vector<float> in(10, 0.5f);
    rb.write(in.data(), in.size());

    std::vector<float> out(32, -1.0f);
    CHECK_EQ(rb.read(out.data(), 32), 10u);
    CHECK_EQ(rb.underrunCount(), 22u);

    std::vector<float> silenced(32, -1.0f);
    rb.write(in.data(), 4);
    CHECK_EQ(rb.readOrSilence(silenced.data(), 32), 4u);
    CHECK_NEAR(silenced[3], 0.5f, 1e-6);
    CHECK_NEAR(silenced[4], 0.0f, 1e-6);   // shortfall is silence, not stale data
    CHECK_NEAR(silenced[31], 0.0f, 1e-6);
}

SM_TEST(A4, "threaded producer/consumer soak loses nothing when drained") {
    constexpr uint32_t kTotal = 1u << 20;
    RingBuffer<uint32_t> rb(4096);
    std::atomic<bool> producerDone{false};
    std::atomic<uint64_t> mismatches{0};
    std::atomic<uint64_t> received{0};

    std::thread producer([&] {
        uint32_t v = 0;
        while (v < kTotal) {
            const uint32_t batch = 97;
            uint32_t buf[97];
            uint32_t n = 0;
            while (n < batch && v < kTotal) buf[n++] = v++;
            // Respect the ring so this is a lossless test of ordering, not of
            // the overrun path (A2 covers that).
            while (rb.freeSpace() < n) std::this_thread::yield();
            rb.write(buf, n);
        }
        producerDone.store(true);
    });

    std::thread consumer([&] {
        uint32_t expect = 0;
        uint32_t buf[256];
        while (expect < kTotal) {
            const size_t n = rb.read(buf, 256);
            if (n == 0) {
                if (producerDone.load() && rb.available() == 0) break;
                std::this_thread::yield();
                continue;
            }
            for (size_t i = 0; i < n; ++i) {
                if (buf[i] != expect) mismatches.fetch_add(1);
                ++expect;
            }
            received.fetch_add(n);
        }
    });

    producer.join();
    consumer.join();
    CHECK_EQ(mismatches.load(), 0u);
    CHECK_EQ(received.load(), static_cast<uint64_t>(kTotal));
}
