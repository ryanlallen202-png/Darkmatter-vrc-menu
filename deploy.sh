#!/usr/bin/env bash
# deploy.sh — full pipeline: build module → patch VRChat APK → install
# Usage: ./deploy.sh [path/to/VRChat_2026_4_2-1948-2b94e366a5-Release.apk]
# Requires: Android SDK, NDK r26c, LSPatch 0.6+, ADB, Java 17+

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VRCHAT_APK="${1:-$SCRIPT_DIR/../VRChat_2026_4_2-1948-2b94e366a5-Release.apk}"
MODULE_APK="$SCRIPT_DIR/app/build/outputs/apk/release/app-release.apk"
LSPATCH_JAR="${LSPATCH_JAR:-$HOME/tools/lspatch.jar}"
OUT_DIR="$SCRIPT_DIR/out"
KEYSTORE="$SCRIPT_DIR/bench.keystore"
PACKAGE="com.vrchat.oculus.quest"

echo "═══════════════════════════════════════════"
echo " VRCQuestMenu deploy — absolute ollie"
echo "═══════════════════════════════════════════"

# ── 1. Build module APK ───────────────────────────────────────────────────
echo "[1/5] Building module APK..."
cd "$SCRIPT_DIR"
./gradlew assembleRelease --quiet
echo "    Module: $MODULE_APK"

# ── 2. Generate keystore if missing ──────────────────────────────────────
if [ ! -f "$KEYSTORE" ]; then
    echo "[2/5] Generating keystore..."
    keytool -genkeypair -v \
        -keystore "$KEYSTORE" \
        -alias bench \
        -keyalg RSA -keysize 2048 -validity 9999 \
        -storepass benchkey -keypass benchkey \
        -dname "CN=bench, OU=bench, O=bench, L=bench, S=bench, C=US"
else
    echo "[2/5] Keystore exists — skipping."
fi

# ── 3. LSPatch — embed module into VRChat APK (rootless, no manager) ─────
echo "[3/5] LSPatch patching VRChat APK..."
mkdir -p "$OUT_DIR"
java -jar "$LSPATCH_JAR" \
    "$VRCHAT_APK" \
    -m "$MODULE_APK" \
    -l 2 \
    --force-debuggable \
    -o "$OUT_DIR"
# LSPatch output naming convention:
PATCHED_APK="$OUT_DIR/$(basename "${VRCHAT_APK%.apk}")-lspatched.apk"
echo "    Patched: $PATCHED_APK"

# ── 4. Re-sign (LSPatch auto-signs; this ensures our keystore is used) ───
echo "[4/5] Re-signing with bench keystore..."
ALIGNED="$OUT_DIR/vrchat_menu_aligned.apk"
zipalign -f 4 "$PATCHED_APK" "$ALIGNED"
apksigner sign \
    --ks "$KEYSTORE" \
    --ks-alias bench \
    --ks-pass pass:benchkey \
    --key-pass pass:benchkey \
    "$ALIGNED"
echo "    Signed: $ALIGNED"

# ── 5. Install + grant permissions ───────────────────────────────────────
echo "[5/5] Installing to Quest..."
adb devices
adb install --no-incremental "$ALIGNED"

echo ""
echo "Granting SYSTEM_ALERT_WINDOW (overlay — no root needed)..."
adb shell appops set "$PACKAGE" SYSTEM_ALERT_WINDOW allow

echo "Granting FOREGROUND_SERVICE..."
adb shell pm grant "$PACKAGE" android.permission.FOREGROUND_SERVICE 2>/dev/null || true

echo "Granting READ_MEDIA_AUDIO..."
adb shell pm grant "$PACKAGE" android.permission.READ_MEDIA_AUDIO 2>/dev/null || true

echo ""
echo "═══════════════════════════════════════════"
echo " DONE — launch VRChat, right thumbstick"
echo " click to toggle menu. absolute ollie."
echo " https://discord.gg/ojailbreaks"
echo "═══════════════════════════════════════════"
echo ""
echo "Logcat filter:"
echo "  adb logcat -s VRCMenu/Main VRCMenu/IL2 VRCMenu/Gun VRCMenu/Sound VRCMenu/Move VRCMenu/Render"
