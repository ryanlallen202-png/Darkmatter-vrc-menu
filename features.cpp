// features.cpp — C++17, arm64-v8a
// FILL: EAC offsets at namespace EACOff — dump from IDA/Ghidra on your VRC build
#include "shared.hpp"
#include <cstdlib>

// ============================================================================
// VRChat class cache
// ============================================================================
static struct VRCClasses {
    bool ready = false;
    Il2CppClass *PlayerApi, *Networking, *RenderSettings,
                *Camera,    *GameObject,  *Transform,
                *Material,  *Renderer,    *Shader,
                *Input;
    MethodInfo  *PlayerGetPos,   *PlayerSetVel,  *PlayerGetVel,
                *PlayerGetName,  *PlayerIsLocal, *PlayerGetId,
                *PlayerSetGrav,  *PlayerTeleport,*PlayerGetAll,
                *NetLocalPlayer,
                *CamGetMain,
                *TrGetPos,  *TrSetPos, *TrGetRot,
                *TrGetFwd,  *TrGetRight, *TrGetUp,
                *GoGetTr,   *GoGetComp,  *GoSetActive,
                *MatSetColor,*MatSetShader,*ShaderFind,
                *RsGetSkybox,*RsSetSkybox,
                *InputGetAxis,*InputGetButton,*InputGetButtonDown;
} G;

static bool InitG() {
    if (G.ready) return true;
    if (!IL2.ready) return false;
#define C(v,ns,n,a) G.v=IL2.GetClass(ns,n,a);
#define M(v,c,n,argc) G.v=IL2.GetMethod(G.c,n,argc);
    C(PlayerApi,     "",             "VRCPlayerApi",    "Assembly-CSharp")
    C(Networking,    "",             "Networking",      "Assembly-CSharp")
    C(Camera,        "UnityEngine",  "Camera",          "UnityEngine")
    C(GameObject,    "UnityEngine",  "GameObject",      "UnityEngine")
    C(Transform,     "UnityEngine",  "Transform",       "UnityEngine")
    C(Material,      "UnityEngine",  "Material",        "UnityEngine")
    C(Renderer,      "UnityEngine",  "Renderer",        "UnityEngine")
    C(Shader,        "UnityEngine",  "Shader",          "UnityEngine")
    C(Input,         "UnityEngine",  "Input",           "UnityEngine")
    C(RenderSettings,"UnityEngine",  "RenderSettings",  "UnityEngine")

    M(PlayerGetPos,      PlayerApi,  "get_position",         0)
    M(PlayerSetVel,      PlayerApi,  "SetVelocity",          1)
    M(PlayerGetVel,      PlayerApi,  "GetVelocity",          0)
    M(PlayerGetName,     PlayerApi,  "get_displayName",      0)
    M(PlayerIsLocal,     PlayerApi,  "get_isLocal",          0)
    M(PlayerGetId,       PlayerApi,  "get_playerId",         0)
    M(PlayerSetGrav,     PlayerApi,  "SetGravityStrength",   1)
    M(PlayerTeleport,    PlayerApi,  "TeleportTo",           1)
    M(PlayerGetAll,      PlayerApi,  "GetAllPlayers",        0)
    M(NetLocalPlayer,    Networking, "get_LocalPlayer",      0)
    M(CamGetMain,        Camera,     "get_main",             0)
    M(TrGetPos,          Transform,  "get_position",         0)
    M(TrSetPos,          Transform,  "set_position",         1)
    M(TrGetRot,          Transform,  "get_rotation",         0)
    M(TrGetFwd,          Transform,  "get_forward",          0)
    M(TrGetRight,        Transform,  "get_right",            0)
    M(TrGetUp,           Transform,  "get_up",               0)
    M(GoGetTr,           GameObject, "get_transform",        0)
    M(GoGetComp,         GameObject, "GetComponent",         1)
    M(GoSetActive,       GameObject, "SetActive",            1)
    M(MatSetColor,       Material,   "set_color",            1)
    M(MatSetShader,      Material,   "set_shader",           1)
    M(ShaderFind,        Shader,     "Find",                 1)
    M(RsGetSkybox,       RenderSettings,"get_skybox",        0)
    M(RsSetSkybox,       RenderSettings,"set_skybox",        1)
    M(InputGetAxis,      Input,      "GetAxis",              1)
    M(InputGetButton,    Input,      "GetButton",            1)
    M(InputGetButtonDown,Input,      "GetButtonDown",        1)
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
    if (!G.Input||!G.InputGetButton) return false;
    void *a[1]={IL2.StrNew(name)};
    Il2CppObject *r=IL2.Invoke(G.InputGetButton,nullptr,a);
    return r?IL2.Unbox<int32_t>(r)!=0:false;
}
static bool BtnDown(const char *name) {
    if (!G.Input||!G.InputGetButtonDown) return false;
    void *a[1]={IL2.StrNew(name)};
    Il2CppObject *r=IL2.Invoke(G.InputGetButtonDown,nullptr,a);
    return r?IL2.Unbox<int32_t>(r)!=0:false;
}
static float Axis(const char *name) {
    if (!G.Input||!G.InputGetAxis) return 0.f;
    void *a[1]={IL2.StrNew(name)};
    Il2CppObject *r=IL2.Invoke(G.InputGetAxis,nullptr,a);
    return r?IL2.Unbox<float>(r):0.f;
}
static bool LeftStickClick() { return BtnDown("Fire3")||BtnDown("joystick button 8"); }
static bool BtnA()     { return Btn("Fire1")||Btn("joystick button 0"); }
static bool BtnB()     { return Btn("Fire2")||Btn("joystick button 1"); }
static bool RTrigger() { return Axis("Oculus_CrossPlatform_SecondaryIndexTrigger")>0.7f; }
static bool LTrigger() { return Axis("Oculus_CrossPlatform_PrimaryIndexTrigger")>0.7f; }
static bool RGrip()    { return Axis("Oculus_CrossPlatform_SecondaryHandTrigger")>0.7f; }
static bool LGrip()    { return Axis("Oculus_CrossPlatform_PrimaryHandTrigger")>0.7f; }

// ============================================================================
// Player accessors
// ============================================================================
static Il2CppObject *LocalPlayer() {
    return G.NetLocalPlayer?IL2.Invoke(G.NetLocalPlayer,nullptr):nullptr;
}
static Vec3 GetPos(Il2CppObject *api) {
    return IL2.Unbox<Vec3>(IL2.Invoke(G.PlayerGetPos,api));
}
static Vec3 GetVel(Il2CppObject *api) {
    return IL2.Unbox<Vec3>(IL2.Invoke(G.PlayerGetVel,api));
}
static void SetVel(Il2CppObject *api, Vec3 v) {
    void *a[1]={&v}; IL2.Invoke(G.PlayerSetVel,api,a);
}
static void SetGrav(Il2CppObject *api, float g) {
    void *a[1]={&g};
    if (G.PlayerSetGrav) IL2.Invoke(G.PlayerSetGrav,api,a);
}
static void TeleportTo(Il2CppObject *api, Vec3 pos) {
    void *a[1]={&pos};
    if (G.PlayerTeleport) IL2.Invoke(G.PlayerTeleport,api,a);
}

// ============================================================================
// Player list (shared with esp.cpp)
// ============================================================================
struct PlayerInfo {
    Il2CppObject *api;
    std::string name;
    Vec3 pos;
    float dist;
    bool isLocal;
    int id;
};
std::vector<PlayerInfo> g_players;

static double g_nextRefresh=0;
static void RefreshPlayers() {
    if (!G.PlayerGetAll||g_time<g_nextRefresh) return;
    g_nextRefresh=g_time+0.5;
    Il2CppArray *arr=(Il2CppArray*)IL2.Invoke(G.PlayerGetAll,nullptr);
    if (!arr) return;
    size_t n=*(uintptr_t*)((uint8_t*)arr+0x18);
    n=n>80?80:n;
    Il2CppObject *local=LocalPlayer();
    Vec3 lp=local?GetPos(local):Vec3{};
    g_players.clear();
    for (size_t i=0;i<n;i++){
        Il2CppObject *api=((Il2CppObject**)((uint8_t*)arr+0x20))[i];
        if (!api) continue;
        PlayerInfo p;
        p.api=api;
        p.name=IL2.StrGet((Il2CppString*)IL2.Invoke(G.PlayerGetName,api));
        p.pos=GetPos(api);
        Vec3 d=p.pos-lp; p.dist=V3Len(d);
        Il2CppObject *lo=IL2.Invoke(G.PlayerIsLocal,api);
        p.isLocal=lo?IL2.Unbox<int32_t>(lo)!=0:false;
        Il2CppObject *id=IL2.Invoke(G.PlayerGetId,api);
        p.id=id?IL2.Unbox<int32_t>(id):0;
        g_players.push_back(p);
    }
}

// ============================================================================
// Feature flags
// ============================================================================
bool f_fly=false,f_velocityFly=false,f_speed=false,f_noclip=false;
bool f_lowGravity=false,f_noGravity=false,f_platforms=false;
bool f_punch=false,f_esp=false,f_skybox=false,f_skyRainbow=false;
bool f_worldBlocks=false,f_blockRainbow=false,f_portalGun=false,f_orbitCam=false;
bool f_freeze=false,f_spin=false,f_bigHead=false,f_invisible=false;
bool f_nameSpoof=false,f_antiEac=true;
int  g_speedTier=1,g_skyColorIdx=0;

static const float kFlySpeed[] ={4,8,16,24};
static const float kSpeedMult[]={1.5f,2.f,3.f,5.f,8.f};
static Vec3  g_savedSpot{};
static bool  g_hasSavedSpot=false;
static double g_punchNext=0,g_skyNext=0;

// ============================================================================
// Skybox
// ============================================================================
static const Color kSkyColors[10]={
    {0.03f,0.03f,0.10f,1},{0.10f,0.00f,0.20f,1},{0.60f,0.10f,0.05f,1},
    {0.00f,0.25f,0.10f,1},{0.05f,0.15f,0.50f,1},{0.80f,0.50f,0.10f,1},
    {0.00f,0.00f,0.00f,1},{1.00f,1.00f,1.00f,1},{0.00f,0.80f,0.80f,1},
    {0.90f,0.00f,0.90f,1}
};
static const char *kSkyNames[10]={
    "Midnight","Void Purple","Blood Red","Deep Forest","Ocean Night",
    "Golden Hour","Black","White","Neon Cyan","Neon Pink"
};

static Il2CppObject *g_skyMat=nullptr;
static Ref g_skyMatRef;

static void ApplySkybox(Color c) {
    if (!G.RsSetSkybox) return;
    if (!g_skyMatRef.alive()&&G.Material&&G.ShaderFind){
        const char *shaders[]={"Skybox/Panoramic","Unlit/Color",
                               "Universal Render Pipeline/Unlit",nullptr};
        Il2CppObject *sh=nullptr;
        for (int i=0;shaders[i]&&!sh;i++){
            void*a[1]={IL2.StrNew(shaders[i])};
            sh=IL2.Invoke(G.ShaderFind,nullptr,a);
        }
        if (sh){
            g_skyMat=IL2.object_new(G.Material);
            MethodInfo *ctor=IL2.GetMethod(G.Material,".ctor",1);
            if (ctor){void*a[1]={sh};IL2.Invoke(ctor,g_skyMat,a);}
            g_skyMatRef.set(g_skyMat);
        }
    }
    Il2CppObject *mat=g_skyMatRef.get();
    if (!mat) return;
    if (G.MatSetColor){void*a[1]={&c};IL2.Invoke(G.MatSetColor,mat,a);}
    void *a[1]={mat};
    IL2.Invoke(G.RsSetSkybox,nullptr,a);
}

static void SkyboxTick() {
    if ((!f_skybox&&!f_skyRainbow)||g_time<g_skyNext) return;
    g_skyNext=g_time+0.5;
    Color c=f_skyRainbow?ColorRainbow((float)g_time*0.08f):kSkyColors[g_skyColorIdx];
    ApplySkybox(c);
}

// ============================================================================
// EAC patch
// FILL: replace 0x0 with real offsets from IDA/Ghidra on your VRC .so
// ============================================================================
namespace EACOff {
    static constexpr uintptr_t kBeginSession = 0x0; // FILL
    static constexpr uintptr_t kAddNotify    = 0x0; // FILL
    static constexpr uintptr_t kValidateSend = 0x0; // FILL
    static constexpr uintptr_t kIsIntegrity  = 0x0; // FILL
    static constexpr uintptr_t kIsModded     = 0x0; // FILL
}
void PatchEAC(uintptr_t base) {
    if (EACOff::kBeginSession) RetPatch(base+EACOff::kBeginSession);
    if (EACOff::kAddNotify)    RetPatch(base+EACOff::kAddNotify);
    if (EACOff::kValidateSend) NopRange(base+EACOff::kValidateSend,6);
    if (EACOff::kIsIntegrity)  RetTrue (base+EACOff::kIsIntegrity);
    if (EACOff::kIsModded)     RetPatch(base+EACOff::kIsModded);
    void *eos=dlopen("libEOSSDK-Android-Shipping.so",RTLD_NOW|RTLD_NOLOAD);
    if (eos){
        const char *syms[]={"EOS_AntiCheatClient_BeginSession",
                            "EOS_AntiCheatClient_AddNotifyMessageToServer",
                            "EOS_AntiCheatClient_AddNotifyPeerActionRequired",
                            "EOS_AntiCheatClient_ReceiveMessageFromServer",nullptr};
        for (int i=0;syms[i];i++){
            void *s=dlsym(eos,syms[i]);
            if (s) RetPatch((uintptr_t)s);
        }
    }
    LOGI("EAC patches applied");
}

// ============================================================================
// Movement
// ============================================================================
static void MovementTick(Il2CppObject *local) {
    if (!local) return;
    if ((f_fly||f_velocityFly)&&BtnA()){
        Il2CppObject *cam=G.CamGetMain?IL2.Invoke(G.CamGetMain,nullptr):nullptr;
        if (cam){
            Il2CppObject *tr=IL2.Invoke(G.GoGetTr,cam);
            if (tr){
                Vec3 fwd=IL2.Unbox<Vec3>(IL2.Invoke(G.TrGetFwd,tr));
                float spd=f_fly?kFlySpeed[g_speedTier<4?g_speedTier:3]:20.0f;
                SetVel(local,fwd*spd);
                SetGrav(local,0.0f);
            }
        }
    }
    if (f_speed){
        Vec3 v=GetVel(local);
        float m=kSpeedMult[g_speedTier<5?g_speedTier:4];
        v.x*=m; v.z*=m; SetVel(local,v);
    }
    if (f_noGravity)                SetGrav(local,0.0f);
    if (f_lowGravity&&!f_noGravity) SetGrav(local,0.5f);
    if (f_spin){
        Vec3 v=GetVel(local);
        Vec3 side={-v.z,0,v.x};
        float l=V3Len(side);
        if (l>0.1f) side=side*(1/l);
        SetVel(local,GetVel(local)+side*2.0f);
    }
    if (f_freeze)
        for (auto &p:g_players) if (!p.isLocal) SetVel(p.api,Vec3{0,0,0});
}

// ============================================================================
// Punch mod
// ============================================================================
static bool g_punchArmed=false;
static void PunchTick(Il2CppObject *local) {
    if (!f_punch||!local) return;
    bool trig=RTrigger();
    if (trig&&!g_punchArmed&&g_time>=g_punchNext){
        g_punchArmed=true; g_punchNext=g_time+0.5;
        Vec3 myPos=GetPos(local);
        Il2CppObject *target=nullptr; float bestDist=5.0f;
        for (auto &p:g_players){
            if (p.isLocal) continue;
            float dist=V3Len(p.pos-myPos);
            if (dist<bestDist){bestDist=dist;target=p.api;}
        }
        if (target){
            Vec3 dir=V3Norm(GetPos(target)-myPos);
            SetVel(target,dir*20.0f+Vec3{0,8,0});
        }
    }
    if (!trig) g_punchArmed=false;
}

// ============================================================================
// Portal gun
// ============================================================================
static bool g_portalFired=false;
static void PortalGunTick(Il2CppObject *local) {
    if (!f_portalGun||!local) return;
    bool trig=RTrigger()&&RGrip();
    if (trig&&!g_portalFired){
        g_portalFired=true;
        Il2CppObject *cam=G.CamGetMain?IL2.Invoke(G.CamGetMain,nullptr):nullptr;
        if (cam){
            Il2CppObject *tr=IL2.Invoke(G.GoGetTr,cam);
            if (tr){
                Vec3 pos=IL2.Unbox<Vec3>(IL2.Invoke(G.TrGetPos,tr));
                Vec3 fwd=IL2.Unbox<Vec3>(IL2.Invoke(G.TrGetFwd,tr));
                TeleportTo(local,pos+fwd*20.0f);
            }
        }
    }
    if (!trig) g_portalFired=false;
}

// ============================================================================
// Save / load spot
// ============================================================================
static void SaveSpot(Il2CppObject *local) {
    if (!local) return;
    g_savedSpot=GetPos(local); g_hasSavedSpot=true;
}
static void GoToSpot(Il2CppObject *local) {
    if (!local||!g_hasSavedSpot) return;
    TeleportTo(local,g_savedSpot);
}

// ============================================================================
// Init / Update
// ============================================================================
void Features_Init() { LOGI("Features init"); }
void Features_Update() {
    if (!InitG()) return;
    RefreshPlayers();
    Il2CppObject *local=LocalPlayer();
    MovementTick(local);
    PunchTick(local);
    PortalGunTick(local);
    SkyboxTick();
}

void Feature_SaveSpot()  { if(InitG()) SaveSpot(LocalPlayer()); }
void Feature_GoToSpot()  { if(InitG()) GoToSpot(LocalPlayer()); }
void Feature_AllOff() {
    f_fly=f_velocityFly=f_speed=f_noclip=f_lowGravity=f_noGravity=
    f_platforms=f_punch=f_esp=f_skybox=f_skyRainbow=
    f_worldBlocks=f_blockRainbow=f_portalGun=f_orbitCam=
    f_freeze=f_spin=f_bigHead=f_invisible=f_nameSpoof=false;
    Il2CppObject *local=LocalPlayer();
    if (local) SetGrav(local,1.0f);
    if (G.RsSetSkybox){void*a[1]={nullptr};IL2.Invoke(G.RsSetSkybox,nullptr,a);}
}
void Feature_NextSpeed()    { g_speedTier=(g_speedTier+1)%5; }
void Feature_NextSkyColor() {
    g_skyColorIdx=(g_skyColorIdx+1)%10;
    if (f_skybox) ApplySkybox(kSkyColors[g_skyColorIdx]);
}
const char* Feature_SkyName()   { return kSkyNames[g_skyColorIdx]; }
const char* Feature_SpeedName() {
    static const char*n[]={"1.5x","2x","3x","5x","8x"};
    return n[g_speedTier<5?g_speedTier:4];
}
size_t      Feature_PlayerCount()      { return g_players.size(); }
const char* Feature_PlayerName(int i)  {
    return (i>=0&&i<(int)g_players.size())?g_players[i].name.c_str():"";
}
float       Feature_PlayerDist(int i)  {
    return (i>=0&&i<(int)g_players.size())?g_players[i].dist:0;
}
bool        Feature_PlayerIsLocal(int i){
    return (i>=0&&i<(int)g_players.size())&&g_players[i].isLocal;
}
bool  Feature_LeftStickClick() { return LeftStickClick(); }
bool  Feature_BtnA()           { return BtnA(); }
bool  Feature_RTrigger()       { return RTrigger(); }
float Feature_RAxisX() { return Axis("Oculus_CrossPlatform_SecondaryThumbstickHorizontal"); }
float Feature_RAxisY() { return Axis("Oculus_CrossPlatform_SecondaryThumbstickVertical"); }
