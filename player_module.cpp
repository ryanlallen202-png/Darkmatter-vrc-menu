// player_module.cpp — nameplate + invisibility
// Nameplate: invokes VRCPlayer.Method_Public_Void_Boolean_0 (SetNamePlateVisible)
//   or walks the VRCPlayerNamePlate GO and calls SetActive(false).
// Invisibility: disables all SkinnedMeshRenderer components on local avatar GO.

#include "player_module.h"
#include "il2cpp_init.h"
#include "offsets.h"
#include <android/log.h>
#include <atomic>

#define TAG  "VRCMenu/Player"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace Player
{
    static std::atomic<bool> g_nameplateHidden{false};
    static std::atomic<bool> g_invisible{false};

    // ── Cached method ptrs ────────────────────────────────────────────────
    static void* s_SetNameplate  = nullptr;
    static void* s_GetLocalPlayer= nullptr;
    static void* s_GetRenderers  = nullptr;
    static void* s_SetEnabled    = nullptr;
    static void* s_GetAudioSrc   = nullptr;

    static bool EnsureResolved()
    {
        if (!il2cpp::Ready()) return false;
        if (s_SetNameplate) return true;

        // VRCPlayer.Method_Public_Void_Boolean_0 is the common generated name
        // for SetNamePlateVisible(bool) in IL2CPP obfuscated builds.
        s_SetNameplate = il2cpp::ResolveMethod(
            VRC_ASSEMBLY, VRC_NS_PLAYER, VRC_CLASS_PLAYER,
            "Method_Public_Void_Boolean_0", 1);
        if (!s_SetNameplate)
            s_SetNameplate = il2cpp::ResolveMethod(
                VRC_ASSEMBLY, VRC_NS_PLAYER, VRC_CLASS_PLAYER,
                "SetNamePlateVisible", 1);

        s_GetLocalPlayer = il2cpp::ResolveMethod(
            VRC_ASSEMBLY, VRC_NS_PLAYER, "VRCPlayer",
            "get_prop_Player_0", 0);  // static local player getter
        if (!s_GetLocalPlayer)
            s_GetLocalPlayer = il2cpp::ResolveMethod(
                VRC_ASSEMBLY, "", "Networking",
                "get_LocalPlayer", 0);

        // SkinnedMeshRenderer.set_enabled
        s_SetEnabled = il2cpp::ResolveMethod(
            UNITY_ASSEMBLY, UNITY_NS, UNITY_CLASS_SMR,
            "set_enabled", 1);
        if (!s_SetEnabled)
            s_SetEnabled = il2cpp::ResolveMethod(
                UNITY_ASSEMBLY, UNITY_NS, UNITY_CLASS_RENDERER,
                "set_enabled", 1);

        // GetComponentsInChildren<SkinnedMeshRenderer> on local avatar GO
        s_GetRenderers = il2cpp::ResolveMethod(
            UNITY_ASSEMBLY, UNITY_NS, UNITY_CLASS_GO,
            "GetComponentsInChildren", 1);

        // AudioSource for soundboard
        s_GetAudioSrc = il2cpp::ResolveMethod(
            UNITY_ASSEMBLY, UNITY_NS, UNITY_CLASS_GO,
            "GetComponent", 1);

        LOGI("Player module resolved: nameplate=%p renderers=%p",
             s_SetNameplate, s_GetRenderers);
        return s_SetNameplate != nullptr;
    }

    static void* GetLocalGO()
    {
        if (!s_GetLocalPlayer || !il2cpp::runtime_invoke) return nullptr;
        void* exc = nullptr;
        void* lp = il2cpp::runtime_invoke(s_GetLocalPlayer, nullptr, nullptr, &exc);
        if (!lp || exc) return nullptr;
        // VRCPlayerApi.gameObject — field at Unity Object offset: klass(8)+monitor(8)+m_cachedPtr(8)
        // The GO ptr is at +0x10 for a UnityEngine.Component (VRCPlayerApi is a Behaviour)
        return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(lp) + 0x10);
    }

    // ── Nameplate ─────────────────────────────────────────────────────────
    bool NameplateHidden() { return g_nameplateHidden.load(); }

    void SetNameplate(bool hide)
    {
        if (!EnsureResolved()) return;

        g_nameplateHidden.store(hide);

        // Walk all VRCPlayers and toggle their nameplate visibility
        // For local player: invert — if hide=true, SetNamePlateVisible(false)
        // Simple path: iterate g_players from features.cpp; we avoid the
        // dependency by calling directly on the local player GO.
        void* lp = GetLocalGO();
        if (!lp || !s_SetNameplate) return;

        void* exc = nullptr;
        bool  vis = !hide;
        void* args[1] = { &vis };
        il2cpp::runtime_invoke(s_SetNameplate, lp, args, &exc);
        LOGI("Nameplate %s", hide ? "hidden" : "visible");
    }

    // ── Invisibility ──────────────────────────────────────────────────────
    bool IsInvisible() { return g_invisible.load(); }

    void SetInvisible(bool invis)
    {
        if (!EnsureResolved()) return;
        g_invisible.store(invis);

        void* go = GetLocalGO();
        if (!go || !s_GetRenderers || !s_SetEnabled) return;

        // GetComponentsInChildren<SkinnedMeshRenderer>(includeInactive=true)
        // We need the SMR class ptr for the type arg
        static void* s_smr_class = nullptr;
        if (!s_smr_class)
        {
            void* asmb = il2cpp::domain_assembly_open(il2cpp::g_domain, UNITY_ASSEMBLY);
            void* img  = il2cpp::assembly_get_image(asmb);
            s_smr_class = il2cpp::class_from_name(img, UNITY_NS, UNITY_CLASS_SMR);
        }
        if (!s_smr_class || !il2cpp::class_get_type) return;
        void* smr_type = il2cpp::class_get_type(s_smr_class);
        if (!smr_type) return;

        void* exc = nullptr;
        bool  incl = true;
        void* gc_args[2] = { smr_type, &incl };
        // GetComponentsInChildren(Type, bool) — 2 args
        static void* s_getCompInChildren = nullptr;
        if (!s_getCompInChildren)
            s_getCompInChildren = il2cpp::ResolveMethod(
                UNITY_ASSEMBLY, UNITY_NS, UNITY_CLASS_GO,
                "GetComponentsInChildren", 2);
        if (!s_getCompInChildren) return;

        void* arr = il2cpp::runtime_invoke(s_getCompInChildren, go, gc_args, &exc);
        if (!arr || exc) return;

        // Il2CppArray: max_length @ 0x18, data @ 0x20
        int32_t len = *reinterpret_cast<int32_t*>(
            reinterpret_cast<uint8_t*>(arr) + 0x18);
        void** items = reinterpret_cast<void**>(
            reinterpret_cast<uint8_t*>(arr) + 0x20);

        bool enabled = !invis;
        for (int32_t i = 0; i < len; i++)
        {
            if (!items[i]) continue;
            void* args[1] = { &enabled };
            il2cpp::runtime_invoke(s_SetEnabled, items[i], args, &exc);
        }
        LOGI("Invisibility %s (%d renderers)", invis ? "ON" : "OFF", len);
    }

    // ── AudioSource for Soundboard ────────────────────────────────────────
    void* GetLocalAudioSource()
    {
        void* go = GetLocalGO();
        if (!go || !s_GetAudioSrc) return nullptr;

        static void* s_as_class = nullptr;
        if (!s_as_class)
        {
            void* asmb = il2cpp::domain_assembly_open(il2cpp::g_domain, UNITY_ASSEMBLY);
            void* img  = il2cpp::assembly_get_image(asmb);
            s_as_class = il2cpp::class_from_name(img, UNITY_NS, "AudioSource");
        }
        if (!s_as_class || !il2cpp::class_get_type) return nullptr;

        void* as_type = il2cpp::class_get_type(s_as_class);
        void* exc  = nullptr;
        void* args[1] = { as_type };
        return il2cpp::runtime_invoke(s_GetAudioSrc, go, args, &exc);
    }
}
