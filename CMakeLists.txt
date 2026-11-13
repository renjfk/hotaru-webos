cmake_minimum_required(VERSION 3.16)
include(cmake/AppVersion.cmake)
project(hotaru VERSION ${HOTARU_VERSION} LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# every .c under src/ is part of the app: a new module needs no CMake change
file(GLOB_RECURSE HOTARU_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.c")
add_executable(hotaru ${HOTARU_SOURCES})
target_compile_options(hotaru PRIVATE -Wall -Wextra)
target_compile_definitions(hotaru PRIVATE HOTARU_VERSION="${HOTARU_VERSION}")
target_include_directories(hotaru SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(hotaru PRIVATE ${SDL2_LIBRARIES})

add_subdirectory(third_party/lvgl EXCLUDE_FROM_ALL)
target_include_directories(lvgl SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(lvgl PRIVATE ${SDL2_LIBRARIES})
target_compile_definitions(lvgl PUBLIC LV_CONF_PATH=../../../src/lv_conf.h)

target_link_libraries(hotaru PRIVATE lvgl)

# libchiaki (optional): cross-built by scripts/chiaki-lib-build-webos.sh into a
# staging tree (include/ + lib/ + link.txt). Point CHIAKI_LIB_STAGING at it to
# build the real session backend (chiaki/session_real.c); otherwise session.c
# simulates. The link line is read from link.txt, the ordered list the build
# script verifies (a glob would duplicate symbols: libevent.a contains the rest).
set(CHIAKI_LIB_STAGING "" CACHE PATH "libchiaki staging dir from scripts/chiaki-lib-build-webos.sh")
if(CHIAKI_LIB_STAGING)
    if(NOT EXISTS "${CHIAKI_LIB_STAGING}/lib/libchiaki.a")
        message(FATAL_ERROR "CHIAKI_LIB_STAGING=${CHIAKI_LIB_STAGING} has no lib/libchiaki.a")
    endif()
    target_include_directories(hotaru SYSTEM PRIVATE "${CHIAKI_LIB_STAGING}/include")
    target_compile_definitions(hotaru PRIVATE HOTARU_HAVE_LIBCHIAKI=1)
    # link.txt is the exact ordered link line the build script verified. Prefer it over a glob:
    # libevent.a already contains libevent_core/extra/pthreads, so globbing defines symbols twice.
    # Archives are recorded as @STAGING@/lib/... so a cached or moved staging tree still links.
    file(READ "${CHIAKI_LIB_STAGING}/link.txt" CHIAKI_LINK_LINE)
    string(REPLACE "@STAGING@" "${CHIAKI_LIB_STAGING}" CHIAKI_LINK_LINE "${CHIAKI_LINK_LINE}")
    separate_arguments(CHIAKI_LINK_ARGS NATIVE_COMMAND "${CHIAKI_LINK_LINE}")
    target_link_libraries(hotaru PRIVATE ${CHIAKI_LINK_ARGS})
endif()

if(CMAKE_CROSSCOMPILING)
    # NDL DirectMedia: the TV's hardware video/audio pipeline. Headers + lib ship in the SDK
    # sysroot (usr/include/libndl-media, lib/libNDL_directmedia); media/ndl.c uses them when found.
    find_path(NDL_INCLUDE_DIR NAMES NDL_directmedia_v2.h PATH_SUFFIXES libndl-media)
    find_library(NDL_LIBRARY NAMES NDL_directmedia)
    if(NDL_INCLUDE_DIR AND NDL_LIBRARY)
        message(STATUS "NDL DirectMedia: ${NDL_LIBRARY} (${NDL_INCLUDE_DIR})")
        target_compile_definitions(hotaru PRIVATE HOTARU_HAVE_NDL=1)
        target_include_directories(hotaru SYSTEM PRIVATE "${NDL_INCLUDE_DIR}")
        target_link_libraries(hotaru PRIVATE "${NDL_LIBRARY}")
    else()
        message(WARNING "NDL DirectMedia not found: building without hardware media")
    endif()

    install(TARGETS hotaru RUNTIME DESTINATION .)
    install(FILES appinfo.json assets/icon.png assets/largeIcon.png DESTINATION .)
    install(DIRECTORY assets/devices DESTINATION assets)   # device art, read next to the binary (ui/art.c)
    # the app's license, the bundled components' notices and their license texts, which their licenses
    # require next to a binary
    install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
    install(FILES LICENSES/lodepng.txt LICENSES/qrcodegen.txt LICENSES/OFL-1.1.txt DESTINATION licenses)
    install(FILES third_party/lvgl/LICENCE.txt DESTINATION licenses RENAME lvgl.txt)
    if(CHIAKI_LIB_STAGING)
        if(EXISTS "${CHIAKI_LIB_STAGING}/licenses")
            install(DIRECTORY "${CHIAKI_LIB_STAGING}/licenses/" DESTINATION licenses)
        else()
            message(WARNING "${CHIAKI_LIB_STAGING} has no licenses/: re-run scripts/chiaki-lib-build-webos.sh")
        endif()
    endif()

    set(CPACK_GENERATOR "External")
    set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/AresPackage.cmake")
    set(CPACK_EXTERNAL_ENABLE_STAGING TRUE)
    set(CPACK_MONOLITHIC_INSTALL TRUE)
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")
    include(CPack)

    add_custom_target(hotaru-package COMMAND cpack)
endif()