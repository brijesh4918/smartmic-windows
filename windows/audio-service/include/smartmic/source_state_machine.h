// The authoritative PTT / routing state machine.
// See docs/adr/ADR-007-ptt-state-machine.md
#pragma once

#include <string_view>

namespace smartmic {

enum class SourceState {
    LocalActive,
    PhoneArming,
    PhoneActive,
    ReturningToLocal,
    PhoneOnly,
    LocalOnly,
    Muted,
    ErrorRecovery,
};

// User-selected routing mode. Modes choose which region of the state graph the
// router lives in; PTT only has an effect in Auto.
enum class RoutingMode {
    Auto,
    PhoneOnly,
    LocalOnly,
    Mute,
};

enum class Event {
    StartPtt,
    StopPtt,
    PhoneReady,     // decodable phone audio actually present
    PhoneLost,      // heartbeat/media watchdog fired
    ArmTimeout,     // PHONE_ARMING took too long
    FadeComplete,
    SourceFailure,  // the active source reported Failed
    Recovered,      // error recovery finished
    ModeAuto,
    ModePhoneOnly,
    ModeLocalOnly,
    ModeMute,
};

struct Transition {
    bool accepted;        // false => illegal edge, state unchanged, no side effect
    SourceState from;
    SourceState to;
    bool changed;         // accepted and from != to
};

class SourceStateMachine {
public:
    SourceState state() const { return state_; }
    RoutingMode mode() const { return mode_; }

    Transition apply(Event e);

    // True when the phone source should be contributing or preparing to.
    bool wantsPhone() const;
    // True when the local source should be captured (it is kept warm during
    // PTT rather than stopped -- ADR-008).
    bool wantsLocal() const;

private:
    SourceState state_ = SourceState::LocalActive;
    RoutingMode mode_  = RoutingMode::Auto;
};

std::string_view toString(SourceState s);
std::string_view toString(RoutingMode m);
std::string_view toString(Event e);

}  // namespace smartmic
