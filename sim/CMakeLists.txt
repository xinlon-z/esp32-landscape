cmake_minimum_required(VERSION 3.16)
project(music_ui_sim C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(LV_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(LV_LVGL_H_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)

add_subdirectory("${PROJECT_ROOT}/managed_components/lvgl__lvgl" "${CMAKE_BINARY_DIR}/lvgl")
set_target_properties(lvgl_examples lvgl_demos PROPERTIES EXCLUDE_FROM_ALL TRUE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

add_executable(music_ui_sim
  main.cpp
  sim_assets.cpp
  sim_music_mqtt.cpp
  "${PROJECT_ROOT}/main/app/features/music/util/music_background.cpp"
  "${PROJECT_ROOT}/main/app/features/music/util/music_visualizer.cpp"
  "${PROJECT_ROOT}/main/app/ui/fonts/material_status_icons_20.c"
  "${PROJECT_ROOT}/main/app/core/event/event_bus.cpp"
  "${PROJECT_ROOT}/main/app/features/music/music_model.cpp"
  "${PROJECT_ROOT}/main/app/features/music/music_presenter.cpp"
  "${PROJECT_ROOT}/main/app/features/music/music_view.cpp"
  "${PROJECT_ROOT}/main/app/features/music/widgets/background_image.cpp"
  "${PROJECT_ROOT}/main/app/features/music/widgets/background_widget.cpp"
  "${PROJECT_ROOT}/main/app/features/music/widgets/cover_widget.cpp"
  "${PROJECT_ROOT}/main/app/features/music/widgets/visualizer_widget.cpp"
  "${PROJECT_ROOT}/main/app/screens/music_screen.cpp"
  "${PROJECT_ROOT}/main/app/services/cover_service.cpp"
  "${PROJECT_ROOT}/main/app/services/mqtt_service.cpp"
  "${PROJECT_ROOT}/main/app/ui/fonts/music_fonts.cpp")

target_compile_definitions(music_ui_sim PRIVATE
  SIM_BUILD
  SIM_REPO_ROOT="${PROJECT_ROOT}")

target_include_directories(music_ui_sim PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}"
  "${CMAKE_CURRENT_LIST_DIR}/include"
  "${PROJECT_ROOT}/main"
  "${PROJECT_ROOT}/managed_components/lvgl__lvgl"
  "${PROJECT_ROOT}/managed_components/lvgl__lvgl/src"
  ${SDL2_INCLUDE_DIRS})

target_compile_options(music_ui_sim PRIVATE ${SDL2_CFLAGS_OTHER})
target_link_libraries(music_ui_sim PRIVATE lvgl PkgConfig::SDL2)

if(APPLE)
  target_link_libraries(music_ui_sim PRIVATE "-framework CoreFoundation")
endif()
