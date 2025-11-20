cmake_minimum_required(VERSION 3.16)
project(keyboard_configurator LANGUAGES CXX)

find_package(hidapi REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)
endif()

set(HIDAPI_TARGET "")
if(TARGET hidapi::hidapi)
    set(HIDAPI_TARGET hidapi::hidapi)
elseif(TARGET hidapi::hidapi-hidraw)
    set(HIDAPI_TARGET hidapi::hidapi-hidraw)
else()
    message(FATAL_ERROR "hidapi target not found. Ensure hidapi is available to CMake.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(keyboard_configurator STATIC
    src/keyboard_model.cpp
    src/key_color_frame.cpp
    src/preset_registry.cpp
    src/static_color_preset.cpp
    src/rainbow_wave_preset.cpp
    src/key_map_preset.cpp
    src/star_matrix_preset.cpp
    src/liquid_plasma_preset.cpp
    src/reaction_diffusion_preset.cpp
    src/smoke_preset.cpp
    src/effect_engine.cpp
    src/config_loader.cpp
    src/configurator_cli.cpp
    src/hyprland_watcher.cpp
    src/shortcut_watcher.cpp
    src/logging_transport.cpp
    src/hidapi_transport.cpp
)

target_include_directories(keyboard_configurator
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(keyboard_configurator
    PUBLIC
        ${HIDAPI_TARGET}
        $<IF:$<TARGET_EXISTS:PkgConfig::LIBEVDEV>,PkgConfig::LIBEVDEV,>
)

# Fallback if pkg-config is not available
if(NOT TARGET PkgConfig::LIBEVDEV)
    find_library(LIBEVDEV_LIB NAMES libevdev)
    if(LIBEVDEV_LIB)
        target_link_libraries(keyboard_configurator PUBLIC ${LIBEVDEV_LIB})
    else()
        message(FATAL_ERROR "libevdev not found. Please install libevdev or provide pkg-config libevdev.")
    endif()
endif()

add_executable(kb_configurator src/main.cpp)

target_link_libraries(kb_configurator
    PRIVATE
        keyboard_configurator
)
