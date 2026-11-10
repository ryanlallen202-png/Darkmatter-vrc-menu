# Darkmatter audio and legacy client sources

## Supported build

The maintained CMake target is a standalone local soundboard library. It does
not modify, repackage, or load into VRChat. Legacy client files (`main.cpp`,
`features.cpp`, `menu.cpp`, `esp.cpp`, and `shared.hpp`) remain in the repository
but are not compiled by this target. Their runtime behavior is unverified.
No APK is produced, and compatibility with the linked VRChat APK is not established.

### Host tests

Requires CMake 3.22+ and a C++17 compiler:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Android library

Requires Android NDK r27c (27.2.12479018):

```sh
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Output: `build/libdarkmatter-audio.so`. An Android host application must link
and call the C++ API in `soundboard.hpp`; the library has no automatic startup
or JNI entry point. Call `Soundboard_Init()` before playback and
`Soundboard_Shutdown()` before unloading. Shutdown stops the player before
releasing PCM memory. API calls are serialized; they are not audio-callback
safe. Clip names are static strings. The twelve clips are synthesized tones,
not recordings of their labels.

GitHub Actions runs the host tests and Android compilation on pushes, pull
requests, and manual dispatch. The Android job uploads
`darkmatter-audio-arm64`; it does not sign or distribute an APK.
