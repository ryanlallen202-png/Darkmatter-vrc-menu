// movement.cpp — VRCPlayer loco speed hook via ShadowHook
// Hooks VRCPlayer.Method_Private_Void_Player_PDM_0 (or equivalent loco update)
// and scales the computed move vector before Unity physics sees it.
// Falls back to SetVelocity patch if method not found.

#include "movement.h"
#include "il2cpp_init.h"
#include "offsets.h"
#include "shadowhook.h"
#include <android/log.h>
#include <atomic>
#include <cstring>

#define TAG  "VRCMenu/Move"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace Movement
{
    static std::atomic<float> g_speed{1.0f};
    static std::atomic<bool>  g_active{false};
    static void*              g_hook    = nullptr;

    // ── Hook target: VRCMotionState.UpdateLocomotion or similar ──────────
    // Method signature: void(VRCPlayer* self, float dt)  — varies by build.
    // We hook it and multiply the velocity field written inside.
    // VRCPlayer.velocity field: standard VRCPlayerApi layout has a
    // Rigidbody reference at a known offset; simpler to intercept
    // the locomotion float input (moveForward / moveRight axes).

    // For 2026.4.2 we hook VRCPlayer.Method_Private_Void_0 which is the
    // per-frame loco update. Identified by: contains call to
    // CharacterController.Move and reads controller axis.
    // We trampoline: original runs first, then we scale the rb velocity.

    using LocoFn = void(*)(void* self);
    static LocoFn g_origLoco = nullptr;

    static void LocoHook(void* self)
    {
        if (g_origLoco) g_origLoco(self);

        float spd = g_speed.load();
        if (spd == 1.0f || !il2cpp::Ready()) return;

        // VRCPlayer at +0x28 holds a CharacterController* on 2026 builds;
        // we scale via VRCPlayerApi.SetVelocity instead, which is safer
        // across updates and doesn't require a struct offset.
        // Resolve SetVelocity once and invoke on self.
        static void* s_setVel = nullptr;
        if (!s_setVel)
            s_setVel = il2cpp::ResolveMethod(
                VRC_ASSEMBLY, VRC_NS_PLAYER, VRC_CLASS_PLAYER,
                "Method_Public_Void_Vector3_0", 1);  // SetVelocity analog
        if (!s_setVel)
            s_setVel = il2cpp::ResolveMethod(
                VRC_ASSEMBLY, VRC_NS_API, VRC_CLASS_PLAYERAPI,
                "SetVelocity", 1);

        if (!s_setVel || !il2cpp::runtime_invoke) return;

        // Get current velocity from VRCPlayerApi on self
        static void* s_getVel = nullptr;
        if (!s_getVel)
            s_getVel = il2cpp::ResolveMethod(
                VRC_ASSEMBLY, VRC_NS_API, VRC_CLASS_PLAYERAPI,
                "GetVelocity", 0);

        if (!s_getVel) return;

        void* exc   = nullptr;
        void* velObj = il2cpp::runtime_invoke(s_getVel, self, nullptr, &exc);
        if (!velObj || exc) return;

        // Vec3 unbox: Il2CppObject header = 2×ptr, then float[3]
        float* v = reinterpret_cast<float*>(
            reinterpret_cast<uint8_t*>(velObj) + sizeof(void*) * 2);

        float scaled[3] = { v[0] * spd, v[1], v[2] * spd };
        void* args[1] = { scaled };
        il2cpp::runtime_invoke(s_setVel, self, args, &exc);
    }

    void ApplyHook()
    {
        if (g_active.load()) return;
        if (!il2cpp::Ready()) { LOGE("IL2 not ready"); return; }

        // Try several known method name variants across VRC builds
        const char* methods[] = {
            "Method_Private_Void_0",
            "Method_Private_Void_Player_1",
            "UpdateLocomotion",
            nullptr
        };

        void* target = nullptr;
        for (int i = 0; methods[i] && !target; i++)
            target = il2cpp::ResolveMethod(
                VRC_ASSEMBLY, VRC_NS_PLAYER, VRC_CLASS_PLAYER, methods[i], -1);

        if (!target)
            target = il2cpp::ResolveMethod(
                VRC_ASSEMBLY, VRC_NS_API, VRC_CLASS_PLAYERAPI,
                "Method_Private_Void_0", 0);

        if (!target) { LOGE("Loco method not found"); return; }

        g_hook = shadowhook_hook_func_addr(
            target, (void*)LocoHook, (void**)&g_origLoco);

        if (g_hook) {
            g_active.store(true);
            LOGI("Movement hook applied @ %p", target);
        } else {
            LOGE("shadowhook failed: %s",
                 shadowhook_to_errmsg(shadowhook_get_errno()));
        }
    }

    void Unhook()
    {
        if (!g_active.load() || !g_hook) return;
        shadowhook_unhook(g_hook);
        g_hook = nullptr;
        g_active.store(false);
        LOGI("Movement hook removed");
    }

    bool  IsActive() { return g_active.load(); }
    float GetSpeed() { return g_speed.load(); }

    void SetSpeed(float s)
    {
        if (s < 0.1f) s = 0.1f;
        if (s > 20.f) s = 20.f;
        g_speed.store(s);
        // Auto-apply hook when speed changes away from 1×
        if (s != 1.0f && !g_active.load()) ApplyHook();
        if (s == 1.0f && g_active.load())  Unhook();
    }
}
