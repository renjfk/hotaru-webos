cmake_minimum_required(VERSION 3.16)
project(chiaki-webos VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# every .c under src/ is part of the app: a new module needs no CMake change
file(GLOB_RECURSE CHIAKI_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.c")
add_executable(chiaki ${CHIAKI_SOURCES})
target_compile_options(chiaki PRIVATE -Wall -Wextra)
target_include_directories(chiaki SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(chiaki PRIVATE ${SDL2_LIBRARIES})

add_subdirectory(third_party/lvgl EXCLUDE_FROM_ALL)
target_include_directories(lvgl SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(lvgl PRIVATE ${SDL2_LIBRARIES})
target_compile_definitions(lvgl PUBLIC LV_CONF_PATH=../../../src/lv_conf.h)

target_link_libraries(chiaki PRIVATE lvgl)

# libchiaki (optional): cross-built by scripts/chiaki-lib-build-webos.sh into a
# staging tree (include/ + lib/ + link.txt). Point CHIAKI_LIB_STAGING at it to
# build the real session backend (chiaki/session_real.c); otherwise session.c
# simulates. The archives are grouped because libchiaki and its deps are cyclic.
set(CHIAKI_LIB_STAGING "" CACHE PATH "libchiaki staging dir from scripts/chiaki-lib-build-webos.sh")
if(CHIAKI_LIB_STAGING)
    if(NOT EXISTS "${CHIAKI_LIB_STAGING}/lib/libchiaki.a")
        message(FATAL_ERROR "CHIAKI_LIB_STAGING=${CHIAKI_LIB_STAGING} has no lib/libchiaki.a")
    endif()
    target_include_directories(chiaki SYSTEM PRIVATE "${CHIAKI_LIB_STAGING}/include")
    target_compile_definitions(chiaki PRIVATE CHIAKI_HAVE_LIBCHIAKI=1)
    file(GLOB CHIAKI_LIB_ARCHIVES "${CHIAKI_LIB_STAGING}/lib/lib*.a")
    target_link_libraries(chiaki PRIVATE
        -Wl,--start-group ${CHIAKI_LIB_ARCHIVES} -Wl,--end-group
        -Wl,--allow-multiple-definition
        pthread dl rt m)
endif()

if(CMAKE_CROSSCOMPILING)
    install(TARGETS chiaki RUNTIME DESTINATION .)
    install(FILES appinfo.json assets/icon.png assets/largeIcon.png DESTINATION .)

    set(CPACK_GENERATOR "External")
    set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_SOURCE_DIR}/AresPackage.cmake")
    set(CPACK_EXTERNAL_ENABLE_STAGING TRUE)
    set(CPACK_MONOLITHIC_INSTALL TRUE)
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")
    include(CPack)

    add_custom_target(chiaki-package COMMAND cpack)
endif()