add_subdirectory(${CMAKE_SOURCE_DIR}/extern/catch2 ${CMAKE_BINARY_DIR}/extern/catch2)

add_executable(coredeck_tests
        test_utilities.cpp
        test_avd.cpp
        test_avd_list_format.cpp
        test_paths.cpp
        test_app_settings.cpp
        test_log_buffer.cpp
        test_log_filter.cpp
        test_options.cpp
        test_skin.cpp
        test_sdk_bootstrap.cpp
        test_sdk_packages.cpp
        test_system_image.cpp
        test_jdk_download.cpp
        test_adb.cpp
        test_device_file.cpp
        test_shared_folder.cpp
        test_version_check.cpp
)

target_link_libraries(coredeck_tests PRIVATE
        coredeck_core
        Catch2::Catch2WithMain
)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/extern/catch2/extras)
include(Catch)
catch_discover_tests(coredeck_tests)
