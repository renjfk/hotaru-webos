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

if(CMAKE_CROSSCOMPILING)
    install(TARGETS chiaki RUNTIME DESTINATION .)
    install(FILES appinfo.json icon.png largeIcon.png DESTINATION .)

    set(CPACK_GENERATOR "External")
    set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_SOURCE_DIR}/AresPackage.cmake")
    set(CPACK_EXTERNAL_ENABLE_STAGING TRUE)
    set(CPACK_MONOLITHIC_INSTALL TRUE)
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")
    include(CPack)

    add_custom_target(chiaki-package COMMAND cpack)
endif()