# Concept gallery (macOS): the main menu and "Add a console" studies in design/menu/, built against
# the app's LVGL and theme tokens, run in a window or recorded headless. See design/menu/README.md.
#
# Needs `brew install sdl2` and LVGL in third_party/ (./build.sh fetches it). Nothing here is
# part of the app build: src/ is not globbed, only theme.c (the tokens and theme) and the QR
# widget are borrowed.
cmake_minimum_required(VERSION 3.16)
project(chiaki-menu-gallery C)
set(CMAKE_C_STANDARD 11)
get_filename_component(APP "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

add_subdirectory(${APP}/third_party/lvgl lvgl EXCLUDE_FROM_ALL)
target_include_directories(lvgl SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
target_compile_definitions(lvgl PUBLIC LV_CONF_PATH=../../../design/menu/lv_conf.h)

file(GLOB SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/*.c)
# the app's QR widget and its encoder: the PSN sign-in card of the "Add a console" study
add_executable(menu-gallery ${SRC} ${APP}/src/ui/theme.c ${APP}/src/ui/qr.c ${APP}/src/vendor/qrcodegen.c)
target_include_directories(menu-gallery PRIVATE ${APP}/src ${SDL2_INCLUDE_DIRS})
target_compile_definitions(menu-gallery PRIVATE ART_DIR="${CMAKE_SOURCE_DIR}/art")
target_compile_options(menu-gallery PRIVATE -Wall -Wextra)
target_link_libraries(menu-gallery PRIVATE lvgl ${SDL2_LINK_LIBRARIES} m)
