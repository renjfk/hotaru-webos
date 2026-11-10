# LG webOS TV simulator (macOS): builds the real app (main.c + display.c
# included) against SDL2 + LVGL and runs it in a window. See sim/run.sh.
#
# Needs `brew install sdl2` and LVGL in third_party/ (./build.sh fetches it).
# gamepad.c is compiled against tests/host/shim so no /dev/input is required;
# INPUT_DIR points at a nonexistent dir, so the app runs in mouse + keyboard mode.
cmake_minimum_required(VERSION 3.16)
project(chiaki-sim C)
set(CMAKE_C_STANDARD 11)
get_filename_component(APP "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

add_subdirectory(${APP}/third_party/lvgl lvgl EXCLUDE_FROM_ALL)
target_include_directories(lvgl SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(lvgl PRIVATE ${SDL2_LINK_LIBRARIES})
target_compile_definitions(lvgl PUBLIC LV_CONF_PATH=../../../src/lv_conf.h)

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS ${APP}/src/*.c)
add_executable(chiaki-sim ${SRC})
target_compile_definitions(chiaki-sim PRIVATE CHIAKI_SIM INPUT_DIR="${CMAKE_BINARY_DIR}/devinput")
target_include_directories(chiaki-sim PRIVATE ${APP}/tests/host/shim ${APP}/src ${SDL2_INCLUDE_DIRS})
target_compile_options(chiaki-sim PRIVATE -Wall -Wextra)
target_link_libraries(chiaki-sim PRIVATE lvgl ${SDL2_LINK_LIBRARIES})