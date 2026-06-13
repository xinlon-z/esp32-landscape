cmake_minimum_required(VERSION 3.16)
project(esp32_clock_tests C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

set(MAIN "${CMAKE_CURRENT_SOURCE_DIR}/../main")
set(TEST_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${MAIN}")

function(clock_test name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name} PRIVATE ${TEST_INCLUDES})
    target_link_libraries(${name} GTest::gtest_main)
    gtest_discover_tests(${name})
endfunction()

clock_test(test_event_queue)
clock_test(test_event_bus)
clock_test(test_mqtt_service_music_parse)
clock_test(test_mqtt_service_cover_pump)
clock_test(test_music_player_icon_geometry)
clock_test(test_music_time_format)
clock_test(test_music_background)
clock_test(test_background_blur_service)
clock_test(test_music_visualizer)
clock_test(test_music_model)
clock_test(test_clock_model)
clock_test(test_clock_presenter)
clock_test(test_clock_view_palette)
clock_test(test_clock_background_service)
clock_test(test_power_mgr_voltage)
clock_test(test_power_mgr_idle_mode)
clock_test(test_power_service)
clock_test(test_time_service)
clock_test(test_screen_nav)
clock_test(test_backlight_policy)
clock_test(test_lvgl_task_config)
clock_test(test_lvgl_rotation)
clock_test(test_cover_scale)

# Separate compilation: music_presenter.cpp and music_model.cpp both define
# kFramesPerSecond in anonymous namespaces, which collide in a single TU.
add_executable(test_music_presenter_eventbus
    test_music_presenter_eventbus.cpp
    ${MAIN}/app/features/music/music_presenter.cpp
    ${MAIN}/app/features/music/music_model.cpp
    ${MAIN}/app/core/event/event_bus.cpp
    ${MAIN}/app/services/mqtt_service.cpp
    ${MAIN}/app/services/cover_service.cpp
    ${MAIN}/app/services/power_service.cpp)
target_include_directories(test_music_presenter_eventbus PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_music_presenter_eventbus GTest::gtest_main)
gtest_discover_tests(test_music_presenter_eventbus)

# Separate compilation: music_mqtt.cpp must remain inlined because the test
# calls its anonymous-namespace internals updateState/updateCover directly.
add_executable(test_music_mqtt_ingest_service_event
    test_music_mqtt_ingest_service_event.cpp
    ${MAIN}/app/services/mqtt_service.cpp
    ${MAIN}/app/services/cover_service.cpp
    ${MAIN}/app/core/event/event_bus.cpp)
target_include_directories(test_music_mqtt_ingest_service_event PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_music_mqtt_ingest_service_event GTest::gtest_main)
gtest_discover_tests(test_music_mqtt_ingest_service_event)
