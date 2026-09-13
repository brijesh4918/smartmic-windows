# Builds libsmartmic_phone.so -- the entire phone runtime.
#
# This is the same C++ the desktop service and the 72-test suite use. The
# Flutter layer above it only draws; everything that can fail in a
# platform-specific way lives here (ADR-009).
cmake_minimum_required(VERSION 3.22)
project(smartmic_phone LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# cpp -> main -> src -> app -> android -> mobile -> apps -> repository root
get_filename_component(SM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../.." ABSOLUTE)
set(SM_TP "${SM_ROOT}/third_party")

add_library(sodium SHARED IMPORTED)
set_target_properties(sodium PROPERTIES
    IMPORTED_LOCATION "${SM_TP}/android/${ANDROID_ABI}/lib/libsodium.so")

add_library(opus STATIC IMPORTED)
set_target_properties(opus PROPERTIES
    IMPORTED_LOCATION "${SM_TP}/android/${ANDROID_ABI}/lib/libopus.a")

add_library(smartmic_phone SHARED
    # portable core
    ${SM_ROOT}/windows/audio-service/src/crossfade.cpp
    ${SM_ROOT}/windows/audio-service/src/resampler.cpp
    ${SM_ROOT}/windows/audio-service/src/source_state_machine.cpp
    ${SM_ROOT}/windows/audio-service/src/device_registry.cpp
    ${SM_ROOT}/windows/audio-service/src/router.cpp
    ${SM_ROOT}/windows/audio-service/src/logging.cpp
    # protocol / security / media / session
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
    # phone runtime
    ${SM_ROOT}/apps/phone-core/phone_runtime.cpp
    ${SM_ROOT}/apps/phone-core/capture_null.cpp
    ${SM_ROOT}/apps/phone-core/capture_aaudio.cpp
)

target_include_directories(smartmic_phone PRIVATE
    ${SM_ROOT}/windows/audio-service/include
    ${SM_ROOT}/shared/protocol/include
    ${SM_ROOT}/shared/security/include
    ${SM_ROOT}/shared/media/include
    ${SM_ROOT}/shared/session/include
    ${SM_ROOT}/apps/phone-core
    ${SM_TP}/include
    ${SM_TP}/android/include
    ${SM_TP}/android/include/opus   # opus headers are included as <opus.h>
)

target_compile_options(smartmic_phone PRIVATE -Wall -Wextra -fvisibility=hidden -O2)

find_library(log-lib log)
target_link_libraries(smartmic_phone PRIVATE sodium opus aaudio ${log-lib})
