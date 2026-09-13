#include "smartmic/source_state_machine.h"

namespace smartmic {
namespace {

SourceState restingStateFor(RoutingMode m) {
    switch (m) {
        case RoutingMode::Auto:      return SourceState::LocalActive;
        case RoutingMode::PhoneOnly: return SourceState::PhoneOnly;
        case RoutingMode::LocalOnly: return SourceState::LocalOnly;
        case RoutingMode::Mute:      return SourceState::Muted;
    }
    return SourceState::LocalActive;
}

}  // namespace

Transition SourceStateMachine::apply(Event e) {
    const SourceState from = state_;
    SourceState to = from;
    bool accepted = true;

    switch (e) {
        // --- mode changes are always legal and always win ---
        case Event::ModeAuto:      mode_ = RoutingMode::Auto;      to = SourceState::LocalActive; break;
        case Event::ModePhoneOnly: mode_ = RoutingMode::PhoneOnly; to = SourceState::PhoneOnly;   break;
        case Event::ModeLocalOnly: mode_ = RoutingMode::LocalOnly; to = SourceState::LocalOnly;   break;
        case Event::ModeMute:      mode_ = RoutingMode::Mute;      to = SourceState::Muted;       break;

        // --- PTT: only meaningful in Auto. In every other mode it is ignored
        //     rather than rejected: the phone holding the button while the PC
        //     is in Local Only is a legitimate, harmless situation. ---
        case Event::StartPtt:
            if (mode_ == RoutingMode::Auto) {
                if (from == SourceState::LocalActive || from == SourceState::ReturningToLocal) {
                    to = SourceState::PhoneArming;
                }
                // PhoneArming / PhoneActive: already going there, idempotent.
            }
            break;

        case Event::StopPtt:
            if (mode_ == RoutingMode::Auto) {
                if (from == SourceState::PhoneActive) {
                    to = SourceState::ReturningToLocal;
                } else if (from == SourceState::PhoneArming) {
                    // No phone audio has been mixed yet, so no fade is needed.
                    to = SourceState::LocalActive;
                }
                // LocalActive / ReturningToLocal: idempotent no-op.
            }
            break;

        case Event::PhoneReady:
            if (from == SourceState::PhoneArming)      to = SourceState::PhoneActive;
            else if (from == SourceState::PhoneActive) to = from;   // duplicate ack
            else if (from == SourceState::PhoneOnly)   to = from;
            else accepted = false;
            break;

        case Event::PhoneLost:
            if (from == SourceState::PhoneActive)       to = SourceState::ReturningToLocal;
            else if (from == SourceState::PhoneArming)  to = SourceState::ErrorRecovery;
            else if (from == SourceState::PhoneOnly)    to = from;  // stays; router emits silence
            else accepted = false;
            break;

        case Event::ArmTimeout:
            if (from == SourceState::PhoneArming) to = SourceState::ErrorRecovery;
            else accepted = false;
            break;

        case Event::FadeComplete:
            if (from == SourceState::ReturningToLocal) to = SourceState::LocalActive;
            else accepted = false;
            break;

        case Event::SourceFailure:
            to = SourceState::ErrorRecovery;
            break;

        case Event::Recovered:
            if (from == SourceState::ErrorRecovery) to = restingStateFor(mode_);
            else accepted = false;
            break;
    }

    if (accepted) state_ = to;
    return Transition{accepted, from, accepted ? to : from, accepted && from != to};
}

bool SourceStateMachine::wantsPhone() const {
    switch (state_) {
        // Deliberately NOT PhoneArming. During arming the phone's contribution
        // is gain-zero anyway, and draining the jitter buffer one frame per
        // tick would keep it at zero depth forever -- so the prefill could
        // never complete and PTT could never arm. Arming means "let the buffer
        // fill", not "play what has arrived so far".
        case SourceState::PhoneActive:
        case SourceState::ReturningToLocal:
        case SourceState::PhoneOnly:
            return true;
        default:
            return false;
    }
}

bool SourceStateMachine::wantsLocal() const {
    switch (state_) {
        case SourceState::LocalActive:
        case SourceState::LocalOnly:
        case SourceState::PhoneArming:
        case SourceState::PhoneActive:       // kept warm -- ADR-008
        case SourceState::ReturningToLocal:
            return true;
        default:
            return false;
    }
}

std::string_view toString(SourceState s) {
    switch (s) {
        case SourceState::LocalActive:      return "LOCAL_ACTIVE";
        case SourceState::PhoneArming:      return "PHONE_ARMING";
        case SourceState::PhoneActive:      return "PHONE_ACTIVE";
        case SourceState::ReturningToLocal: return "RETURNING_TO_LOCAL";
        case SourceState::PhoneOnly:        return "PHONE_ONLY";
        case SourceState::LocalOnly:        return "LOCAL_ONLY";
        case SourceState::Muted:            return "MUTED";
        case SourceState::ErrorRecovery:    return "ERROR_RECOVERY";
    }
    return "?";
}

std::string_view toString(RoutingMode m) {
    switch (m) {
        case RoutingMode::Auto:      return "Auto";
        case RoutingMode::PhoneOnly: return "PhoneOnly";
        case RoutingMode::LocalOnly: return "LocalOnly";
        case RoutingMode::Mute:      return "Mute";
    }
    return "?";
}

std::string_view toString(Event e) {
    switch (e) {
        case Event::StartPtt:      return "START_PTT";
        case Event::StopPtt:       return "STOP_PTT";
        case Event::PhoneReady:    return "PHONE_READY";
        case Event::PhoneLost:     return "PHONE_LOST";
        case Event::ArmTimeout:    return "ARM_TIMEOUT";
        case Event::FadeComplete:  return "FADE_COMPLETE";
        case Event::SourceFailure: return "SOURCE_FAILURE";
        case Event::Recovered:     return "RECOVERED";
        case Event::ModeAuto:      return "MODE_AUTO";
        case Event::ModePhoneOnly: return "MODE_PHONE_ONLY";
        case Event::ModeLocalOnly: return "MODE_LOCAL_ONLY";
        case Event::ModeMute:      return "MODE_MUTE";
    }
    return "?";
}

}  // namespace smartmic
