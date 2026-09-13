add_executable(smartmic-service service/main.cpp)
target_link_libraries(smartmic-service PRIVATE smartmic_core smartmic_platform_host
                                               smartmic_session smartmic_media)
if(WIN32)
    target_link_libraries(smartmic-service PRIVATE smartmic_platform_windows)
endif()
if(APPLE)
    target_link_libraries(smartmic-service PRIVATE smartmic_platform_mac)
endif()

add_executable(smartmic-phone phone-sim/main.cpp)
target_link_libraries(smartmic-phone PRIVATE smartmic_core smartmic_session smartmic_media)
