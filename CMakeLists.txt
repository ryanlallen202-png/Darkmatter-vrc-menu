cmake_minimum_required(VERSION 3.22)
project(darkmatter LANGUAGES CXX)

# ── Toolchain enforced by Android Gradle / build.gradle ─────────────────────
# NDK r26c, ABI arm64-v8a, API 32, C++17

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Strict warnings — ship clean ─────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wno-unused-parameter
    -fvisibility=hidden
    -ffunction-sections -fdata-sections
    -O2
)

# ── ShadowHook (pre-built AAR ships .so + headers) ───────────────────────────
# Place the extracted shadowhook AAR under libs/shadowhook/ in the module root.
# Headers: libs/shadowhook/include/shadowhook.h
# .so:     libs/shadowhook/jni/arm64-v8a/libshadowhook.so
set(SHADOWHOOK_DIR "${CMAKE_SOURCE_DIR}/libs/shadowhook")
add_library(shadowhook SHARED IMPORTED)
set_target_properties(shadowhook PROPERTIES
    IMPORTED_LOCATION "${SHADOWHOOK_DIR}/jni/arm64-v8a/libshadowhook.so"
)
target_include_directories(shadowhook INTERFACE "${SHADOWHOOK_DIR}/include")

# ── FFmpegKit (all-codecs AAR, arm64-v8a) ────────────────────────────────────
# Place extracted AAR under libs/ffmpegkit/
# Headers: libs/ffmpegkit/include/
# .so:     libs/ffmpegkit/jni/arm64-v8a/libffmpegkit.so (+ libavcodec etc.)
set(FFMPEGKIT_DIR "${CMAKE_SOURCE_DIR}/libs/ffmpegkit")
add_library(ffmpegkit SHARED IMPORTED)
set_target_properties(ffmpegkit PROPERTIES
    IMPORTED_LOCATION "${FFMPEGKIT_DIR}/jni/arm64-v8a/libffmpegkit.so"
)
target_include_directories(ffmpegkit INTERFACE "${FFMPEGKIT_DIR}/include")

# ── Darkmatter shared library ─────────────────────────────────────────────────
add_library(darkmatter SHARED
    # v2 IL2CPP resolver (from zip)
    il2cpp_init.cpp

    # Existing repo modules (kept as-is)
    features.cpp
    esp.cpp
    menu.cpp
    soundboard.cpp   # replaced by zip's FFmpegKit version

    # New modules from zip
    gunlib.cpp
    menu_renderer.cpp
    movement.cpp
    player_module.cpp

    # New main (replaces repo's main.cpp — rename before build)
    main_new.cpp
)

target_include_directories(darkmatter PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${SHADOWHOOK_DIR}/include
    ${FFMPEGKIT_DIR}/include
)

target_link_libraries(darkmatter
    android
    log
    EGL
    GLESv2
    shadowhook
    ffmpegkit
)

# Strip dead code
target_link_options(darkmatter PRIVATE -Wl,--gc-sections)
