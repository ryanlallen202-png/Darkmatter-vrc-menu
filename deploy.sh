#!/usr/bin/env bash
# deploy.sh — Darkmatter VRC Quest menu
# Full pipeline: Gradle build → LSPatch → re-sign → ADB install
# absolute ollie, built by olivia — https://discord.gg/ojailbreaks
#
# Usage:
#   ./deploy.sh [path/to/VRChat_2026_4_2-1948-2b94e366a5-Release.apk]
#
# Requires:
#   Android SDK + NDK r26c (ANDROID_HOME set)
#   Java 17+
#   LSPatch 0.6+ jar  — set LSPATCH_JAR or place at ~/tools/lspatch.jar
#   ADB in PATH, Quest connected and unlocked
#
# GitHub Actions: see .github/workflows/build.yml (also updated)
#
# Pre-build: rename main_new.cpp → main.cpp in this repo before running.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── Config ────────────────────────────────────────────────────────────────────
VRCHAT_APK="${1:-$SCRIPT_DIR/../VRChat_2026_4_2-1948-2b94e366a5-Release.apk}"
MODULE_APK="$SCRIPT_DIR/app/build/outputs/apk/release/app-release.apk"
LSPATCH_JAR="${LSPATCH_JAR:-$HOME/tools/lspatch.jar}"
OUT_DIR="$SCRIPT_DIR/out"
KEYSTORE="$SCRIPT_DIR/bench.keystore"
PACKAGE="com.vrchat.oculus.quest"
ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"
BUILD_TOOLS="$ANDROID_HOME/build-tools/34.0.0"

echo "═══════════════════════════════════════════════════════"
echo "  Darkmatter VRC — absolute ollie, built by olivia"
echo "  https://discord.gg/ojailbreaks"
echo "═══════════════════════════════════════════════════════"

# ── 0. Rename main_new.cpp → main.cpp if not already done ────────────────────
if [[ -f "$SCRIPT_DIR/main_new.cpp" && ! -f "$SCRIPT_DIR/main.cpp.orig" ]]; then
    echo "[0/6] Swapping main_new.cpp → main.cpp"
    cp "$SCRIPT_DIR/main.cpp" "$SCRIPT_DIR/main.cpp.orig" 2>/dev/null || true
    cp "$SCRIPT_DIR/main_new.cpp" "$SCRIPT_DIR/main.cpp"
fi

# ── 1. Build module APK via Gradle ───────────────────────────────────────────
echo "[1/6] Building module APK (NDK r26c, arm64-v8a)..."
cd "$SCRIPT_DIR"
./gradlew assembleRelease --quiet
echo "    Module: $MODULE_APK"

# ── 2. Keystore ───────────────────────────────────────────────────────────────
if [[ ! -f "$KEYSTORE" ]]; then
    echo "[2/6] Generating keystore..."
    keytool -genkeypair -v \
        -keystore "$KEYSTORE" \
        -alias bench \
        -keyalg RSA -keysize 2048 -validity 9999 \
        -storepass benchkey -keypass benchkey \
        -dname "CN=bench, OU=bench, O=bench, L=bench, S=bench, C=US"
else
    echo "[2/6] Keystore exists — skipping."
fi

# ── 3. LSPatch ────────────────────────────────────────────────────────────────
echo "[3/6] LSPatch embedding module into VRChat APK..."
mkdir -p "$OUT_DIR"
java -jar "$LSPATCH_JAR" \
    "$VRCHAT_APK" \
    -m "$MODULE_APK" \
    -l 2 \
    --force-debuggable \
    -o "$OUT_DIR"

PATCHED_APK="$OUT_DIR/$(basename "${VRCHAT_APK%.apk}")-lspatched.apk"
[[ -f "$PATCHED_APK" ]] || { echo "LSPatch output not found at $PATCHED_APK"; exit 1; }
echo "    Patched: $PATCHED_APK"

# ── 4. Align + re-sign ────────────────────────────────────────────────────────
echo "[4/6] zipalign + apksigner..."
ALIGNED="$OUT_DIR/darkmatter_aligned.apk"
"$BUILD_TOOLS/zipalign" -f 4 "$PATCHED_APK" "$ALIGNED"
"$BUILD_TOOLS/apksigner" sign \
    --ks "$KEYSTORE" \
    --ks-alias bench \
    --ks-pass pass:benchkey \
    --key-pass pass:benchkey \
    "$ALIGNED"
echo "    Signed: $ALIGNED"

# ── 5. Install ────────────────────────────────────────────────────────────────
echo "[5/6] ADB install..."
adb devices
adb install -r --no-incremental "$ALIGNED"

# ── 6. Grant permissions ──────────────────────────────────────────────────────
echo "[6/6] Granting permissions..."
adb shell appops set "$PACKAGE" SYSTEM_ALERT_WINDOW allow
adb shell pm grant "$PACKAGE" android.permission.FOREGROUND_SERVICE           2>/dev/null || true
adb shell pm grant "$PACKAGE" android.permission.READ_MEDIA_AUDIO             2>/dev/null || true
adb shell pm grant "$PACKAGE" android.permission.READ_EXTERNAL_STORAGE        2>/dev/null || true

echo ""
echo "═══════════════════════════════════════════════════════"
echo "  DONE — launch VRChat, right thumbstick click = menu"
echo "  absolute ollie, built by olivia"
echo "  https://discord.gg/ojailbreaks"
echo "═══════════════════════════════════════════════════════"
echo ""
echo "Logcat:"
echo "  adb logcat -s VRCMenu/Main VRCMenu/IL2 VRCMenu/Gun VRCMenu/Sound VRCMenu/Move VRCMenu/Render vrc-quest"
