cmake_minimum_required(VERSION 3.16)
project(sinodragon LANGUAGES CXX)

# --- Dependencies ---
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)
endif()

# 1. FIND TOML++ (System Package)
find_package(tomlplusplus CONFIG REQUIRED)

# 2. FIND HIDAPI
# Arch and vcpkg ship a CMake config package; Debian/Ubuntu only ship pkg-config
# files, so fall back to those rather than hard-failing.
find_package(hidapi QUIET)
set(HIDAPI_TARGET "")
if(TARGET hidapi::hidapi)
    set(HIDAPI_TARGET hidapi::hidapi)
elseif(TARGET hidapi::hidapi-hidraw)
    set(HIDAPI_TARGET hidapi::hidapi-hidraw)
elseif(PkgConfig_FOUND)
    pkg_check_modules(HIDAPI QUIET IMPORTED_TARGET hidapi-hidraw)
    if(NOT TARGET PkgConfig::HIDAPI)
        pkg_check_modules(HIDAPI QUIET IMPORTED_TARGET hidapi-libusb)
    endif()
    if(NOT TARGET PkgConfig::HIDAPI)
        pkg_check_modules(HIDAPI QUIET IMPORTED_TARGET hidapi)
    endif()
    if(TARGET PkgConfig::HIDAPI)
        set(HIDAPI_TARGET PkgConfig::HIDAPI)
    endif()
endif()
if(HIDAPI_TARGET STREQUAL "")
    message(FATAL_ERROR "hidapi not found (looked for a CMake config package and for the hidapi-hidraw/hidapi-libusb/hidapi pkg-config modules).")
endif()

# --- Compiler Settings ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Main Library ---
add_library(sinodragon_core STATIC
    src/keyboard_model.cpp
    src/key_color_frame.cpp
    src/types.cpp
    src/key_activity.cpp
    src/key_activity_watcher.cpp
    src/preset_registry.cpp
    src/static_color_preset.cpp
    src/rainbow_wave_preset.cpp
    src/key_map_preset.cpp
    src/star_matrix_preset.cpp
    src/liquid_plasma_preset.cpp
    src/reaction_diffusion_preset.cpp
    src/smoke_preset.cpp
    src/doom_fire_preset.cpp
    src/reactive_ripple_preset.cpp
    src/space_colonization_preset.cpp
    src/snake_preset.cpp
    src/game_preset.cpp
    src/tetris_preset.cpp
    src/pong_preset.cpp
    src/life_preset.cpp
    src/typing_heatmap_preset.cpp
    src/system_state.cpp
    src/system_meter_preset.cpp
    src/status_light_preset.cpp
    src/effect_engine.cpp
    src/runtime.cpp
    src/app_options.cpp
    src/control_server.cpp
    src/shutdown_signal.cpp
    src/single_instance.cpp
    src/config_loader.cpp
    src/configurator_cli.cpp
    src/window_source.cpp
    src/window_source_hyprland.cpp
    src/window_source_sway.cpp
    src/window_source_x11.cpp
    src/json_lite.cpp
    src/shortcut_watcher.cpp
    src/logging_transport.cpp
    src/preview_transport.cpp
    src/hidapi_transport.cpp
)

target_include_directories(sinodragon_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(sinodragon_core
    PUBLIC
        ${HIDAPI_TARGET}
        $<IF:$<TARGET_EXISTS:PkgConfig::LIBEVDEV>,PkgConfig::LIBEVDEV,>
        # 2. LINK TOML++ (This automatically handles include paths)
        tomlplusplus::tomlplusplus
)

# Fallback if pkg-config is not available
if(NOT TARGET PkgConfig::LIBEVDEV)
    find_library(LIBEVDEV_LIB NAMES libevdev)
    if(LIBEVDEV_LIB)
        target_link_libraries(sinodragon_core PUBLIC ${LIBEVDEV_LIB})
    else()
        message(FATAL_ERROR "libevdev not found.")
    endif()
endif()

add_executable(sinodragon src/main.cpp)

target_link_libraries(sinodragon
    PRIVATE
        sinodragon_core
)

add_executable(sinoctl src/sinoctl.cpp)

target_link_libraries(sinoctl
    PRIVATE
        sinodragon_core
)

find_package(Threads REQUIRED)
target_link_libraries(sinodragon_core PUBLIC Threads::Threads)

# Optional X11 window source. Wayland-only setups do not need it; without it
# the x11 backend compiles to a stub that reports itself unavailable.
find_package(X11 QUIET)
if(X11_FOUND)
    target_compile_definitions(sinodragon_core PRIVATE SINODRAGON_HAVE_X11)
    target_link_libraries(sinodragon_core PUBLIC ${X11_LIBRARIES})
    target_include_directories(sinodragon_core PRIVATE ${X11_INCLUDE_DIR})
    message(STATUS "X11 window source: enabled")
else()
    message(STATUS "X11 window source: disabled (libX11 not found)")
endif()

# --- Install ---
include(GNUInstallDirs)
install(TARGETS sinodragon sinoctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/70-sinodragon.rules
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/udev/rules.d)
install(FILES packaging/sinodragon.service
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
