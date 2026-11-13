# LG webOS TV simulator (macOS): builds the real app (main.c + display.c
# included) against SDL2 + LVGL and runs it in a window. See sim/run.sh.
#
# Needs `brew install sdl2` and LVGL in third_party/ (./build.sh fetches it).
# gamepad.c is compiled against tests/host/shim so no /dev/input is required;
# INPUT_DIR points at a nonexistent dir, so the app runs on the keyboard (the TV remote) alone.
cmake_minimum_required(VERSION 3.16)
project(chiaki-sim C)
set(CMAKE_C_STANDARD 11)
get_filename_component(APP "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
include(${APP}/cmake/AppVersion.cmake)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

add_subdirectory(${APP}/third_party/lvgl lvgl EXCLUDE_FROM_ALL)
target_include_directories(lvgl SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(lvgl PRIVATE ${SDL2_LINK_LIBRARIES})
target_compile_definitions(lvgl PUBLIC LV_CONF_PATH=../../../src/lv_conf.h)

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS ${APP}/src/*.c)
add_executable(chiaki-sim ${SRC})
target_compile_definitions(chiaki-sim PRIVATE CHIAKI_SIM INPUT_DIR="${CMAKE_BINARY_DIR}/devinput"
                                              CHIAKI_ASSETS_DIR="${APP}/assets"
                                              CHIAKI_WEBOS_VERSION="${CHIAKI_WEBOS_VERSION}")
target_include_directories(chiaki-sim PRIVATE ${APP}/tests/host/shim ${SDL2_INCLUDE_DIRS})
target_compile_options(chiaki-sim PRIVATE -Wall -Wextra)
target_link_libraries(chiaki-sim PRIVATE lvgl ${SDL2_LINK_LIBRARIES})

# Native libchiaki (optional): when scripts/chiaki-lib-build-macos.sh has staged
# it, build the REAL session backend (chiaki/session_real.c) so the simulator
# exercises real PSN/registration instead of the simulation. Same shape as the
# webOS staging: include/ + lib/ + link.txt (the ordered, verified link line).
set(CHIAKI_LIB_STAGING "" CACHE PATH "native libchiaki staging dir from scripts/chiaki-lib-build-macos.sh")
if(NOT CHIAKI_LIB_STAGING AND EXISTS "${APP}/out/chiaki-lib-macos/lib/libchiaki.a")
    set(CHIAKI_LIB_STAGING "${APP}/out/chiaki-lib-macos")
endif()
if(CHIAKI_LIB_STAGING)
    if(NOT EXISTS "${CHIAKI_LIB_STAGING}/lib/libchiaki.a")
        message(FATAL_ERROR "CHIAKI_LIB_STAGING=${CHIAKI_LIB_STAGING} has no lib/libchiaki.a")
    endif()
    message(STATUS "chiaki-sim: using native libchiaki from ${CHIAKI_LIB_STAGING}")
    # session_real.c also uses json-c directly for the PSN token exchange
    pkg_check_modules(JSONC REQUIRED json-c)
    target_include_directories(chiaki-sim SYSTEM PRIVATE "${CHIAKI_LIB_STAGING}/include" ${JSONC_INCLUDE_DIRS})
    target_compile_definitions(chiaki-sim PRIVATE CHIAKI_HAVE_LIBCHIAKI=1)
    file(READ "${CHIAKI_LIB_STAGING}/link.txt" CHIAKI_LINK_LINE)
    separate_arguments(CHIAKI_LINK_ARGS NATIVE_COMMAND "${CHIAKI_LINK_LINE}")
    target_link_libraries(chiaki-sim PRIVATE ${CHIAKI_LINK_ARGS})
    # system frameworks the static libcurl/libchiaki needs on macOS; link.txt
    # cannot carry them (CMake mangles -framework), so resolve them here
    foreach(fw CoreFoundation CoreServices Security SystemConfiguration)
        find_library(FW_${fw} ${fw})
        if(NOT FW_${fw})
            message(FATAL_ERROR "chiaki-sim: macOS framework ${fw} not found")
        endif()
        target_link_libraries(chiaki-sim PRIVATE ${FW_${fw}})
    endforeach()
endif()