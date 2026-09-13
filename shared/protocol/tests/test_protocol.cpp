#include "smartmic/protocol/codec.h"
#include "smartmic/protocol/replay_window.h"
#include "test_harness.h"

using namespace smartmic::protocol;

SM_TEST(P1, "control messages round-trip through the codec") {
    Message m = Message::make(MsgType::SetMode, "sess-1", "dev-abc", 42, {{"mode", "PhoneOnly"}});
    const std::string wire = encode(m);

    const auto r = decode(wire);
    CHECK(bool(r));
    CHECK_EQ(r.message.protocolVersion, kProtocolVersion);
    CHECK(r.message.sessionId == std::string("sess-1"));
    CHECK(r.message.deviceId == std::string("dev-abc"));
    CHECK_EQ(r.message.seq, 42u);
    CHECK(r.message.type == MsgType::SetMode);
    CHECK(r.message.payload.at("mode").get<std::string>() == std::string("PhoneOnly"));
    CHECK(r.message.messageId.size() == 36);
}

SM_TEST(P2, "every message type survives the round trip") {
    const MsgType all[] = {MsgType::Hello, MsgType::Auth, MsgType::Capabilities, MsgType::Ping,
                           MsgType::Pong, MsgType::StartPtt, MsgType::PttPreparing,
                           MsgType::PttReady, MsgType::StopPtt, MsgType::PttStopped,
                           MsgType::SetMode, MsgType::SetLocalSource, MsgType::GetStatus,
                           MsgType::Status, MsgType::Error};
    for (MsgType t : all) {
        const auto r = decode(encode(Message::make(t, "s", "d", 1)));
        CHECK(bool(r));
        CHECK(r.message.type == t);
    }
}

SM_TEST(P3, "malformed input is rejected, never thrown from") {
    CHECK(decode("").error == DecodeError::NotJson);
    CHECK(decode("{").error == DecodeError::NotJson);
    CHECK(decode("[1,2,3]").error == DecodeError::NotAnObject);
    CHECK(decode("\"hello\"").error == DecodeError::NotAnObject);
    CHECK(decode("{}").error == DecodeError::MissingField);

    // Right shape, wrong types.
    CHECK(decode(R"({"protocolVersion":1,"sessionId":5,"messageId":"m","seq":1,)"
                 R"("timestamp":1,"deviceId":"d","type":"PING"})").error == DecodeError::BadFieldType);

    // Unknown type is not silently ignored.
    CHECK(decode(R"({"protocolVersion":1,"sessionId":"s","messageId":"m","seq":1,)"
                 R"("timestamp":1,"deviceId":"d","type":"LAUNCH_MISSILES"})").error ==
          DecodeError::UnknownType);

    // Payload must be an object if present.
    CHECK(decode(R"({"protocolVersion":1,"sessionId":"s","messageId":"m","seq":1,)"
                 R"("timestamp":1,"deviceId":"d","type":"PING","payload":7})").error ==
          DecodeError::BadFieldType);

    const std::string huge(kMaxMessageBytes + 1, 'x');
    CHECK(decode(huge).error == DecodeError::TooLarge);
}

SM_TEST(P4, "a version outside the supported range fails closed, never downgrades") {
    // Threat model T15: an attacker must not be able to talk us down to v0.
    CHECK(decode(R"({"protocolVersion":0,"sessionId":"s","messageId":"m","seq":1,)"
                 R"("timestamp":1,"deviceId":"d","type":"PING"})").error ==
          DecodeError::UnsupportedVersion);
    CHECK(decode(R"({"protocolVersion":99,"sessionId":"s","messageId":"m","seq":1,)"
                 R"("timestamp":1,"deviceId":"d","type":"PING"})").error ==
          DecodeError::UnsupportedVersion);
}

SM_TEST(P5, "the replay window accepts fresh messages exactly once") {
    ReplayWindow<64> w;
    CHECK(w.accept(10));
    CHECK(!w.accept(10));        // exact replay
    CHECK(w.accept(11));
    CHECK(w.accept(9));          // reorder within the window is legitimate
    CHECK(!w.accept(9));         // ...but only once
    CHECK(w.accept(50));
    CHECK(!w.accept(50));
}

SM_TEST(P6, "messages older than the window are refused rather than guessed at") {
    ReplayWindow<64> w;
    CHECK(w.accept(1000));
    CHECK(w.accept(1000 - 63));
    CHECK(!w.accept(1000 - 64));   // cannot prove it is not a replay -> refuse
    CHECK(!w.accept(1));
}

SM_TEST(P7, "a large forward jump resets the window without losing safety") {
    ReplayWindow<64> w;
    CHECK(w.accept(5));
    CHECK(w.accept(100000));
    CHECK(!w.accept(100000));
    CHECK(!w.accept(5));           // far behind the new high-water mark
    CHECK(w.accept(99999));
}

SM_TEST(P8, "a captured START_PTT cannot be replayed to reopen the microphone") {
    // Threat model T5, end to end at the protocol layer.
    ReplayWindow<256> w;
    const Message start = Message::make(MsgType::StartPtt, "sess", "phone", 7);
    const std::string captured = encode(start);

    const auto first = decode(captured);
    CHECK(bool(first));
    CHECK(w.accept(first.message.seq));

    for (int i = 0; i < 100; ++i) {
        const auto replayed = decode(captured);
        CHECK(bool(replayed));                    // it decodes...
        CHECK(!w.accept(replayed.message.seq));   // ...and is refused every time
    }
}
