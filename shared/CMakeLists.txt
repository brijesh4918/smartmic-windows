# SmartMic.Protocol -- one wire format, shared by the service and (via bindings)
# by both mobile platforms.
add_library(smartmic_protocol
    protocol/src/message.cpp
    protocol/src/codec.cpp
)
target_include_directories(smartmic_protocol PUBLIC protocol/include ${NLOHMANN_JSON_INCLUDE_DIR})
target_link_libraries(smartmic_protocol PUBLIC PkgConfig::SODIUM)

# SmartMic.Security -- device identity, pairing, session keys.
add_library(smartmic_security
    security/src/identity.cpp
    security/src/pairing.cpp
)
target_include_directories(smartmic_security PUBLIC security/include)
target_link_libraries(smartmic_security PUBLIC PkgConfig::SODIUM)

# SmartMic media: codec, jitter buffer, AEAD framing, transports.
add_library(smartmic_media
    media/src/opus_codec.cpp
    media/src/jitter_buffer.cpp
    media/src/secure_channel.cpp
    media/src/transport.cpp
    media/src/chaos_transport.cpp
    media/src/udp_transport.cpp
    media/src/phone_media_source.cpp
)
target_include_directories(smartmic_media PUBLIC media/include)
target_link_libraries(smartmic_media
    PUBLIC smartmic_core smartmic_protocol smartmic_security PkgConfig::OPUS PkgConfig::SODIUM)
if(WIN32)
    target_link_libraries(smartmic_media PUBLIC ws2_32)
endif()

# Session: datagram framing, pairing handshake, authenticated channel.
add_library(smartmic_session session/src/peer_session.cpp)
target_include_directories(smartmic_session PUBLIC session/include)
target_link_libraries(smartmic_session PUBLIC smartmic_media smartmic_protocol smartmic_security)
