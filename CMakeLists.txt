cmake_minimum_required(VERSION 3.16)
project(keyboard_configurator LANGUAGES CXX)

find_package(hidapi REQUIRED)

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
    src/effect_engine.cpp
    src/config_loader.cpp
    src/configurator_cli.cpp
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
)

add_executable(kb_configurator src/main.cpp)

target_link_libraries(kb_configurator
    PRIVATE
        keyboard_configurator
)
