cmake_minimum_required(VERSION 3.22)
project(vrc-quest LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Target: arm64-v8a shared library loaded by the VRChat Quest process.
# Built as a plugin (.so) — not an app. No STL exceptions across the IL2CPP
# boundary, no RTTI, and libc++ linked statically so the loader never has to
# resolve a second copy inside VRChat.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_library(vrc-quest SHARED
    main.cpp
    features.cpp
    menu.cpp
    esp.cpp
    soundboard.cpp
    audio_decode.cpp
    photon.cpp
    gunlib.cpp
    hook.cpp
    event_pump.cpp
)

target_include_directories(vrc-quest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Strict, but the IL2CPP boundary legitimately needs void* casts; those are
# contained in shared.hpp's Invoke() and the module call sites, not blanket
# disabled.
target_compile_options(vrc-quest PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -fno-exceptions
    -fno-rtti
    -O2
    -ffunction-sections
    -fdata-sections
)

target_link_options(vrc-quest PRIVATE
    -Wl,--gc-sections
    -Wl,--exclude-libs,ALL
    -Wl,-z,max-page-size=16384    # Quest 2/3 pages are 16 KB
)

# Android system libraries only exist in the NDK sysroot. CMake sets
# ANDROID when the toolchain file is in use, so the host syntax check never
# tries to resolve them.
if(ANDROID)
    find_library(log-lib log REQUIRED)
    find_library(android-lib android REQUIRED)
    find_library(dl-lib dl REQUIRED)
    target_link_libraries(vrc-quest PRIVATE ${log-lib} ${android-lib} ${dl-lib})
else()
    target_link_libraries(vrc-quest PRIVATE dl pthread)
endif()

# JNI_OnLoad is the only symbol the loader needs exported.
set_target_properties(vrc-quest PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "vrc-quest"
    CXX_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
)

# ---------------------------------------------------------------------------
# Host syntax check — never part of the shipped artifact.
#   cmake -B build-host -DVRC_HOST_CHECK=ON && cmake --build build-host
# Compiles every translation unit against tiny JNI/OpenSLES/log stubs so a
# header mistake fails here instead of in CI.
# ---------------------------------------------------------------------------
option(VRC_HOST_CHECK "Compile all sources against host stubs" OFF)
if(VRC_HOST_CHECK)
    add_executable(vrc-host-check EXCLUDE_FROM_ALL
        main.cpp features.cpp menu.cpp esp.cpp soundboard.cpp
        audio_decode.cpp photon.cpp gunlib.cpp hook.cpp event_pump.cpp
    )
    target_include_directories(vrc-host-check PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hoststub
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_options(vrc-host-check PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(vrc-host-check PRIVATE dl pthread)
endif()

# ---------------------------------------------------------------------------
# Output layout: the workflow copies build/libvrc-quest.so to the artifact.
# ---------------------------------------------------------------------------
set_target_properties(vrc-quest PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
