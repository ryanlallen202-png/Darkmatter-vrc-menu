#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// VRChat 2026.4.2-1948-2b94e366a5-Release  |  com.vrchat.oculus.quest
// Unity 6000.0.67f1-DWR  |  arm64-v8a  |  libil2cpp.so SHA: build-verified
// All offsets extracted from APK at /lib/arm64-v8a/libil2cpp.so
// ═══════════════════════════════════════════════════════════════════════════

// ── IL2CPP exported symbol names (obfuscated, resolved from .dynsym) ────────
// These are the REAL export names after VRChat's il2cpp-obfuscator pass.
// Resolved by signature: ADRP+LDR64+RET → static global getter = domain_get.
// Three candidates — runtime probe in il2cpp_init.cpp identifies the live one.

#define IL2CPP_SYM_DOMAIN_GET_A     "dZD_mYCnXmC"   // ADRP+LDR+RET @ 0x69c7a1c
#define IL2CPP_SYM_DOMAIN_GET_B     "datjjqSnczE"   // ADRP+LDR+RET @ 0x69c7a28
#define IL2CPP_SYM_DOMAIN_GET_C     "YnxEfCOIeEB"   // ADRP+LDR+RET @ 0x69c83d4

// Known stable exports (not obfuscated — libc forwards)
#define IL2CPP_SYM_FREE             "il2cpp_free"
#define IL2CPP_SYM_GC_HANDLE        "il2cpp_gchandle_get_target"
#define IL2CPP_SYM_NATIVE_STACK     "il2cpp_native_stack_trace"

// ── Metadata encryption ───────────────────────────────────────────────────
// global-metadata.dat first 4 bytes: 0xef953434 (NOT standard 0xFAB11BAF)
// → XOR-encrypted with per-build key stored in .data segment of libil2cpp.so
// Bypass: hook the metadata reader at JNI_OnLoad + dump post-decrypt via
//         frida-style gadget OR use the metadata_key_offset below.
// Key location: .data section @ file offset 0x12ca6fa0+0x468 (found by
//               searching for the XOR loop referencing both the metadata
//               ptr and a 4-byte constant near the LOAD segment boundary)
#define METADATA_XOR_KEY_RVA        0x12cae408ULL   // RVA in loaded image

// ── JNI_OnLoad hook offset ────────────────────────────────────────────────
// libil2cpp.so JNI_OnLoad is the entry point — hook here to intercept
// il2cpp_init before metadata is loaded, allowing dump of decrypted metadata
// Exported symbol "JNI_OnLoad" is present and not obfuscated
#define JNI_ONLOAD_SYM              "JNI_OnLoad"

// ── Photon ────────────────────────────────────────────────────────────────
// Photon lives in libil2cpp.so as managed→native compiled code.
// RaiseEvent is identified by the string ref "Photon_Injected" at 0x259990c.
// The function that owns that string pointer is located by scanning .rodata
// reference → function containing the ADRP that loads 0x259990c.
// Runtime: resolve at il2cpp boot via class/method walk once metadata loads.
// Assembly: "PhotonClient.dll" → class "ExitGames.Client.Photon.LoadBalancingClient"
#define PHOTON_ASSEMBLY             "PhotonClient.dll"
#define PHOTON_NS                   "ExitGames.Client.Photon"
#define PHOTON_CLASS_LBC            "LoadBalancingClient"
#define PHOTON_METHOD_RAISE         "OpRaiseEvent"
#define PHOTON_METHOD_SETPROPS      "OpSetPropertiesOfActor"
#define PHOTON_METHOD_CLOSE         "OpCloseConnection"

// VRChat game assembly
#define VRC_ASSEMBLY                "Assembly-CSharp.dll"
#define VRC_NS_PLAYER               "VRC"
#define VRC_CLASS_PLAYER            "VRCPlayer"
#define VRC_NS_API                  "VRC.SDKBase"
#define VRC_CLASS_PLAYERAPI         "VRCPlayerApi"

// ── Unity engine ──────────────────────────────────────────────────────────
#define UNITY_ASSEMBLY              "UnityEngine.CoreModule.dll"
#define UNITY_NS                    "UnityEngine"
#define UNITY_CLASS_GO              "GameObject"
#define UNITY_CLASS_RENDERER        "Renderer"
#define UNITY_CLASS_SMR             "SkinnedMeshRenderer"

// ── Anti-tamper bypass flags ──────────────────────────────────────────────
// VRChat 2026 uses 3 anti-tamper layers:
//   1. Encrypted global-metadata.dat (XOR key in .data) → bypassed at load
//   2. Obfuscated IL2CPP export names → bypassed via signature scan
//   3. Certificate/signature check in Java (VRCApplication.onCreate) → bypassed
//      by LSPatch which re-signs and patches the signature validator
// No additional runtime integrity check detected in libil2cpp.so strings.
// No Denuvo, no EAC, no BattlEye detected in lib listing.
