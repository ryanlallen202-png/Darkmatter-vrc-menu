cmake_minimum_required(VERSION 3.22)
project(darkmatter VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── NDK / ABI ────────────────────────────────────────────────────────────────
if(NOT ANDROID_ABI)
  set(ANDROID_ABI "arm64-v8a")
endif()

# ── ShadowHook paths (injected by CI or fallback to in-tree) ─────────────────
if(NOT SHADOWHOOK_INCLUDE_DIR)
  set(SHADOWHOOK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../libs/shadowhook/include")
endif()
if(NOT SHADOWHOOK_LIB_DIR)
  set(SHADOWHOOK_LIB_DIR "${CMAKE_SOURCE_DIR}/../libs/shadowhook/jni/${ANDROID_ABI}")
endif()

# ── shadowhook imported target ───────────────────────────────────────────────
add_library(shadowhook SHARED IMPORTED)

# Only wire the include dir if it actually exists on disk.
# This prevents the CMake "non-existent path" error when the fetch
# step hasn't run yet or the AAR contained no headers at that path.
if(EXISTS "${SHADOWHOOK_INCLUDE_DIR}")
  set_target_properties(shadowhook PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${SHADOWHOOK_INCLUDE_DIR}"
  )
  message(STATUS "ShadowHook headers: ${SHADOWHOOK_INCLUDE_DIR}")
else()
  message(WARNING
    "ShadowHook include dir not found: ${SHADOWHOOK_INCLUDE_DIR}\n"
    "Continuing without headers — add them or run Get ShadowHook step first."
  )
endif()

set_target_properties(shadowhook PROPERTIES
  IMPORTED_LOCATION "${SHADOWHOOK_LIB_DIR}/libshadowhook.so"
)

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
  main.cpp
  esp.cpp
  features.cpp
  menu.cpp
  soundboard.cpp
)

add_library(darkmatter SHARED ${SOURCES})

target_include_directories(darkmatter PRIVATE
  ${CMAKE_SOURCE_DIR}
)

# Link shadowhook only if the .so landed
if(EXISTS "${SHADOWHOOK_LIB_DIR}/libshadowhook.so")
  target_link_libraries(darkmatter PRIVATE shadowhook)
  message(STATUS "Linking shadowhook from ${SHADOWHOOK_LIB_DIR}")
else()
  message(WARNING
    "libshadowhook.so not found at ${SHADOWHOOK_LIB_DIR} — skipping link.\n"
    "Run Get ShadowHook workflow step to populate this path."
  )
endif()

target_link_libraries(darkmatter PRIVATE
  android
  log
)

# ── Hardening ─────────────────────────────────────────────────────────────────
target_compile_options(darkmatter PRIVATE
  -Wall -Wextra -fvisibility=hidden
  -fstack-protector-strong
  -O2
)

target_link_options(darkmatter PRIVATE
  -Wl,-z,relro
  -Wl,-z,now
  -Wl,--strip-all
)

# ── Output path mirrors AAR layout ───────────────────────────────────────────
set_target_properties(darkmatter PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/outputs/jni/${ANDROID_ABI}"
)
