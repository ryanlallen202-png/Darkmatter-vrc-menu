# Dark Matter — VRChat Quest mod menu

Native arm64 plugin for the VRChat Quest client. IL2CPP reflection for game
state, a world-space uGUI menu anchored to the left hand, a right-index-finger
cursor, and a Photon bridge for the networked features.

Nothing is injected into the APK. The library is loaded by a rootless loader
(LSPatch / Zygisk-detached module) or by a build you control; the plugin itself
only assumes `JNI_OnLoad` is called inside the VRChat process.

---

## Menu

| Control | Action |
| --- | --- |
| L3 (left joystick click) | open / close |
| Right index finger | cursor |
| Right trigger | click |
| Right thumbstick Y | drag a slider while it is held |
| Dock button | anchor in front of the face instead of the hand |

Tabs: **Player · Exploits · Visual · Players · Sound · GunLib · Misc · Config**

---

## Features

### Player
- **Fly** — hold A, camera-forward at the selected tier (4 / 8 / 16 / 24 m/s)
- **Velocity Fly** — fixed 20 m/s impulse along camera forward
- **Noclip** — layer-mask patch, restored on toggle-off
- **Punch Mod** — right trigger launches the nearest player inside 5 m
- **Low / No Gravity** — gravity strength 0.5 / 0
- **Speed Hack** — 1.5× / 2× / 3× / 5× / 8× horizontal, tier cycles with the row
- **Spin** — tangential input each frame
- **Portal Gun** — right grip + trigger teleports 20 m along the view ray
- **Save / Go To Spot** — one world position, held for the session

### Visual
- **ESP** — name + distance above every remote player, camera-projected,
  80 m cull, off-frustum culled
- **Skybox** — 10 presets or rainbow, original skybox restored on disable
- **World Blocks** — per-material colour, rainbow option
- **Orbit Cam** — detached camera orbit

### Exploits
Freeze Players · Big Head · Invisible (renderer cache, exact restore) ·
Name Spoof · Anti-EAC

### Players
Live roster with distance. **Click a row to select that player as the GunLib
target.**

### Sound
12 built-in clips, all playable locally and broadcastable to the room.
`Play Broadcast` sends the highlighted clip through Photon; `Stop All Audio`
sends the stop frame.

Runtime files: `Soundboard::AddFile(path)` decodes **WAV (PCM 8/16/24/32 +
float), MP3, OGG/Vorbis, Opus, FLAC, M4A/AAC, AMR**, resamples to 44.1 kHz mono,
and appends to the clip table. WAV is parsed natively; everything else goes
through Android `MediaExtractor` + `MediaCodec` over JNI.

### GunLib
- **Crash** — oversized actor property table (32 × 8 KB); bounded, single op
- **Lag** — paced `RaiseEvent` at 5–300 ev/s, five payload sizes, stop path
  joins the thread
- **Kick** — master-client `OpKickPlayer` only. Non-master sends are refused and
  logged: VRChat validates kick authority server-side, so the old forged-event
  path would be dropped or flagged rather than working.

---

## Build

```bash
# Android, arm64, 16 KB pages
cmake -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# → build/libvrc-quest.so
```

Host syntax gate (no NDK needed — compiles every unit against the stubs in
`third_party/hoststub/`):

```bash
cmake -B build-host -DVRC_HOST_CHECK=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --target vrc-host-check -j
```

CI runs the host gate first, then the NDK build, then verifies that
`JNI_OnLoad` is exported from the artifact.

---

## Layout

```
main.cpp          IL2CPP resolution, update thread, ordered bring-up/teardown
shared.hpp        IL2CPP ABI, Ref (GC handle), player cache, module contracts
features.cpp      class cache, field reflection, movement, skybox, invisibility
menu.cpp          uGUI canvas, widgets, hit test, row dispatch
esp.cpp           camera projection, label pool
soundboard.cpp    OpenSLES engine, clip table, frame transport
audio_decode.cpp  WAV native + MediaExtractor/MediaCodec bridge
photon.cpp        LoadBalancingClient bridge — RaiseEvent / properties / kick
gunlib.cpp        crash / lag / kick ops
hook.cpp          AArch64 inline hook engine (12-byte patch, trampoline)
event_pump.cpp    OnEvent hook → soundboard receive path
```

---

## Offsets

Every game-specific value lives in **one** place. Nothing else in the tree
carries a raw offset.

| Where | What | How to fill |
| --- | --- | --- |
| `features.cpp` → `InitFields()` | `VRCPlayer.isLocal`, `VRCPlayer.Descriptor` | resolved by name at runtime through `il2cpp_class_get_field_from_name`; if the build renames them, the probe list already carries the two known spellings |
| `features.cpp` → `EACOff{}` | EAC function offsets | Il2CppDumper / Ghidra on `libil2cpp.so`; zero means unpatched, never a stray write |
| `shared.hpp` → `Offset{}` | reserved for builds that need a literal | same |

The EAC symbol layer (`EOS_AntiCheatClient_*` in `libEOSSDK-Android-Shipping.so`)
needs no offsets at all — it resolves by `dlsym` and neutralizes whatever the
current build exports.

---

## Behaviour contracts

**Fail closed.** An unresolved class, method, or field logs once and the
feature refuses to run. Nothing writes through a null pointer, and a zero
offset is never patched.

**Bounded.** Player lists cap at 80, renderer cache at 4096, arrays at 64 KB,
decode output at 10 minutes, crash payload at 256 KB, lag at 300 ev/s.

**Reversible.** Invisibility stores the exact renderer set and restores it.
Skybox keeps the original material. Gravity resets to 1.0 on `All Off` and on
shutdown. Hooks are detached on unload.

**Ordered teardown.** `JNI_OnUnload` stops the tick, then the UI, the event
pump, the soundboard, the Photon bridge, and finally the feature state — each
step releasing what the step above it was reading.

---

## Notes

- Quest 2/3 only: arm64-v8a, `android-29`, 16 KB page size.
- `JNI_OnLoad` is idempotent; a loader that calls it twice does not start a
  second tick thread.
- The inline hook refuses any function whose first three instructions are
  PC-relative. That is a deliberate refusal, not a bug: relocating an `ADRP`
  or a literal load would silently corrupt the trampoline.
- Anti-EAC symbol neutralization is on by default (`f_antiEac`). Off is a
  toggle; off means the symbols stay as shipped.

Test on a throwaway account. Crash and lag affect the whole room including you.
