# Builds libsmartmic_phone.a for iOS.
#
# Produced ahead of time and vendored into the Flutter app, so opening the
# project in Xcode and pressing Run does not require CMake on the developer's
# machine.
cmake_minimum_required(VERSION 3.22)
project(smartmic_phone_ios LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(SM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(SM_TP "${SM_ROOT}/third_party")

add_library(smartmic_phone STATIC
    ${SM_ROOT}/windows/audio-service/src/crossfade.cpp
    ${SM_ROOT}/windows/audio-service/src/resampler.cpp
    ${SM_ROOT}/windows/audio-service/src/source_state_machine.cpp
    ${SM_ROOT}/windows/audio-service/src/device_registry.cpp
    ${SM_ROOT}/windows/audio-service/src/router.cpp
    ${SM_ROOT}/windows/audio-service/src/logging.cpp
    ${SM_ROOT}/shared/protocol/src/message.cpp
    ${SM_ROOT}/shared/protocol/src/codec.cpp
    ${SM_ROOT}/shared/security/src/identity.cpp
    ${SM_ROOT}/shared/security/src/pairing.cpp
    ${SM_ROOT}/shared/media/src/opus_codec.cpp
    ${SM_ROOT}/shared/media/src/jitter_buffer.cpp
    ${SM_ROOT}/shared/media/src/secure_channel.cpp
    ${SM_ROOT}/shared/media/src/transport.cpp
    ${SM_ROOT}/shared/media/src/chaos_transport.cpp
    ${SM_ROOT}/shared/media/src/udp_transport.cpp
    ${SM_ROOT}/shared/media/src/phone_media_source.cpp
    ${SM_ROOT}/shared/session/src/peer_session.cpp
    ${SM_ROOT}/apps/phone-core/phone_runtime.cpp
    ${SM_ROOT}/apps/phone-core/capture_null.cpp
    ${SM_ROOT}/apps/phone-core/capture_coreaudio.cpp
)

target_include_directories(smartmic_phone PRIVATE
    ${SM_ROOT}/windows/audio-service/include
    ${SM_ROOT}/shared/protocol/include
    ${SM_ROOT}/shared/security/include
    ${SM_ROOT}/shared/media/include
    ${SM_ROOT}/shared/session/include
    ${SM_ROOT}/apps/phone-core
    ${SM_TP}/include
    ${SM_TP}/ios/include
    ${SM_TP}/ios/include/opus
)

# Default visibility: the symbols have to survive into the app binary for
# Dart's DynamicLibrary.process() to find them.
target_compile_options(smartmic_phone PRIVATE -Wall -Wextra -O2 -fvisibility=default)
