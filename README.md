# Darkmatter Quest companion

## Supported build

The maintained CMake target is a standalone local soundboard library. It does
not modify, repackage, or load into VRChat. Legacy client files (`main.cpp`,
`features.cpp`, `menu.cpp`, `esp.cpp`, and `shared.hpp`) remain in the repository
but are not compiled by this target. Their runtime behavior is unverified.
The Android app packages that library into a separate, installable 2D APK for
Quest/Android (Android 10+, ARM64). It has Soundboard, Settings, and Logs tabs.
It is not connected to VRChat and does not route audio into its microphone.
It does not need the VRChat APK, network access, or storage permissions.
Headset behavior remains unverified until device testing.

## Test APK

GitHub Actions builds `darkmatter-quest-debug-apk`, containing `app-debug.apk`.
This is a debug-signed sideload build, not a store release. Enable developer
mode on the headset and install using your usual sideload tool, or:

```sh
adb install -r app-debug.apk
adb shell am start -n com.darkmatter.companion/.MainActivity
```

Open it as a 2D app from Unknown Sources on Quest. Audio stops when the app
leaves the foreground. Use headset media volume; clips are synthesized tones,
not recordings. No in-game feature controls are exposed.

### Logs

At startup, on pause, on detected audio errors, and on Java crashes, the app
attempts to save a report in **Download/Darkmatter/darkmatter-<timestamp>.txt**.
By default it also updates the report after each playback. Logs tab provides
**Export log to Downloads** and a non-crashing **Test error logging** button.
One report is updated per app session; old session reports remain until deleted.

Reports contain app events, Android/device version, recent logcat from this
process only, and previous process-exit information when Android exposes it.
Private logs rotate at approximately 256 KiB. Exports include up to two private
log segments plus bounded process logcat. Review reports before sharing.

Force-stop, power loss, out-of-memory termination, native crashes, storage-full
conditions, or a Downloads-provider failure can prevent an immediate export.
On the next launch, the app attempts to include previous-exit details (Android
11+). Native crash traces are device-dependent and are not guaranteed. If no
Downloads file appears, use:

```sh
adb logcat -d -s Darkmatter soundboard AndroidRuntime
adb shell run-as com.darkmatter.companion cat files/session.log
```

### Build the APK

Requires JDK 17, Gradle 8.9, Android SDK 35, NDK r27c, CMake 3.22.1, and an
`ANDROID_HOME` environment variable or local `local.properties` SDK path:

```sh
gradle --no-daemon :app:assembleDebug :app:lintDebug
```

Output: `app/build/outputs/apk/debug/app-debug.apk`. CI verifies the signature.
The automatically generated CI debug signing key is not stable across runners;
if Android rejects an update due to a different signature, uninstall the old
test build first (this deletes its private settings and private logs). Export
logs before uninstalling. Downloads exports remain user-managed.


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
and call the C++ API in `soundboard.hpp`; the library has no automatic startup. The APK uses explicit JNI bindings. Call `Soundboard_Init()` before playback and
`Soundboard_Shutdown()` before unloading. Shutdown stops the player before
releasing PCM memory. API calls are serialized; they are not audio-callback
safe. Clip names are static strings. The twelve clips are synthesized tones,
not recordings of their labels.

GitHub Actions runs the host tests and Android compilation on pushes, pull
requests, and manual dispatch. The Android job uploads
`darkmatter-audio-arm64`. A separate job builds, lints, signature-verifies, and
uploads the debug APK as `darkmatter-quest-debug-apk`.
