cmake_minimum_required(VERSION 3.22)
project(darkmatter VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Sources ──────────────────────────────────────────────────────────────────
add_library(darkmatter SHARED
    main.cpp
    features.cpp
    menu.cpp
    esp.cpp
    soundboard.cpp
)

# ── Compiler flags (AArch64, NDK26) ──────────────────────────────────────────
target_compile_options(darkmatter PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -O2
    -fvisibility=hidden          # only JNI_OnLoad/JNI_OnUnload exported
    -ffunction-sections
    -fdata-sections
    -fstack-protector-strong
    -DANDROID
)

target_link_options(darkmatter PRIVATE
    -Wl,--gc-sections
    -Wl,--strip-all
    -Wl,-z,relro
    -Wl,-z,now
)

# ── NDK system libs ───────────────────────────────────────────────────────────
find_library(log-lib   log)
find_library(dl-lib    dl)
find_library(android-lib android)

target_link_libraries(darkmatter
    ${log-lib}
    ${dl-lib}
    ${android-lib}
)

# ── Include paths ──────────────────────────────────────────────────────────────
target_include_directories(darkmatter PRIVATE ${CMAKE_SOURCE_DIR})
