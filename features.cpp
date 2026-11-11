#include "shared.hpp"
#include <cstdlib>

// ============================================================================
// VRChat class cache — resolved once, null-checked at every use
// ============================================================================
static struct {
    bool ready = false;
    Il2CppClass *PlayerApi, *Networking, *RenderSettings,
                *Camera, *GameObject, *Transform, *Rigidbody,
                *Material, *Renderer, *Shader,
                *Input, *Object, *Physics, *RaycastHit;
    MethodInfo  *PlayerGetPos, *PlayerSetPos, *PlayerGetVel, *PlayerSetVel,
                *PlayerGetName, *PlayerIsLocal, *PlayerGetId,
                *PlayerSetGrav, *PlayerGetGrav,
                *PlayerGetGrounded, *PlayerTeleport,
                *PlayerGetAll,
                *NetLocalPlayer,
                *CamGetMain,
                *TrGetPos, *TrSetPos, *TrGetRot, *TrSetRot,
                *TrGetFwd, *TrGetRight, *TrGetUp, *TrGetLS, *TrSetLS,
                *GoGetTr, *GoGetComp, *GoSetActive,
                *RendSetEnabled,
                *MatSetColor,
                *RsGetSkybox, *RsSetSkybox,
                *InputGetAxis, *InputGetButton, *InputGetButtonDown,
                *ObjFindTypeAll,
                *PhysRaycast;
} G;

// ---- Reflected field access -----------------------------------------------
// Resolving a field through il2cpp_field_get_offset keeps us off hardcoded
// constants. If a build shifts, the offset follows the dump.
static struct {
    Il2CppClass *VrcPlayer = nullptr;
    int isLocalOff  = 0;
    int descOff     = 0;
} F;

static bool InitFields() {
    if (F.VrcPlayer) return true;
    Il2CppClass *vp = IL2.GetClass("", "VRCPlayer", "Assembly-CSharp");
    if (!vp) { LOGE("VRCPlayer class not found"); return false; }
    F.VrcPlayer = vp;

    auto probe = [&](const char* name, int& out) {
        int off = IL2.FieldOffset(vp, name);
        if (off < 0) return false;
        out = off;
        LOGI("VRCPlayer.%s @ 0x%x", name, off);
        return true;
    };

    const char* localNames[] = {"isLocal","isLocalPlayer","prop_Boolean_0",nullptr};
    F.isLocalOff = -1;
    for (int i=0; localNames[i] && F.isLocalOff < 0; i++) probe(localNames[i], F.isLocalOff);
    if (F.isLocalOff < 0)
        LOGE("VRCPlayer.isLocal unresolved — speed hook refuses to scale");

    const char* descNames[] = {"Descriptor","prop_VRCPlayerDescriptor_0",nullptr};
    F.descOff = -1;
    for (int i=0; descNames[i] && F.descOff < 0; i++) probe(descNames[i], F.descOff);
    return true;
}

static bool InitG() {
    if (G.ready) return true;
    if (!IL2.ready) return false;
#define C(v,ns,n,a)  G.v=IL2.GetClass(ns,n,a);
#define M(v,c,n,argc) G.v=IL2.GetMethod(G.c,n,argc);
    C(PlayerApi,     "",             "VRCPlayerApi",    "Assembly-CSharp")
    C(Networking,    "",             "Networking",      "Assembly-CSharp")
    C(Camera,        "UnityEngine",  "Camera",          "UnityEngine")
    C(GameObject,    "UnityEngine",  "GameObject",      "UnityEngine")
    C(Transform,     "UnityEngine",  "Transform",       "UnityEngine")
    C(Rigidbody,     "UnityEngine",  "Rigidbody",       "UnityEngine")
    C(Material,      "UnityEngine",  "Material",        "UnityEngine")
    C(Renderer,      "UnityEngine",  "Renderer",        "UnityEngine")
    C(Shader,        "UnityEngine",  "Shader",          "UnityEngine")
    C(Object,        "UnityEngine",  "Object",          "UnityEngine")
    C(Input,         "UnityEngine",  "Input",           "UnityEngine")
    C(RenderSettings,"UnityEngine",  "RenderSettings",  "UnityEngine")
    C(Physics,       "UnityEngine",  "Physics",         "UnityEngine")

    M(PlayerGetPos,    PlayerApi, "get_position",        0)
    M(PlayerSetPos,    PlayerApi, "TeleportTo",          1)
    M(PlayerGetVel,    PlayerApi, "GetVelocity",         0)
    M(PlayerSetVel,    PlayerApi, "SetVelocity",         1)
    M(PlayerGetName,   PlayerApi, "get_displayName",     0)
    M(PlayerIsLocal,   PlayerApi, "get_isLocal",         0)
    M(PlayerGetId,     PlayerApi, "get_playerId",        0)
    M(PlayerSetGrav,   PlayerApi, "SetGravityStrength",  1)
    M(PlayerGetGrav,   PlayerApi, "GetGravityStrength",  0)
    M(PlayerGetGrounded,PlayerApi,"IsPlayerGrounded",    0)
    M(PlayerTeleport,  PlayerApi, "TeleportTo",          1)
    M(PlayerGetAll,    PlayerApi, "GetAllPlayers",       0)
    M(NetLocalPlayer,  Networking,"get_LocalPlayer",     0)
    M(CamGetMain,      Camera,    "get_main",            0)
    M(TrGetPos,        Transform, "get_position",        0)
    M(TrSetPos,        Transform, "set_position",        1)
    M(TrGetRot,        Transform, "get_rotation",        0)
    M(TrSetRot,        Transform, "set_rotation",        1)
    M(TrGetFwd,        Transform, "get_forward",         0)
    M(TrGetRight,      Transform, "get_right",           0)
    M(TrGetUp,         Transform, "get_up",              0)
    M(TrGetLS,         Transform, "get_localScale",      0)
    M(TrSetLS,         Transform, "set_localScale",      1)
    M(GoGetTr,         GameObject,"get_transform",       0)
    M(GoGetComp,       GameObject,"GetComponent",        1)
    M(GoSetActive,     GameObject,"SetActive",           1)
    M(RendSetEnabled,  Renderer,  "set_enabled",         1)
    M(MatSetColor,     Material,  "set_color",           1)
    M(RsGetSkybox,     RenderSettings,"get_skybox",      0)
    M(RsSetSkybox,     RenderSettings,"set_skybox",      1)
    M(InputGetAxis,    Input,     "GetAxis",             1)
    M(InputGetButton,  Input,     "GetButton",           1)
    M(InputGetButtonDown,Input,   "GetButtonDown",       1)
    M(ObjFindTypeAll,  Object,    "FindObjectsOfType",   1)
    M(PhysRaycast,     Physics,   "Raycast",             2)
#undef C
#undef M

    bool havePlayer = G.PlayerApi && G.PlayerGetPos && G.PlayerGetVel;
    bool haveInput  = G.Input && G.InputGetButton;
    G.ready = havePlayer && haveInput;
    if (G.ready) InitFields();
    LOGI("VRC classes %s", G.ready?"ready":"INCOMPLETE");
    return G.ready;
}

// ============================================================================
// Input helpers
// ============================================================================
static bool Btn(const char *name) {
    if (!G.Input || !G.InputGetButton) return false;
    Il2CppString *s = IL2.StrNew(name);
    void *a[1]={s};
    Il2CppObject *r = IL2.Invoke(G.InputGetButton, nullptr, a);
    return r && IL2.Unbox<bool>(r);
}
static bool BtnDown(const char *name) {
    if (!G.Input || !G.InputGetButtonDown) return false;
    void *a[1]={IL2.StrNew(name)};
    Il2CppObject *r = IL2.Invoke(G.InputGetButtonDown, nullptr, a);
    return r && IL2.Unbox<bool>(r);
}
static float Axis(const char *name) {
    if (!G.Input || !G.InputGetAxis) return 0.f;
    void *a[1]={IL2.StrNew(name)};
    Il2CppObject *r = IL2.Invoke(G.InputGetAxis, nullptr, a);
    return r ? IL2.Unbox<float>(r) : 0.f;
}

// Quest controller mapping. XR input names first; legacy Unity names as fallback.
static bool LeftStickClick() {
    return Axis("Oculus_CrossPlatform_PrimaryThumbstickButton") > 0.5f
        || BtnDown("Fire3") || BtnDown("joystick button 8");
}
static bool BtnA()     { return Btn("Fire1") || Btn("joystick button 0"); }
static bool BtnB()     { return Btn("Fire2") || Btn("joystick button 1"); }
static bool BtnX()     { return Btn("Jump")  || Btn("joystick button 2"); }
static bool BtnY()     { return Btn("joystick button 3"); }
static bool RTrigger() { return Axis("Oculus_CrossPlatform_SecondaryIndexTrigger") > 0.7f; }
static bool LTrigger() { return Axis("Oculus_CrossPlatform_PrimaryIndexTrigger")   > 0.7f; }
static bool RGrip()    { return Axis("Oculus_CrossPlatform_SecondaryHandTrigger")  > 0.7f; }
static bool LGrip()    { return Axis("Oculus_CrossPlatform_PrimaryHandTrigger")    > 0.7f; }

// ============================================================================
// Player accessors
// ============================================================================
static Il2CppObject *LocalPlayer() {
    return G.NetLocalPlayer ? IL2.Invoke(G.NetLocalPlayer, nullptr) : nullptr;
}
static Vec3 GetPos(Il2CppObject *api) {
    if (!api || !G.PlayerGetPos) return {};
    return IL2.Unbox<Vec3>(IL2.Invoke(G.PlayerGetPos, api));
}
static Vec3 GetVel(Il2CppObject *api) {
    if (!api || !G.PlayerGetVel) return {};
    return IL2.Unbox<Vec3>(IL2.Invoke(G.PlayerGetVel, api));
}
static void SetVel(Il2CppObject *api, Vec3 v) {
    if (!api || !G.PlayerSetVel) return;
    void *a[1]={&v};
    IL2.Invoke(G.PlayerSetVel, api, a);
}
static void SetGrav(Il2CppObject *api, float g) {
    if (!api || !G.PlayerSetGrav) return;
    void *a[1]={&g};
    IL2.Invoke(G.PlayerSetGrav, api, a);
}
static Il2CppObject* Tr(Il2CppObject* obj) {
    if (!obj || !G.GoGetTr) return nullptr;
    return IL2.Invoke(G.GoGetTr, obj);
}
static void TeleportTo(Il2CppObject *api, Vec3 pos) {
    if (!api || !G.PlayerTeleport) return;
    void *a[1]={&pos};
    IL2.Invoke(G.PlayerTeleport, api, a);
}

// ============================================================================
// Player list — shared with ESP and menu
// ============================================================================
std::vector<PlayerInfo> g_players;
int g_localActor = 0;
static double g_nextRefresh = 0;

static void RefreshPlayers() {
    if (!G.PlayerGetAll || g_time < g_nextRefresh) return;
    g_nextRefresh = g_time + 0.5;
    Il2CppArray *arr = (Il2CppArray*)IL2.Invoke(G.PlayerGetAll, nullptr);
    if (!arr) return;
    size_t n = *(uintptr_t*)((uint8_t*)arr + kArrLengthAt);
    if (n > 80) n = 80;                        // hard bound: room never blows the vector
    Il2CppObject *local = LocalPlayer();
    Vec3 lp = local ? GetPos(local) : Vec3{};
    std::vector<PlayerInfo> fresh;
    fresh.reserve(n);
    for (size_t i=0;i<n;i++){
        Il2CppObject *api = ((Il2CppObject**)((uint8_t*)arr + kArrDataAt))[i];
        if (!api) continue;
        PlayerInfo p;
        p.api   = api;
        p.name  = IL2.StrGet((Il2CppString*)IL2.Invoke(G.PlayerGetName, api));
        p.pos   = GetPos(api);
        p.dist  = V3Len(p.pos - lp);
        Il2CppObject *lo = IL2.Invoke(G.PlayerIsLocal, api);
        p.isLocal = lo && IL2.Unbox<bool>(lo);
        Il2CppObject *id = IL2.Invoke(G.PlayerGetId, api);
        p.playerId = id ? IL2.Unbox<int32_t>(id) : 0;
        p.actor = p.playerId;
        fresh.push_back(std::move(p));
    }
    g_players.swap(fresh);                     // atomic-enough swap; readers see whole list
}

// ============================================================================
// Feature flags (read by menu.cpp)
// ============================================================================
bool f_fly          = false;
bool f_velocityFly  = false;
bool f_speed        = false;
bool f_noclip       = false;
bool f_lowGravity   = false;
bool f_noGravity    = false;
bool f_platforms    = false;
bool f_punch        = false;
bool f_esp          = false;
bool f_skybox       = false;
bool f_skyRainbow   = false;
bool f_worldBlocks  = false;
bool f_blockRainbow = false;
bool f_portalGun    = false;
bool f_orbitCam     = false;
bool f_freeze       = false;
bool f_spin         = false;
bool f_bigHead      = false;
bool f_invisible    = false;
bool f_nameSpoof    = false;
bool f_antiEac      = true;
bool f_speedServer  = false;   // local scale only vs. networked

int  g_speedTier    = 1;
int  g_skyColorIdx  = 0;
int  g_blockColorIdx= 0;
float g_flySpeed    = 15.0f;
float g_flapMult    = 5.5f;

static const float kFlySpeed[]  = {4,8,16,24};
static const float kSpeedMult[] = {1.5f,2.0f,3.0f,5.0f,8.0f};

static Vec3   g_savedSpot{};
static bool   g_hasSavedSpot = false;
static double g_punchNext    = 0;
static double g_skyNext      = 0;
static bool   g_wasInvisible = false;
static bool   g_wasFrozen    = false;

// ============================================================================
// Skybox
// ============================================================================
static const Color kSkyColors[] = {
    {0.03f,0.03f,0.10f,1}, {0.10f,0.00f,0.20f,1},
    {0.60f,0.10f,0.05f,1}, {0.00f,0.25f,0.10f,1},
    {0.05f,0.15f,0.50f,1}, {0.80f,0.50f,0.10f,1},
    {0.00f,0.00f,0.00f,1}, {1.00f,1.00f,1.00f,1},
    {0.00f,0.80f,0.80f,1}, {0.90f,0.00f,0.90f,1},
};
static const char *kSkyNames[] = {"Midnight","Void Purple","Blood Red","Deep Forest","Ocean Night","Golden Hour","Black","White","Neon Cyan","Neon Pink"};
static constexpr int kSkyCount = 10;

static Ref g_skyMat;
static Ref g_skySaved;   // original RenderSettings.skybox

static void CacheSkyboxOnce() {
    if (g_skySaved || !G.RsGetSkybox) return;
    Il2CppObject *cur = IL2.Invoke(G.RsGetSkybox, nullptr);
    if (cur) g_skySaved.set(cur);
}
static void ApplySkybox(Color c) {
    if (!G.RsSetSkybox) return;
    CacheSkyboxOnce();
    if (!g_skyMat && G.Material && G.Shader) {
        // Shader.Find is static on Shader
        MethodInfo *find = IL2.GetMethod(G.Shader, "Find", 1);
        const char *tryShaders[] = {
            "Skybox/Panoramic","Unlit/Color","Universal Render Pipeline/Unlit", nullptr
        };
        Il2CppObject *sh = nullptr;
        for (int i=0; tryShaders[i] && !sh; i++) {
            void *a[1]={IL2.StrNew(tryShaders[i])};
            sh = IL2.Invoke(find, nullptr, a);
        }
        if (sh) {
            Il2CppObject *m = IL2.object_new(G.Material);
            MethodInfo *ctor = IL2.GetMethod(G.Material, ".ctor", 1);
            if (m && ctor) { void *a[1]={sh}; IL2.Invoke(ctor, m, a); }
            if (m) g_skyMat.set(m);
        }
    }
    if (!g_skyMat) return;
    if (G.MatSetColor) { void *a[1]={&c}; IL2.Invoke(G.MatSetColor, g_skyMat.get(), a); }
    Il2CppObject *m = g_skyMat.get();
    void *a[1]={m};
    IL2.Invoke(G.RsSetSkybox, nullptr, a);
}
static void RestoreSkybox() {
    if (!G.RsSetSkybox) return;
    Il2CppObject *orig = g_skySaved.get();
    void *a[1]={orig};
    IL2.Invoke(G.RsSetSkybox, nullptr, a);
}

static void SkyboxTick() {
    if ((!f_skybox && !f_skyRainbow) || g_time < g_skyNext) return;
    g_skyNext = g_time + 0.5;
    Color c = f_skyRainbow ? ColorRainbow((float)g_time*0.08f)
                           : kSkyColors[g_skyColorIdx];
    ApplySkybox(c);
}

// ============================================================================
// Integrity / anti-cheat surface
// ============================================================================
// Two layers: symbol-level neutralization of the Easy Anti-Cheat client entry
// points (EOSSDK), and address-level patches once you dump the offsets.
// Every entry is 0 until filled — a zero never patched, so a bad dump cannot
// corrupt an unrelated function.
namespace EACOff {
    static constexpr uintptr_t kBeginSession  = 0x0;
    static constexpr uintptr_t kAddNotify     = 0x0;
    static constexpr uintptr_t kValidateSend  = 0x0;
    static constexpr uintptr_t kIsIntegrity   = 0x0;
    static constexpr uintptr_t kIsModded      = 0x0;
}
void PatchEAC(uintptr_t base) {
    if (!f_antiEac) { LOGI("Anti-EAC off by config"); return; }
    if (EACOff::kBeginSession)  RetPatch (base+EACOff::kBeginSession);
    if (EACOff::kAddNotify)     RetPatch (base+EACOff::kAddNotify);
    if (EACOff::kValidateSend)  NopRange (base+EACOff::kValidateSend, 6);
    if (EACOff::kIsIntegrity)   RetTrue  (base+EACOff::kIsIntegrity);
    if (EACOff::kIsModded)      RetPatch (base+EACOff::kIsModded);

    const char *libs[] = {
        "libEOSSDK-Android-Shipping.so",
        "libEasyAntiCheat.so",
        "libanticheat.so",
        nullptr
    };
    const char *syms[] = {
        "EOS_AntiCheatClient_BeginSession",
        "EOS_AntiCheatClient_AddNotifyMessageToServer",
        "EOS_AntiCheatClient_AddNotifyPeerActionRequired",
        "EOS_AntiCheatClient_ReceiveMessageFromServer",
        "EOS_AntiCheatClient_PollStatus",
        "EOS_AntiCheatServer_BeginSession",
        nullptr
    };
    int patched = 0;
    for (int l = 0; libs[l]; l++) {
        void *eos = dlopen(libs[l], RTLD_NOW | RTLD_NOLOAD);
        if (!eos) continue;
        for (int i = 0; syms[i]; i++) {
            void *s = dlsym(eos, syms[i]);
            if (s) { RetPatch((uintptr_t)s); patched++; }
        }
        LOGI("EAC: %s symbols neutralized", libs[l]);
    }
    LOGI("EAC patches applied (%d symbols)", patched);
}

// ============================================================================
// Movement tick
// ============================================================================
static void MovementTick(Il2CppObject *local) {
    if (!local) return;

    // Fly: hold A, camera-forward at tier speed, gravity off while airborne
    if (f_fly && BtnA()) {
        Il2CppObject *cam = G.CamGetMain ? IL2.Invoke(G.CamGetMain, nullptr) : nullptr;
        if (cam) {
            Il2CppObject *tr = IL2.Invoke(G.GoGetTr, cam);
            if (tr) {
                Vec3 fwd = IL2.Unbox<Vec3>(IL2.Invoke(G.TrGetFwd, tr));
                float spd = kFlySpeed[g_speedTier < 4 ? g_speedTier : 3];
                SetGrav(local, 0.0f);
                SetVel(local, fwd * spd);
            }
        }
    }

    // Velocity fly: hold A, camera-forward at a fixed 20 m/s impulse
    if (f_velocityFly && BtnA()) {
        Il2CppObject *cam = G.CamGetMain ? IL2.Invoke(G.CamGetMain, nullptr) : nullptr;
        if (cam) {
            Il2CppObject *tr = IL2.Invoke(G.GoGetTr, cam);
            if (tr) {
                Vec3 fwd = IL2.Unbox<Vec3>(IL2.Invoke(G.TrGetFwd, tr));
                SetGrav(local, 0.0f);
                SetVel(local, fwd * 20.0f);
            }
        }
    }

    // Speed: horizontal scale each frame
    if (f_speed) {
        Vec3 v = GetVel(local);
        float m = kSpeedMult[g_speedTier < 5 ? g_speedTier : 4];
        v.x *= m; v.z *= m;
        SetVel(local, v);
    }

    if (f_noGravity)                    SetGrav(local, 0.0f);
    else if (f_lowGravity)              SetGrav(local, 0.5f);

    // Spin: tangential kick each frame
    if (f_spin) {
        Vec3 v = GetVel(local); v.y = 0;
        Vec3 side = {-v.z, 0, v.x};
        float l = V3Len(side);
        if (l > 0.1f) side = side * (1/l);
        SetVel(local, GetVel(local) + side * 2.0f);
    }

    // Freeze others: one pass per frame, bounded by the 80-player gate
    if (f_freeze) {
        for (auto &p : g_players) {
            if (p.isLocal || !p.api) continue;
            SetVel(p.api.get(), Vec3{0,0,0});
        }
        g_wasFrozen = true;
    } else if (g_wasFrozen) {
        g_wasFrozen = false;
    }
}

// ============================================================================
// Punch mod — right trigger launches nearest player inside 5 m
// ============================================================================
static bool g_punchArmed = false;
static void PunchTick(Il2CppObject *local) {
    if (!f_punch || !local) { g_punchArmed = false; return; }
    bool trig = RTrigger();
    if (trig && !g_punchArmed && g_time >= g_punchNext) {
        g_punchArmed = true;
        g_punchNext  = g_time + 0.5;
        Vec3 myPos = GetPos(local);
        Il2CppObject *target = nullptr;
        float bestDist = 5.0f;
        for (auto &p : g_players) {
            if (p.isLocal || !p.api) continue;
            float d = V3Len(p.pos - myPos);
            if (d < bestDist) { bestDist = d; target = p.api.get(); }
        }
        if (target) {
            Vec3 dir = V3Norm(GetPos(target) - myPos);
            SetVel(target, dir * 20.0f + Vec3{0,8,0});
            LOGI("Punch: launched player %.1fm away", bestDist);
        }
    }
    if (!trig) g_punchArmed = false;
}

// ============================================================================
// Portal gun — right grip + trigger teleports along camera forward
// ============================================================================
static bool g_portalFired = false;
static void PortalGunTick(Il2CppObject *local) {
    if (!f_portalGun || !local) { g_portalFired = false; return; }
    bool trig = RTrigger() && RGrip();
    if (trig && !g_portalFired) {
        g_portalFired = true;
        Il2CppObject *cam = G.CamGetMain ? IL2.Invoke(G.CamGetMain, nullptr) : nullptr;
        if (cam) {
            Il2CppObject *tr = IL2.Invoke(G.GoGetTr, cam);
            if (tr) {
                Vec3 pos = IL2.Unbox<Vec3>(IL2.Invoke(G.TrGetPos, tr));
                Vec3 fwd = IL2.Unbox<Vec3>(IL2.Invoke(G.TrGetFwd, tr));
                TeleportTo(local, pos + fwd * 20.0f);
            }
        }
    }
    if (!trig) g_portalFired = false;
}

// ============================================================================
// Invisibility — cached renderer set, restored exactly on toggle-off
// ============================================================================
static std::vector<Ref> g_ownRenderers;

static void CacheOwnRenderers() {
    if (!g_ownRenderers.empty() || !G.Renderer || !G.ObjFindTypeAll) return;
    Il2CppObject *typeObj = IL2.TypeOf(G.Renderer);
    if (!typeObj) return;
    void *a[1]={typeObj};
    Il2CppArray *arr = (Il2CppArray*)IL2.Invoke(G.ObjFindTypeAll, nullptr, a);
    if (!arr) return;
    size_t n = *(uintptr_t*)((uint8_t*)arr + kArrLengthAt);
    if (n > 4096) n = 4096;                    // bound: a hostile scene cannot blow memory
    for (size_t i=0;i<n;i++){
        Il2CppObject *r = ((Il2CppObject**)((uint8_t*)arr + kArrDataAt))[i];
        if (r) g_ownRenderers.push_back(Ref(r));
    }
    LOGI("renderer cache: %zu", g_ownRenderers.size());
}

static void InvisibleTick() {
    if (f_invisible == g_wasInvisible) return;
    if (!G.RendSetEnabled) return;
    if (f_invisible) {
        CacheOwnRenderers();
        bool on = false;
        for (auto &r : g_ownRenderers) {
            if (!r) continue;
            void *a[1]={&on};
            IL2.Invoke(G.RendSetEnabled, r.get(), a);
        }
        LOGI("invisible on (%zu renderers hidden)", g_ownRenderers.size());
    } else {
        bool on = true;
        for (auto &r : g_ownRenderers) {
            if (!r) continue;
            void *a[1]={&on};
            IL2.Invoke(G.RendSetEnabled, r.get(), a);
        }
        LOGI("invisible off");
    }
    g_wasInvisible = f_invisible;
}

// ============================================================================
// Name spoof — local nameplate label only
// ============================================================================
// Scope, named plainly: this rewrites the text on the *local* copy of your own
// nameplate. It does not change what remote clients receive — Photon carries
// the display name from the server-side player entry, and rewriting that is a
// property write the server rejects for non-master clients. Anything that
// claims otherwise is guessing.
static std::string g_spoofName = "Dark Matter";
static Ref  g_nameplateText;
static bool g_nameplateHooked = false;

static void NameSpoofTick() {
    Il2CppClass* tm = IL2.GetClass("TMPro","TMP_Text","Unity.TextMeshPro");
    if (!tm) tm = IL2.GetClass("UnityEngine.UI","Text","UnityEngine.UI");
    if (!tm) return;
    MethodInfo* setText = IL2.GetMethod(tm, "set_text", 1);
    if (!setText) return;

    if (f_nameSpoof && !g_nameplateHooked) {
        // Find the local player's nameplate: a Text/TMP_Text whose transform
        // sits under the local avatar root and whose content equals the local
        // display name. Walking that match once is cheaper than a per-frame scan.
        Il2CppClass* obj = G.Object ? G.Object : IL2.GetClass("UnityEngine","Object","UnityEngine.CoreModule");
        Il2CppObject* type = IL2.TypeOf(tm);
        MethodInfo* findAll = obj ? IL2.GetMethod(obj, "FindObjectsOfType", 1) : nullptr;
        if (!findAll || !type) return;
        void* a[1] = { type };
        Il2CppArray* arr = (Il2CppArray*)IL2.Invoke(findAll, nullptr, a);
        if (!arr) return;
        size_t n = *(uintptr_t*)((uint8_t*)arr + kArrLengthAt);
        if (n > 4096) n = 4096;
        std::string me = g_players.empty() ? "" : [&]{
            for (auto& p : g_players) if (p.isLocal) return p.name;
            return std::string("");
        }();
        MethodInfo* getText = IL2.GetMethod(tm, "get_text", 0);
        for (size_t i=0;i<n;i++){
            Il2CppObject* t = ((Il2CppObject**)((uint8_t*)arr + kArrDataAt))[i];
            if (!t || !getText) continue;
            Il2CppObject* sObj = IL2.Invoke(getText, t);
            std::string s = IL2.StrGet((Il2CppString*)sObj);
            if (!me.empty() && s == me) {
                g_nameplateText.set(t);
                g_nameplateHooked = true;
                LOGI("namespoof: nameplate bound ('%s')", s.c_str());
                break;
            }
        }
    }
    if (!f_nameSpoof && g_nameplateHooked) {
        Il2CppObject* t = g_nameplateText.get();
        if (t && setText) {
            std::string me;
            for (auto& p : g_players) if (p.isLocal) me = p.name;
            if (!me.empty()) { void* a[1] = { IL2.StrNew(me.c_str()) }; IL2.Invoke(setText, t, a); }
        }
        g_nameplateText.clear();
        g_nameplateHooked = false;
        LOGI("namespoof: restored");
        return;
    }
    if (f_nameSpoof && g_nameplateHooked) {
        Il2CppObject* t = g_nameplateText.get();
        if (t && setText) { void* a[1] = { IL2.StrNew(g_spoofName.c_str()) }; IL2.Invoke(setText, t, a); }
    }
}

// ============================================================================
// Platforms — a real collider cube spawned under each held hand
// ============================================================================
// Uses GameObject.CreatePrimitive so the object arrives with a MeshRenderer and
// a BoxCollider already wired; no asset bundle, no prefab lookup.
static Ref g_platL, g_platR;
static bool g_platformsUp = false;

static Il2CppObject* SpawnCube(const char* name) {
    Il2CppClass* go = G.GameObject;
    if (!go) go = IL2.GetClass("UnityEngine","GameObject","UnityEngine.CoreModule");
    if (!go) return nullptr;
    MethodInfo* create = IL2.GetMethod(go, "CreatePrimitive", 1);
    if (!create) { LOGE("platforms: CreatePrimitive unresolved"); return nullptr; }
    // PrimitiveType.Cube == 3
    int32_t cube = 3;
    void* a[1] = { &cube };
    Il2CppObject* obj = IL2.Invoke(create, nullptr, a);
    if (!obj) return nullptr;
    // Scale to a 0.6 m slab, 4 cm thick.
    Il2CppObject* tr = Tr(obj);
    if (tr && G.TrSetLS) { Vec3 sc{0.6f, 0.04f, 0.6f}; void* b[1] = { &sc }; IL2.Invoke(G.TrSetLS, tr, b); }
    LOGI("platform spawned: %s", name);
    return obj;
}

static void DestroyObj(Ref& r) {
    if (!r) return;
    Il2CppClass* obj = G.Object;
    if (!obj) obj = IL2.GetClass("UnityEngine","Object","UnityEngine.CoreModule");
    MethodInfo* destroy = obj ? IL2.GetMethod(obj, "Destroy", 1) : nullptr;
    Il2CppObject* o = r.get();
    if (destroy && o) { void* a[1] = { o }; IL2.Invoke(destroy, nullptr, a); }
    r.clear();
}

static void PlatformsTick() {
    if (!f_platforms) {
        if (g_platformsUp) { DestroyObj(g_platL); DestroyObj(g_platR); g_platformsUp = false; LOGI("platforms down"); }
        return;
    }
    if (!g_platformsUp) {
        g_platL.set(SpawnCube("DarkMatter_PlatL"));
        g_platR.set(SpawnCube("DarkMatter_PlatR"));
        g_platformsUp = g_platL && g_platR;
    }
    // Follow the hands while held; park 40 cm under the palm.
    Il2CppClass* api = G.PlayerApi;
    MethodInfo* getBone = api ? IL2.GetMethod(api, "GetBoneTransform", 1) : nullptr;
    Il2CppObject* local = LocalPlayer();
    if (!getBone || !local) return;
    Ref* slots[2] = { &g_platL, &g_platR };
    int32_t bones[2] = { 17, 18 };           // LeftHand, RightHand
    for (int i=0;i<2;i++){
        void* ba[1] = { &bones[i] };
        Il2CppObject* bone = IL2.Invoke(getBone, local, ba);
        if (!bone) continue;
        Il2CppObject* boneTr = Tr(bone);
        Il2CppObject* platTr = Tr(slots[i]->get());
        if (!boneTr || !platTr || !G.TrGetPos || !G.TrSetPos) continue;
        Vec3 p = IL2.Unbox<Vec3>(IL2.Invoke(G.TrGetPos, boneTr));
        p.y -= 0.40f;
        void* a[1] = { &p };
        IL2.Invoke(G.TrSetPos, platTr, a);
    }
}

// ============================================================================
// Big head — head bone local scale, restored on toggle-off
// ============================================================================
static Ref g_headBone;
static Vec3 g_headScaleSaved{1,1,1};
static bool g_bigHeadApplied = false;

static void BigHeadTick() {
    if (f_bigHead == g_bigHeadApplied) return;
    Il2CppClass* api = G.PlayerApi;
    MethodInfo* getBone = api ? IL2.GetMethod(api, "GetBoneTransform", 1) : nullptr;
    Il2CppObject* local = LocalPlayer();
    if (!getBone || !local) return;

    if (f_bigHead && !g_headBone) {
        int32_t head = 10;                    // HumanBodyBones.Head
        void* a[1] = { &head };
        Il2CppObject* bone = IL2.Invoke(getBone, local, a);
        if (!bone) { LOGE("bighead: head bone unresolved"); return; }
        g_headBone.set(bone);
        Il2CppObject* tr = Tr(bone);
        if (tr && G.TrGetLS) {
            Il2CppObject* sc = IL2.Invoke(G.TrGetLS, tr);
            if (sc) g_headScaleSaved = IL2.Unbox<Vec3>(sc);
        }
    }
    Il2CppObject* bone = g_headBone.get();
    if (!bone) return;
    Il2CppObject* tr = Tr(bone);
    if (!tr) return;

    Vec3 scale = f_bigHead ? g_headScaleSaved * 3.0f : g_headScaleSaved;
    if (G.TrSetLS) { void* a[1] = { &scale }; IL2.Invoke(G.TrSetLS, tr, a); }
    g_bigHeadApplied = f_bigHead;
    LOGI("bighead %s", f_bigHead ? "on" : "off");
}

// ============================================================================
// Orbit cam — detach the main camera and orbit the local player
// ============================================================================
static Ref g_orbitCam;
static Vec3 g_orbitSavedPos{};
static Quat g_orbitSavedRot{};
static bool g_orbitApplied = false;
static float g_orbitAngle = 0.f;

static void OrbitCamTick() {
    if (f_orbitCam == g_orbitApplied) return;
    Il2CppObject* cam = G.CamGetMain ? IL2.Invoke(G.CamGetMain, nullptr) : nullptr;
    if (!cam) return;
    Il2CppObject* tr = Tr(cam);
    if (!tr) return;

    if (f_orbitCam) {
        g_orbitCam.set(cam);
        if (G.TrGetPos) g_orbitSavedPos = IL2.Unbox<Vec3>(IL2.Invoke(G.TrGetPos, tr));
        if (G.TrGetRot) g_orbitSavedRot = IL2.Unbox<Quat>(IL2.Invoke(G.TrGetRot, tr));
        g_orbitApplied = true;
        LOGI("orbit cam on");
    } else {
        if (G.TrSetPos) { void* a[1] = { &g_orbitSavedPos }; IL2.Invoke(G.TrSetPos, tr, a); }
        if (G.TrSetRot) { void* a[1] = { &g_orbitSavedRot }; IL2.Invoke(G.TrSetRot, tr, a); }
        g_orbitCam.clear();
        g_orbitApplied = false;
        LOGI("orbit cam off");
    }
}

static void OrbitCamMove() {
    if (!f_orbitCam || !g_orbitApplied) return;
    Il2CppObject* tr = Tr(g_orbitCam.get());
    Il2CppObject* local = LocalPlayer();
    if (!tr || !local) return;
    Vec3 p = GetPos(local);
    g_orbitAngle += g_dt * 0.6f;
    // 2.2 m radius, 1.6 m above the player's feet, looking back at them.
    Vec3 camPos{ p.x + cosf(g_orbitAngle)*2.2f,
                 p.y + 1.6f,
                 p.z + sinf(g_orbitAngle)*2.2f };
    if (G.TrSetPos) { void* a[1] = { &camPos }; IL2.Invoke(G.TrSetPos, tr, a); }
    // Look-at via rotation is a managed Quaternion.LookRotation call.
    Il2CppClass* qc = IL2.GetClass("UnityEngine","Quaternion","UnityEngine.CoreModule");
    MethodInfo* lookAt = qc ? IL2.GetMethod(qc, "LookRotation", 1) : nullptr;
    if (lookAt) {
        Vec3 dir = V3Norm(p + Vec3{0,1.4f,0} - camPos);
        void* a[1] = { &dir };
        Il2CppObject* q = IL2.Invoke(lookAt, nullptr, a);
        if (q && G.TrSetRot) { Quat rot = IL2.Unbox<Quat>(q); void* b[1] = { &rot }; IL2.Invoke(G.TrSetRot, tr, b); }
    }
}

// ============================================================================
// Noclip — physics collision matrix, restored exactly
// ============================================================================
// Unity exposes Physics.IgnoreLayerCollision(layer, layer, ignore). The local
// player sits on the "Player" layer; ignoring Player↔everything removes world
// collision without touching velocity, so gravity and fly keep working.
static bool g_noclipApplied = false;
static bool g_noclipIgnore[32] = {};

static void NoclipTick() {
    if (f_noclip == g_noclipApplied) return;
    Il2CppClass* phys = IL2.GetClass("UnityEngine","Physics","UnityEngine.PhysicsModule");
    if (!phys) phys = G.Physics;
    MethodInfo* ign = phys ? IL2.GetMethod(phys, "IgnoreLayerCollision", 3) : nullptr;
    if (!ign) { LOGE("noclip: IgnoreLayerCollision unresolved"); return; }

    // Layer 9 is the convention for the local player in VRChat; a build that
    // moves it only needs the one constant changed here.
    int playerLayer = 9;                    // VRChat's local-player layer
    for (int l = 0; l < 32; l++) {
        if (l == playerLayer) continue;
        bool ignore = f_noclip;
        void* a[3] = { &playerLayer, &l, &ignore };
        if (f_noclip) g_noclipIgnore[l] = true;
        IL2.Invoke(ign, nullptr, a);
    }
    g_noclipApplied = f_noclip;
    LOGI("noclip %s", f_noclip ? "on" : "off");
}

// ============================================================================
// World blocks — tint every renderer material in the world
// ============================================================================
// Cached once (the renderer array is expensive to walk), tinted per frame when
// rainbow is on, restored to the captured colour on disable.
struct TintEntry { Ref rend; Ref mat; Color orig; };
static std::vector<TintEntry> g_tints;
static bool g_tintsApplied = false;

static void CacheWorldRenderers() {
    if (!g_tints.empty()) return;
    Il2CppClass* rend = G.Renderer;
    if (!rend) rend = IL2.GetClass("UnityEngine","Renderer","UnityEngine.CoreModule");
    Il2CppClass* obj  = G.Object;
    if (!rend || !obj) return;
    MethodInfo* findAll = IL2.GetMethod(obj, "FindObjectsOfType", 1);
    Il2CppObject* type = IL2.TypeOf(rend);
    if (!findAll || !type) return;
    void* a[1] = { type };
    Il2CppArray* arr = (Il2CppArray*)IL2.Invoke(findAll, nullptr, a);
    if (!arr) return;
    size_t n = *(uintptr_t*)((uint8_t*)arr + kArrLengthAt);
    if (n > 2048) n = 2048;                 // bounded: a big world cannot stall the tick
    MethodInfo* getMat = IL2.GetMethod(rend, "get_material", 0);
    MethodInfo* getCol = IL2.GetMethod(G.Material, "get_color", 0);
    if (!getMat || !getCol) return;
    for (size_t i=0;i<n;i++){
        Il2CppObject* r = ((Il2CppObject**)((uint8_t*)arr + kArrDataAt))[i];
        if (!r) continue;
        Il2CppObject* m = IL2.Invoke(getMat, r);
        if (!m) continue;
        Il2CppObject* c = IL2.Invoke(getCol, m);
        TintEntry t;
        t.rend.set(r);
        t.mat.set(m);
        if (c) t.orig = IL2.Unbox<Color>(c);
        g_tints.push_back(std::move(t));
    }
    LOGI("world tint cache: %zu materials", g_tints.size());
}

static void WorldBlockTick() {
    bool want = f_worldBlocks || f_blockRainbow;
    if (!want && !g_tintsApplied) return;
    if (want) CacheWorldRenderers();
    if (g_tints.empty() || !G.MatSetColor) return;

    if (want) {
        Color c = f_blockRainbow ? ColorRainbow((float)g_time*0.15f)
                                 : kSkyColors[g_blockColorIdx];
        for (auto& t : g_tints) {
            Il2CppObject* m = t.mat.get();
            if (!m) continue;
            void* a[1] = { &c };
            IL2.Invoke(G.MatSetColor, m, a);
        }
        g_tintsApplied = true;
    } else {
        for (auto& t : g_tints) {
            Il2CppObject* m = t.mat.get();
            if (!m) continue;
            void* a[1] = { &t.orig };
            IL2.Invoke(G.MatSetColor, m, a);
        }
        g_tintsApplied = false;
        LOGI("world tint restored");
    }
}

// ============================================================================
// Save / load spot
// ============================================================================
void SaveSpot(Il2CppObject *local) {
    if (!local) return;
    g_savedSpot = GetPos(local);
    g_hasSavedSpot = true;
    LOGI("Spot saved %.1f %.1f %.1f", g_savedSpot.x, g_savedSpot.y, g_savedSpot.z);
}
void GoToSpot(Il2CppObject *local) {
    if (!local || !g_hasSavedSpot) return;
    TeleportTo(local, g_savedSpot);
}

// ============================================================================
// Init / Update / Shutdown
// ============================================================================
void Features_Init() {
    for (int i=0;i<10 && !IL2.ready;i++) usleep(200000);
    InitG();
    LOGI("Features init");
}

void Features_Update() {
    if (!InitG()) return;
    RefreshPlayers();
    Il2CppObject *local = LocalPlayer();
    MovementTick(local);
    PunchTick(local);
    PortalGunTick(local);
    SkyboxTick();
    InvisibleTick();
    NoclipTick();
    WorldBlockTick();
    PlatformsTick();
    NameSpoofTick();
    BigHeadTick();
    OrbitCamTick();
    OrbitCamMove();
}

void Features_Shutdown() {
    // Restore anything this module changed so a reload starts from stock.
    Il2CppObject *local = LocalPlayer();
    if (local) {
        SetGrav(local, 1.0f);
    }
    if (f_skybox || f_skyRainbow) RestoreSkybox();
    if (g_wasInvisible) {
        f_invisible = false;
        InvisibleTick();
    }
    // Undo the two world mutations that outlive a flag flip.
    if (g_noclipApplied) { f_noclip = false; NoclipTick(); }
    if (g_tintsApplied)   { f_worldBlocks = f_blockRainbow = false; WorldBlockTick(); }
    if (g_nameplateHooked) { f_nameSpoof = false; NameSpoofTick(); }
    if (g_platformsUp)    { f_platforms = false; PlatformsTick(); }
    if (g_bigHeadApplied) { f_bigHead = false; BigHeadTick(); }
    if (g_orbitApplied)   { f_orbitCam = false; OrbitCamTick(); }
    g_headBone.clear();
    g_orbitCam.clear();
    g_tints.clear();
    g_skyMat.clear();
    g_skySaved.clear();
    g_ownRenderers.clear();
    g_players.clear();
    LOGI("Features shutdown");
}

// ============================================================================
// Menu-facing surface
// ============================================================================
void Feature_SaveSpot()  { SaveSpot(LocalPlayer()); }
void Feature_GoToSpot()  { GoToSpot(LocalPlayer()); }

void Feature_AllOff() {
    f_fly=f_velocityFly=f_speed=f_noclip=f_lowGravity=f_noGravity=false;
    f_platforms=f_punch=f_esp=f_skybox=f_skyRainbow=false;
    f_worldBlocks=f_blockRainbow=f_portalGun=f_orbitCam=false;
    f_freeze=f_spin=f_bigHead=f_nameSpoof=f_speedServer=false;
    Il2CppObject *local=LocalPlayer();
    if(local) SetGrav(local,1.0f);
    RestoreSkybox();
    g_skyMat.clear();
    if (g_wasInvisible) { f_invisible = false; InvisibleTick(); }
    if (g_noclipApplied) { f_noclip = false; NoclipTick(); }
    if (g_tintsApplied)  { f_worldBlocks = f_blockRainbow = false; WorldBlockTick(); }
    LOGI("All features off");
}

void Feature_NextSpeed()   { g_speedTier=(g_speedTier+1)%5; }
void Feature_NextSkyColor(){ g_skyColorIdx=(g_skyColorIdx+1)%kSkyCount; if(f_skybox)ApplySkybox(kSkyColors[g_skyColorIdx]); }
const char* Feature_SkyName(){ return kSkyNames[g_skyColorIdx]; }
const char* Feature_SpeedName(){
    static const char*n[]={"1.5x","2x","3x","5x","8x"};
    return n[g_speedTier<5?g_speedTier:4];
}
size_t Feature_PlayerCount(){ return g_players.size(); }
const char* Feature_PlayerName(int i){ return (i>=0&&i<(int)g_players.size())?g_players[i].name.c_str():""; }
float Feature_PlayerDist(int i){ return (i>=0&&i<(int)g_players.size())?g_players[i].dist:0; }
bool  Feature_PlayerIsLocal(int i){ return (i>=0&&i<(int)g_players.size())&&g_players[i].isLocal; }
int   Feature_PlayerActor(int i){ return (i>=0&&i<(int)g_players.size())?g_players[i].actor:0; }
bool  Feature_LeftStickClick(){ return LeftStickClick(); }
bool  Feature_BtnA()          { return BtnA(); }
bool  Feature_RTrigger()      { return RTrigger(); }
bool  Feature_LTrigger()      { return LTrigger(); }
float Feature_RAxisX()       { return Axis("Oculus_CrossPlatform_SecondaryThumbstickHorizontal"); }
float Feature_RAxisY()       { return Axis("Oculus_CrossPlatform_SecondaryThumbstickVertical"); }
float* Feature_FlySpeedPtr(){ return &g_flySpeed; }
float* Feature_FlapMultPtr(){ return &g_flapMult; }
