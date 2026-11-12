#!/usr/bin/env bash
# push_fixes.sh — commit fixed workflow + CMakeLists to Darkmatter-vrc-menu
# Usage: bash push_fixes.sh
# Expects: build.yml  patch.yml  patch.py  CMakeLists.txt  in $PWD
# Built by Olivia — https://discord.gg/ojailbreaks

set -euo pipefail

OWNER="ryanlallen202-png"
REPO="Darkmatter-vrc-menu"
TOKEN="${GITHUB_TOKEN:-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}"
BRANCH="main"
API="https://api.github.com/repos/${OWNER}/${REPO}"

b64() { base64 -w0 "$1"; }

push_file() {
  local DEST_PATH="$1"
  local SRC_FILE="$2"
  local MSG="$3"

  echo "→ pushing ${DEST_PATH}"

  # get current sha if file exists (needed for update)
  local SHA=""
  SHA=$(curl -sf -H "Authorization: Bearer ${TOKEN}" \
    "${API}/contents/${DEST_PATH}?ref=${BRANCH}" \
    | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('sha',''))" 2>/dev/null || true)

  local PAYLOAD
  if [ -n "$SHA" ]; then
    PAYLOAD=$(python3 -c "
import json, sys
print(json.dumps({
  'message': sys.argv[1],
  'content': sys.argv[2],
  'sha':     sys.argv[3],
  'branch':  sys.argv[4],
}))" "$MSG" "$(b64 "$SRC_FILE")" "$SHA" "$BRANCH")
  else
    PAYLOAD=$(python3 -c "
import json, sys
print(json.dumps({
  'message': sys.argv[1],
  'content': sys.argv[2],
  'branch':  sys.argv[3],
}))" "$MSG" "$(b64 "$SRC_FILE")" "$BRANCH")
  fi

  local HTTP
  HTTP=$(curl -s -o /tmp/gh_resp.json -w "%{http_code}" \
    -X PUT \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Content-Type: application/json" \
    "${API}/contents/${DEST_PATH}" \
    -d "$PAYLOAD")

  if [[ "$HTTP" == "200" || "$HTTP" == "201" ]]; then
    echo "  ✓ ${DEST_PATH} (HTTP ${HTTP})"
  else
    echo "  ✗ ${DEST_PATH} (HTTP ${HTTP})"
    cat /tmp/gh_resp.json
    exit 1
  fi
}

push_file ".github/workflows/build.yml" "build.yml" \
  "fix: resolve ShadowHook 404 — dynamic tag + AAR extraction"

push_file ".github/workflows/patch.yml" "patch.yml" \
  "fix: resolve Frida gadget 404 — dynamic version resolution"

push_file "patch.py" "patch.py" \
  "fix: patch.py — live Frida tag resolution, no hardcoded URL"

push_file "CMakeLists.txt" "CMakeLists.txt" \
  "fix: CMake guard on shadowhook include path, no more non-existent-path error"

echo ""
echo "All files pushed. Trigger a new Actions run to verify."
