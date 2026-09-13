# SmartMic.AudioService
#
# smartmic_core is deliberately platform-neutral: it builds on clang, gcc and
# MSVC with no OS audio dependency, so the router, state machine, ring buffer,
# crossfade and resampler are testable on any developer machine and in CI.
# Only smartmic_platform_windows needs MSVC and the Windows SDK.

add_library(smartmic_core
    src/crossfade.cpp
    src/resampler.cpp
    src/source_state_machine.cpp
    src/device_registry.cpp
    src/router.cpp
    src/logging.cpp
)
target_include_directories(smartmic_core PUBLIC include)
find_package(Threads REQUIRED)
target_link_libraries(smartmic_core PUBLIC Threads::Threads)

add_library(smartmic_platform_host
    platform/host/host_sources.cpp
)
target_include_directories(smartmic_platform_host PUBLIC platform/host)
target_link_libraries(smartmic_platform_host PUBLIC smartmic_core)

# On Apple hosts the service can use the machine's real microphone as the
# stand-in for "the PC microphone", which makes the macOS end-to-end test
# representative rather than a tone against a tone.
#
# Kept in its OWN library, linked only by the service: pulling AudioToolbox and
# CoreAudio into the test binary made it hang in dyld before main, and tests
# have no business linking audio frameworks in the first place.
if(APPLE)
    add_library(smartmic_platform_mac
        platform/host/live_mic_source.cpp
        ${CMAKE_SOURCE_DIR}/apps/phone-core/capture_coreaudio.cpp
        ${CMAKE_SOURCE_DIR}/apps/phone-core/capture_null.cpp
    )
    target_include_directories(smartmic_platform_mac
        PUBLIC platform/host ${CMAKE_SOURCE_DIR}/apps/phone-core)
    target_link_libraries(smartmic_platform_mac PUBLIC smartmic_core
        "-framework AudioToolbox" "-framework CoreAudio" "-framework CoreFoundation")
    target_compile_definitions(smartmic_platform_mac PUBLIC SMARTMIC_HAVE_LIVE_MIC=1)
endif()

if(WIN32)
    add_library(smartmic_platform_windows
        platform/windows/wasapi_common.cpp
        platform/windows/wasapi_device_enumerator.cpp
        platform/windows/wasapi_capture_source.cpp
        platform/windows/wasapi_render_sink.cpp
        platform/windows/smartmic_driver_link.cpp
        platform/windows/driver_diagnostics.cpp
    )
    target_include_directories(smartmic_platform_windows PUBLIC platform/windows)
    target_link_libraries(smartmic_platform_windows PUBLIC smartmic_core ole32 oleaut32 uuid winmm avrt setupapi)
    target_compile_definitions(smartmic_platform_windows PUBLIC SMARTMIC_HAVE_WASAPI=1)
endif()

add_executable(smartmic-router app/main.cpp)
target_link_libraries(smartmic-router PRIVATE smartmic_core smartmic_platform_host)
if(WIN32)
    target_link_libraries(smartmic-router PRIVATE smartmic_platform_windows)
endif()

add_executable(smartmic_tests
    tests/test_harness.cpp
    tests/test_ring_buffer.cpp
    tests/test_crossfade.cpp
    tests/test_resampler.cpp
    tests/test_state_machine.cpp
    tests/test_device_registry.cpp
    tests/test_router.cpp
    ${CMAKE_SOURCE_DIR}/shared/protocol/tests/test_protocol.cpp
    ${CMAKE_SOURCE_DIR}/shared/security/tests/test_security.cpp
    ${CMAKE_SOURCE_DIR}/shared/media/tests/test_media.cpp
    ${CMAKE_SOURCE_DIR}/tests/integration/test_end_to_end.cpp
)
target_link_libraries(smartmic_tests PRIVATE smartmic_core smartmic_platform_host smartmic_protocol smartmic_security smartmic_media)
target_include_directories(smartmic_tests PRIVATE tests)
add_test(NAME phase1_acceptance COMMAND smartmic_tests)
