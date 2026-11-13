#!/usr/bin/env bash
# rename_patched.sh
# Usage: ./scripts/rename_patched.sh <input_patched.apk> [keystore.jks] [alias] [password]
#
# Renames the patched VRChat APK:
#   package  → com.darkmatter.vrchat
#   app name → "Dark Matter VRChat"
# Then re-signs and outputs DarkMatterVRChat-signed.apk
#
# Deps: apktool, zipalign, apksigner, keytool (all in $PATH / Android build-tools)

set -euo pipefail

INPUT="${1:-}"
KS="${2:-}"
ALIAS="${3:-darkmatter}"
PASS="${4:-android}"

NEW_PKG="com.darkmatter.vrchat"
NEW_LABEL="Dark Matter VRChat"
OUT="DarkMatterVRChat-signed.apk"
DECODED="patched-decoded"
UNSIGNED="darkmatter-vrchat-unsigned.apk"
ALIGNED="darkmatter-vrchat-aligned.apk"
KEYSTORE="debug.keystore"

if [ -z "$INPUT" ] || [ ! -f "$INPUT" ]; then
    echo "Usage: $0 <patched_input.apk> [keystore] [alias] [password]"
    exit 1
fi

echo "── Decompiling $INPUT ──────────────────────────────────────────────────"
apktool d "$INPUT" -o "$DECODED" --no-src -f

MANIFEST="$DECODED/AndroidManifest.xml"

ORIG_PKG=$(grep -oP 'package="\K[^"]+' "$MANIFEST" | head -1)
echo "   Original package : $ORIG_PKG"
echo "   New package      : $NEW_PKG"

# ── Rewrite package + authorities ─────────────────────────────────────────────
sed -i "s|package=\"${ORIG_PKG}\"|package=\"${NEW_PKG}\"|g" "$MANIFEST"
find "$DECODED" -name "*.xml" -exec sed -i "s|${ORIG_PKG}|${NEW_PKG}|g" {} +

# ── Rewrite app label ──────────────────────────────────────────────────────────
# Try strings.xml first
STRINGS="$DECODED/res/values/strings.xml"
if [ -f "$STRINGS" ]; then
    sed -i "s|<string name=\"app_name\">.*</string>|<string name=\"app_name\">${NEW_LABEL}</string>|g" \
        "$STRINGS"
    echo "   Patched strings.xml app_name → $NEW_LABEL"
fi
# Also stamp directly on manifest label attribute
sed -i "s|android:label=\"[^\"]*\"|android:label=\"${NEW_LABEL}\"|g" "$MANIFEST"

echo "── Rebuilding APK ───────────────────────────────────────────────────────"
apktool b "$DECODED" -o "$UNSIGNED"

# ── Align ─────────────────────────────────────────────────────────────────────
echo "── Aligning ─────────────────────────────────────────────────────────────"
zipalign -v 4 "$UNSIGNED" "$ALIGNED"

# ── Sign ──────────────────────────────────────────────────────────────────────
if [ -n "$KS" ] && [ -f "$KS" ]; then
    echo "── Signing with provided keystore $KS ───────────────────────────────"
    apksigner sign \
        --ks "$KS" \
        --ks-key-alias "$ALIAS" \
        --ks-pass "pass:${PASS}" \
        --key-pass "pass:${PASS}" \
        --out "$OUT" \
        "$ALIGNED"
else
    echo "── Generating debug keystore ─────────────────────────────────────────"
    keytool -genkeypair -v \
        -keystore "$KEYSTORE" \
        -alias darkmatter \
        -keyalg RSA -keysize 2048 -validity 9999 \
        -storepass android -keypass android \
        -dname "CN=Dark Matter, OU=Dev, O=Darkmatter, L=Unknown, ST=Unknown, C=US" \
        2>/dev/null || true

    apksigner sign \
        --ks "$KEYSTORE" \
        --ks-key-alias darkmatter \
        --ks-pass pass:android \
        --key-pass pass:android \
        --out "$OUT" \
        "$ALIGNED"
fi

echo "── Done ─────────────────────────────────────────────────────────────────"
echo "   Output: $OUT"
echo "   Package: $NEW_PKG"
echo "   Label  : $NEW_LABEL"

# Cleanup
rm -rf "$DECODED" "$UNSIGNED" "$ALIGNED"
