#include <cmath>

#include "smartmic/audio_format.h"
#include "smartmic/crossfade.h"
#include "test_harness.h"

using namespace smartmic;

SM_TEST(A5, "crossfade is equal power across the whole curve") {
    Crossfade cf;
    cf.fadeTo(1.0f, 20);
    const size_t n = millisToSamples(20);
    for (size_t i = 0; i < n; ++i) {
        const GainPair g = cf.next();
        const float energy = g.local * g.local + g.phone * g.phone;
        CHECK_NEAR(energy, 1.0, 0.01);   // within 1 %
    }
    CHECK_NEAR(cf.position(), 1.0, 1e-6);
}

SM_TEST(A7, "fade completes in exactly the requested number of samples") {
    for (uint32_t ms : {10u, 15u, 20u, 30u}) {
        Crossfade cf;
        cf.fadeTo(1.0f, ms);
        const size_t expected = millisToSamples(ms);
        CHECK_EQ(cf.remaining(), expected);
        for (size_t i = 0; i < expected - 1; ++i) {
            cf.next();
            CHECK(cf.active());
        }
        cf.next();
        CHECK(!cf.active());
        CHECK_NEAR(cf.position(), 1.0, 1e-6);
    }
}

SM_TEST(A7b, "retarget mid-fade keeps the same slope and never jumps") {
    Crossfade cf;
    cf.fadeTo(1.0f, 20);
    const size_t half = millisToSamples(20) / 2;
    for (size_t i = 0; i < half; ++i) cf.next();
    const float mid = cf.position();
    CHECK(mid > 0.4f && mid < 0.6f);

    cf.fadeTo(0.0f, 20);                       // release during the press fade
    const float afterRetarget = cf.position();
    CHECK_NEAR(afterRetarget, mid, 1e-6);      // no discontinuity at the retarget
    while (cf.active()) cf.next();
    CHECK_NEAR(cf.position(), 0.0, 1e-6);
}
