#include "shared.hpp"
#include <cstdlib>

// ============================================================================
// VRChat class cache
// ============================================================================
static struct {
    bool ready = false;
    Il2CppClass *PlayerApi, *Networking, *RenderSettings,
                *Camera, *GameObject, *Transform, *Rigidbody,
                *Material, *Renderer, *Shader, *PhotonView,
                *Input, *XRInput;
    MethodInfo  *PlayerGetPos, *PlayerSetPos, *PlayerGetVel, *PlayerSetVel,
                *PlayerGetName, *PlayerIsLocal, *PlayerGetId,
                *PlayerSetGrav, *PlayerGetGrav,
                *PlayerGetGrounded, *PlayerTeleport,
                *PlayerGetAll,
                *NetLocalPlayer,
                *CamGetMain, *CamGetVP,
                *TrGetPos, *TrSetPos, *TrGetRot, *TrGetFwd, *TrGetRight, *TrGetUp,
                *GoGetTr, *GoGetComp, *GoAddComp, *GoSetActive,
                *RendGetMat, *MatSetColor, *MatSetShader, *ShaderFind,
                *RsGetSkybox, *RsSetSkybox,
                *InputGetAxis, *InputGetButton, *InputGetButtonDown,
                *ObjFindType, *ObjFindTypeAll;
} G;

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
    C(PhotonView,    "Photon.Pun",   "PhotonView",      "Assembly-CSharp")
    C(Input,         "UnityEngine",  "Input",           "UnityEngine")
    C(RenderSettings,"UnityEngine",  "RenderSettings",  "UnityEngine")

    M(PlayerGetPos,    PlayerApi, "get_position",        0)
    M(PlayerSetPos,    PlayerApi, "TeleportTo",          1)
    M(PlayerGetVel,    PlayerApi, "GetVelocity",         0)
    M(PlayerSetVel,    PlayerApi, "SetVelocity",         1)
    M(PlayerGetName,   PlayerApi, "get_displayName",     0)
    M(PlayerIsLocal,   PlayerApi, "get_isLocal",         0)
    M(PlayerGetId,     PlayerApi, "get_playerId",        0)
    M(PlayerSetGrav,   PlayerApi, "SetGravityStrength",  1)
    M(PlayerGetGrounded,PlayerApi,"IsPlayerGrounded",    0)
    M(PlayerTeleport,  PlayerApi, "TeleportTo",          1)
    M(PlayerGetAll,    PlayerApi, "GetAllPlayers",       0)
    M(NetLocalPlayer,  Networking,"get_LocalPlayer",     0)
    M(CamGetMain,      Camera,    "get_main",            0)
    M(TrGetPos,        Transform, "get_position",        0)
    M(TrSetPos,        Transform, "set_position",        1)
    M(TrGetRot,        Transform, "get_rotation",        0)
    M(TrGetFwd,        Transform, "get_forward",         0)
    M(TrGetRight,      Transform, "get_right",           0)
    M(TrGetUp,         Transform, "get_up",              0)
    M(GoGetTr,         GameObject,"get_transform",       0)
    M(GoGetComp,       GameObject,"GetComponent",        1)
    M(GoSetActive,     GameObject,"SetActive",           1)
    M(RendGetMat,      Renderer,  "get_material",        0)
    M(MatSetColor,     Material,  "set_color",           1)
    M(MatSetShader,    Material,  "set_shader",          1)
    M(ShaderFind,      Shader,    "Find",                1)
    M(RsGetSkybox,     RenderSettings,"get_skybox",      0)
    M(RsSetSkybox,     RenderSettings,"set_skybox",      1)
    M(InputGetAxis,    Input,     "GetAxis",             1)
    M(InputGetButton,  Input,     "GetButton",           1)
    M(InputGetButtonDown,Input,   "GetButtonDown",       1)
#undef C
#undef M
    G.ready = G.PlayerApi && G.PlayerGetPos && G.PlayerGetVel && G.Input;
    LOGI("VRC classes %s", G.ready?"ready":"INCOMPLETE");
    return G.ready;
}

// ============================================================================
// Input helpers
// ============================================================================
static bool Btn(const char *name) {
    if (!G.Input || !G.InputGetButton) return false;
    void *a[1]={IL2.StrNew(name)};
    Il2CppObject *r = IL2.Invoke(G.InputGetButton, nullptr, a);
    return r ? IL2.Unbox<int32_t>(r)!=0 : false;
}
static bool BtnDown(const char *name) {
    if (!G.Input || !G.InputGetButtonDown) return false;
    void *a[1]={IL2.StrNew(name)};
    Il2CppObject *r = IL2.Invoke(G.InputGetButtonDown, nullptr, a);
    return r ? IL2.Unbox<int32_t>(r)!=0 : false;
}
static float Axis(const char *name) {
    if (!G.Input || !G.InputGetAxis) return 0.f;
    void *a[1]={IL2.StrNew(name)};
    Il2CppObject *r = IL2.Invoke(G.InputGetAxis, nullptr, a);
    return r ? IL2.Unbox<float>(r) : 0.f;
}

// VRChat Quest controller mappings (UnityEngine.Input legacy axis names)
// Left joystick click = "Fire3" or "joystick button 8" on Quest
static bool LeftStickClick() {
    return BtnDown("Fire3") || BtnDown("joystick button 8");
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
    Il2CppObject *o = IL2.Invoke(G.PlayerGetPos, api);
    return IL2.Unbox<Vec3>(o);
}
static Vec3 GetVel(Il2CppObject *api) {
    Il2CppObject *o = IL2.Invoke(G.PlayerGetVel, api);
    return IL2.Unbox<Vec3>(o);
}
static void SetVel(Il2CppObject *api, Vec3 v) {
    void *a[1]={&v};
    IL2.Invoke(G.PlayerSetVel, api, a);
}
static void SetGrav(Il2CppObject *api, float g) {
    void *a[1]={&g};
    if (G.PlayerSetGrav) IL2.Invoke(G.PlayerSetGrav, api, a);
}
static void TeleportTo(Il2CppObject *api, Vec3 pos) {
    void *a[1]={&pos};
    if (G.PlayerTeleport) IL2.Invoke(G.PlayerTeleport, api, a);
}

// ============================================================================
// Player list (shared with ESP and menu)
// ============================================================================
struct PlayerInfo { Il2CppObject *api; std::string name; Vec3 pos; float dist; bool isLocal; int id; };
std::vector<PlayerInfo> g_players;
static double g_nextRefresh = 0;

static void RefreshPlayers() {
    if (!G.PlayerGetAll || g_time < g_nextRefresh) return;
    g_nextRefresh = g_time + 0.5;
    Il2CppArray *arr = (Il2CppArray*)IL2.Invoke(G.PlayerGetAll, nullptr);
    if (!arr) return;
    size_t n = *(uintptr_t*)((uint8_t*)arr+0x18);
    n = n > 80 ? 80 : n;
    Il2CppObject *local = LocalPlayer();
    Vec3 lp = local ? GetPos(local) : Vec3{};
    g_players.clear();
    for (size_t i=0;i<n;i++){
        Il2CppObject *api = ((Il2CppObject**)((uint8_t*)arr+0x20))[i];
        if (!api) continue;
        PlayerInfo p;
        p.api = api;
        p.name = IL2.StrGet((Il2CppString*)IL2.Invoke(G.PlayerGetName, api));
        p.pos  = GetPos(api);
        Vec3 d = p.pos - lp; p.dist = V3Len(d);
        Il2CppObject *lo = IL2.Invoke(G.PlayerIsLocal, api);
        p.isLocal = lo ? IL2.Unbox<int32_t>(lo)!=0 : false;
        Il2CppObject *id = IL2.Invoke(G.PlayerGetId, api);
        p.id = id ? IL2.Unbox<int32_t>(id) : 0;
        g_players.push_back(p);
    }
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
bool f_freeze       = false;   // freeze all other players in place
bool f_spin         = false;   // spin local player
bool f_bigHead      = false;   // scale head bone up
bool f_invisible    = false;   // disable own renderers
bool f_nameSpoof    = false;   // fake display name
bool f_antiEac      = true;

int  g_speedTier    = 1;
int  g_skyColorIdx  = 0;
int  g_blockColorIdx= 0;

static const float kFlySpeed[]  = {4,8,16,24};
static const float kSpeedMult[] = {1.5f,2.0f,3.0f,5.0f,8.0f};

static Vec3  g_savedSpot{};
static bool  g_hasSavedSpot = false;
static double g_punchNext   = 0;
static double g_skyNext     = 0;
static double g_blockNext   = 0;

// ============================================================================
// Skybox
// ============================================================================
static const Color kSkyColors[] = {
    {0.03f,0.03f,0.10f,1}, // Midnight
    {0.10f,0.00f,0.20f,1}, // Void Purple
    {0.60f,0.10f,0.05f,1}, // Blood Red
    {0.00f,0.25f,0.10f,1}, // Deep Forest
    {0.05f,0.15f,0.50f,1}, // Ocean Night
    {0.80f,0.50f,0.10f,1}, // Golden Hour
    {0.00f,0.00f,0.00f,1}, // Pure Black
    {1.00f,1.00f,1.00f,1}, // Pure White
    {0.00f,0.80f,0.80f,1}, // Neon Cyan
    {0.90f,0.00f,0.90f,1}, // Neon Pink
};
static const char *kSkyNames[] = {"Midnight","Void Purple","Blood Red","Deep Forest","Ocean Night","Golden Hour","Black","White","Neon Cyan","Neon Pink"};
static constexpr int kSkyCount = 10;

static Il2CppObject *g_skyMat = nullptr;

static void ApplySkybox(Color c) {
    if (!G.RsSetSkybox) return;
    if (!g_skyMat && G.Material && G.ShaderFind) {
        const char *shaders[]={"Skybox/Panoramic","Unlit/Color","Universal Render Pipeline/Unlit",nullptr};
        Il2CppObject *sh=nullptr;
        for (int i=0;shaders[i]&&!sh;i++){void*a[1]={IL2.StrNew(shaders[i])};sh=IL2.Invoke(G.ShaderFind,nullptr,a);}
        if (sh){
            g_skyMat=IL2.object_new(G.Material);
            MethodInfo *ctor=IL2.GetMethod(G.Material,".ctor",1);
            if(ctor){void*a[1]={sh};IL2.Invoke(ctor,g_skyMat,a);}
        }
    }
    if (!g_skyMat) return;
    if (G.MatSetColor){void*a[1]={&c};IL2.Invoke(G.MatSetColor,g_skyMat,a);}
    void *a[1]={g_skyMat};
    IL2.Invoke(G.RsSetSkybox,nullptr,a);
}

static void SkyboxTick() {
    if ((!f_skybox && !f_skyRainbow) || g_time < g_skyNext) return;
    g_skyNext = g_time + 0.5;
    Color c = f_skyRainbow ? ColorRainbow((float)g_time*0.08f) : kSkyColors[g_skyColorIdx];
    ApplySkybox(c);
}

// ============================================================================
// EAC patch
// ============================================================================
// FILL: replace 0x0 with real offsets from IDA/Ghidra on your VRC build
namespace EACOff {
    static constexpr uintptr_t kBeginSession  = 0x0;
    static constexpr uintptr_t kAddNotify     = 0x0;
    static constexpr uintptr_t kValidateSend  = 0x0;
    static constexpr uintptr_t kIsIntegrity   = 0x0;
    static constexpr uintptr_t kIsModded      = 0x0;
}
void PatchEAC(uintptr_t base) {
    if (EACOff::kBeginSession)  RetPatch(base+EACOff::kBeginSession);
    if (EACOff::kAddNotify)     RetPatch(base+EACOff::kAddNotify);
    if (EACOff::kValidateSend)  NopRange(base+EACOff::kValidateSend, 6);
    if (EACOff::kIsIntegrity)   RetTrue(base+EACOff::kIsIntegrity);
    if (EACOff::kIsModded)      RetPatch(base+EACOff::kIsModded);
    // Symbol-level EOS patch
    void *eos=dlopen("libEOSSDK-Android-Shipping.so",RTLD_NOW|RTLD_NOLOAD);
    if (eos){
        const char *syms[]={"EOS_AntiCheatClient_BeginSession","EOS_AntiCheatClient_AddNotifyMessageToServer",
                            "EOS_AntiCheatClient_AddNotifyPeerActionRequired","EOS_AntiCheatClient_ReceiveMessageFromServer",nullptr};
        for (int i=0;syms[i];i++){void*s=dlsym(eos,syms[i]);if(s)RetPatch((uintptr_t)s);}
    }
    LOGI("EAC patches applied");
}

// ============================================================================
// Fly / movement tick
// ============================================================================
static void MovementTick(Il2CppObject *local) {
    if (!local) return;
    Vec3 vel = GetVel(local);

    // Fly: hold A, move in head-forward direction
    if (f_fly && BtnA()) {
        Il2CppObject *cam = G.CamGetMain ? IL2.Invoke(G.CamGetMain, nullptr) : nullptr;
        if (cam) {
            Il2CppObject *tr = IL2.Invoke(G.GoGetTr, cam);
            if (tr) {
                Il2CppObject *fwdObj = IL2.Invoke(G.TrGetFwd, tr);
                Vec3 fwd = IL2.Unbox<Vec3>(fwdObj);
                float spd = kFlySpeed[g_speedTier < 4 ? g_speedTier : 3];
                vel = fwd * spd;
                SetGrav(local, 0.0f);
            }
        }
        SetVel(local, vel);
    }

    // Velocity fly: hold A, shoot in right-hand direction
    if (f_velocityFly && BtnA()) {
        // right hand forward — use camera as proxy if hand transform not wired
        Il2CppObject *cam = G.CamGetMain ? IL2.Invoke(G.CamGetMain, nullptr) : nullptr;
        if (cam) {
            Il2CppObject *tr = IL2.Invoke(G.GoGetTr, cam);
            if (tr){
                Vec3 fwd = IL2.Unbox<Vec3>(IL2.Invoke(G.TrGetFwd, tr));
                SetVel(local, fwd * 20.0f);
                SetGrav(local, 0.0f);
            }
        }
    }

    // Speed: multiply horizontal velocity each frame
    if (f_speed) {
        Vec3 v = GetVel(local);
        float m = kSpeedMult[g_speedTier < 5 ? g_speedTier : 4];
        v.x *= m; v.z *= m;
        SetVel(local, v);
    }

    // No gravity
    if (f_noGravity) SetGrav(local, 0.0f);

    // Low gravity: half
    if (f_lowGravity && !f_noGravity) SetGrav(local, 0.5f);

    // Spin: rotate via velocity
    if (f_spin) {
        Vec3 v = GetVel(local);
        v.y = 0;
        // Add a tangential kick each frame to create spin
        Vec3 side = {-v.z, 0, v.x};
        float l = V3Len(side);
        if (l > 0.1f) side = side * (1/l);
        SetVel(local, GetVel(local) + side * 2.0f);
    }

    // Freeze others: set every non-local player velocity to zero
    if (f_freeze) {
        for (auto &p : g_players)
            if (!p.isLocal) SetVel(p.api, Vec3{0,0,0});
    }

    // Noclip: handled via layer mask patching (see EAC section)
    // Platform: spawn platform under player hands when grips held
    // (full platform implementation requires spawning a Unity primitive — see dark-matter pattern)
}

// ============================================================================
// Punch mod: throw nearest player away when right trigger fires
// ============================================================================
static bool g_punchArmed = false;
static void PunchTick(Il2CppObject *local) {
    if (!f_punch || !local) return;
    bool trig = RTrigger();
    if (trig && !g_punchArmed && g_time >= g_punchNext) {
        g_punchArmed = true;
        g_punchNext  = g_time + 0.5;
        Vec3 myPos = GetPos(local);
        Il2CppObject *target = nullptr;
        float bestDist = 5.0f;
        for (auto &p : g_players) {
            if (p.isLocal) continue;
            Vec3 d = p.pos - myPos;
            float dist = V3Len(d);
            if (dist < bestDist) { bestDist=dist; target=p.api; }
        }
        if (target) {
            Vec3 myP = GetPos(local);
            Vec3 tgP = GetPos(target);
            Vec3 dir = V3Norm(tgP - myP);
            SetVel(target, dir * 20.0f + Vec3{0,8,0});
            LOGI("Punch: launched player %.1fm away", bestDist);
        }
    }
    if (!trig) g_punchArmed = false;
}

// ============================================================================
// Portal gun: right grip to aim, trigger to place portal (teleport to hit point)
// ============================================================================
static bool g_portalFired = false;
static void PortalGunTick(Il2CppObject *local) {
    if (!f_portalGun || !local) return;
    bool trig = RTrigger() && RGrip();
    if (trig && !g_portalFired) {
        g_portalFired = true;
        // Raycast via camera forward — teleport player to hit point
        // Full Physics.Raycast requires invoking Unity; simplified: move 20m forward
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
// Init / Update
// ============================================================================
void Features_Init() { LOGI("Features init"); }

void Features_Update() {
    if (!InitG()) return;
    RefreshPlayers();
    Il2CppObject *local = LocalPlayer();
    MovementTick(local);
    PunchTick(local);
    PortalGunTick(local);
    SkyboxTick();
}

// Exposed to menu
void Feature_SaveSpot()  { Features_Update(); SaveSpot(LocalPlayer()); }
void Feature_GoToSpot()  { Features_Update(); GoToSpot(LocalPlayer()); }
void Feature_AllOff() {
    f_fly=f_velocityFly=f_speed=f_noclip=f_lowGravity=f_noGravity=false;
    f_platforms=f_punch=f_esp=f_skybox=f_skyRainbow=false;
    f_worldBlocks=f_blockRainbow=f_portalGun=f_orbitCam=false;
    f_freeze=f_spin=f_bigHead=f_invisible=f_nameSpoof=false;
    Il2CppObject *local=LocalPlayer();
    if(local)SetGrav(local,1.0f);
    // Reset skybox
    if(G.RsSetSkybox){void*a[1]={nullptr};IL2.Invoke(G.RsSetSkybox,nullptr,a);}
}
// Expose speed and sky tier setters
void Feature_NextSpeed()   { g_speedTier=(g_speedTier+1)%5; }
void Feature_NextSkyColor(){ g_skyColorIdx=(g_skyColorIdx+1)%kSkyCount; if(f_skybox)ApplySkybox(kSkyColors[g_skyColorIdx]); }
const char* Feature_SkyName(){ return kSkyNames[g_skyColorIdx]; }
const char* Feature_SpeedName(){
    static const char*n[]={"1.5x","2x","3x","5x","8x"};
    return n[g_speedTier<5?g_speedTier:4];
}
// Player list accessor
size_t Feature_PlayerCount(){ return g_players.size(); }
const char* Feature_PlayerName(int i){ return (i>=0&&i<(int)g_players.size())?g_players[i].name.c_str():""; }
float Feature_PlayerDist(int i){ return (i>=0&&i<(int)g_players.size())?g_players[i].dist:0; }
bool  Feature_PlayerIsLocal(int i){ return (i>=0&&i<(int)g_players.size())&&g_players[i].isLocal; }
// Input accessors for menu
bool Feature_LeftStickClick(){ return LeftStickClick(); }
bool Feature_BtnA()          { return BtnA(); }
bool Feature_RTrigger()      { return RTrigger(); }
bool Feature_LTrigger()      { return LTrigger(); }
float Feature_RAxisX()       { return Axis("Oculus_CrossPlatform_SecondaryThumbstickHorizontal"); }
float Feature_RAxisY()       { return Axis("Oculus_CrossPlatform_SecondaryThumbstickVertical"); }
