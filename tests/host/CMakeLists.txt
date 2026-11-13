# Host (macOS) behaviour tests: every src/ module except main.c and the SDL
# display, rendered offscreen with a fake evdev pad. Run via tests/host/run.sh.
cmake_minimum_required(VERSION 3.16)
project(hotaru-host-tests C)
set(CMAKE_C_STANDARD 11)
get_filename_component(APP "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)
include(${APP}/cmake/AppVersion.cmake)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

add_subdirectory(${APP}/third_party/lvgl lvgl EXCLUDE_FROM_ALL)
target_include_directories(lvgl SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(lvgl PRIVATE ${SDL2_LINK_LIBRARIES})
target_compile_definitions(lvgl PUBLIC LV_CONF_PATH=../../../src/lv_conf.h)

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS ${APP}/src/*.c)
list(FILTER SRC EXCLUDE REGEX "/src/main\\.c$|/src/platform/display\\.c$")
add_executable(host-tests harness.c ${SRC})
target_compile_definitions(host-tests PRIVATE INPUT_DIR="${CMAKE_BINARY_DIR}/devinput"
                                              SYS_INPUT_DIR="${CMAKE_BINARY_DIR}/sys/class/input"
                                              SHOTS_DIR="${CMAKE_BINARY_DIR}/shots"
                                              CONFIG_DIR="${CMAKE_BINARY_DIR}/config"
                                              HOTARU_ASSETS_DIR="${APP}/assets"
                                              HOTARU_VERSION="${HOTARU_VERSION}")
target_include_directories(host-tests PRIVATE ${CMAKE_SOURCE_DIR}/shim ${APP}/src ${SDL2_INCLUDE_DIRS})
target_compile_options(host-tests PRIVATE -Wall -Wextra -Wno-unused-function)
target_link_libraries(host-tests PRIVATE lvgl ${SDL2_LINK_LIBRARIES})
