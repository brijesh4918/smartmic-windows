#include "smartmic/source_state_machine.h"
#include "test_harness.h"

using namespace smartmic;

namespace {
SourceStateMachine armed() {
    SourceStateMachine sm;
    sm.apply(Event::StartPtt);
    sm.apply(Event::PhoneReady);
    return sm;
}
}  // namespace

SM_TEST(A9, "legal PTT cycle walks the expected states") {
    SourceStateMachine sm;
    CHECK(sm.state() == SourceState::LocalActive);

    CHECK(sm.apply(Event::StartPtt).changed);
    CHECK(sm.state() == SourceState::PhoneArming);

    CHECK(sm.apply(Event::PhoneReady).changed);
    CHECK(sm.state() == SourceState::PhoneActive);

    CHECK(sm.apply(Event::StopPtt).changed);
    CHECK(sm.state() == SourceState::ReturningToLocal);

    CHECK(sm.apply(Event::FadeComplete).changed);
    CHECK(sm.state() == SourceState::LocalActive);
}

SM_TEST(A9b, "illegal edges are rejected with no side effect") {
    SourceStateMachine sm;
    // PhoneReady means nothing when nobody asked for the phone.
    auto t = sm.apply(Event::PhoneReady);
    CHECK(!t.accepted);
    CHECK(sm.state() == SourceState::LocalActive);

    t = sm.apply(Event::FadeComplete);
    CHECK(!t.accepted);
    CHECK(sm.state() == SourceState::LocalActive);

    t = sm.apply(Event::ArmTimeout);
    CHECK(!t.accepted);
    CHECK(sm.state() == SourceState::LocalActive);

    t = sm.apply(Event::Recovered);
    CHECK(!t.accepted);
    CHECK(sm.state() == SourceState::LocalActive);

    t = sm.apply(Event::PhoneLost);
    CHECK(!t.accepted);
    CHECK(sm.state() == SourceState::LocalActive);
}

SM_TEST(A11, "STOP_PTT is idempotent from every state") {
    for (int i = 0; i < 3; ++i) {
        SourceStateMachine sm;
        if (i >= 1) sm.apply(Event::StartPtt);
        if (i >= 2) sm.apply(Event::PhoneReady);
        for (int n = 0; n < 3; ++n) {
            const auto t = sm.apply(Event::StopPtt);
            CHECK(t.accepted);   // never an error, however many times it arrives
        }
        sm.apply(Event::FadeComplete);
        CHECK(sm.state() == SourceState::LocalActive);
    }
}

SM_TEST(A9c, "modes take precedence and PTT is ignored outside Auto") {
    SourceStateMachine sm = armed();
    CHECK(sm.state() == SourceState::PhoneActive);

    sm.apply(Event::ModeLocalOnly);
    CHECK(sm.state() == SourceState::LocalOnly);
    CHECK(sm.apply(Event::StartPtt).accepted);       // accepted, but ignored
    CHECK(sm.state() == SourceState::LocalOnly);

    sm.apply(Event::ModeMute);
    CHECK(sm.state() == SourceState::Muted);
    CHECK(!sm.wantsLocal());
    CHECK(!sm.wantsPhone());

    sm.apply(Event::ModePhoneOnly);
    CHECK(sm.state() == SourceState::PhoneOnly);
    CHECK(sm.wantsPhone());

    sm.apply(Event::ModeAuto);
    CHECK(sm.state() == SourceState::LocalActive);
}

SM_TEST(A9d, "local source is kept warm for the whole PTT cycle") {
    SourceStateMachine sm;
    sm.apply(Event::StartPtt);
    CHECK(sm.wantsLocal());                 // arming
    // The phone is not read during arming: doing so would drain the jitter
    // buffer as fast as it fills and the prefill would never complete.
    CHECK(!sm.wantsPhone());
    sm.apply(Event::PhoneReady);
    CHECK(sm.wantsLocal());                 // active -- never stopped (ADR-008)
    CHECK(sm.wantsPhone());
    sm.apply(Event::StopPtt);
    CHECK(sm.wantsLocal());                 // returning
}

SM_TEST(A9e, "error recovery returns to the resting state of the current mode") {
    SourceStateMachine sm;
    sm.apply(Event::ModePhoneOnly);
    sm.apply(Event::SourceFailure);
    CHECK(sm.state() == SourceState::ErrorRecovery);
    CHECK(sm.apply(Event::Recovered).accepted);
    CHECK(sm.state() == SourceState::PhoneOnly);
}
