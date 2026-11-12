// soundboard.cpp — C++17, arm64-v8a
// Plays audio clips via VRChat's AudioSource / AudioClip IL2CPP layer.
// Clips are loaded from the APK's streaming assets at runtime.
// FILL: replace kClipPaths[] with real paths from your APK streaming assets bundle.
#include "shared.hpp"
#include <cstring>

// ============================================================================
// AudioSource class cache
// ============================================================================
static struct {
    bool ready=false;
    Il2CppClass *AudioSource,*AudioClip,*GameObject,*Transform,*Camera;
    MethodInfo  *AsPlay,*AsStop,*AsSetClip,*AsSetVol,*AsSetSpatial,
                *AsNew,          // AudioSource.PlayClipAtPoint(clip,pos,vol)
                *AcLoadPath,     // WWW / UnityWebRequest — not wired; use cache
                *CamGetMain,
                *GoGetTr,*GoAddComp,*TrGetPos;
} A;

static bool InitAudio(){
    if (A.ready) return true;
    if (!IL2.ready) return false;
#define C(v,ns,n,a) A.v=IL2.GetClass(ns,n,a);
#define M(v,c,n,argc) A.v=IL2.GetMethod(A.c,n,argc);
    C(AudioSource,"UnityEngine","AudioSource","UnityEngine")
    C(AudioClip,  "UnityEngine","AudioClip",  "UnityEngine")
    C(GameObject, "UnityEngine","GameObject", "UnityEngine")
    C(Transform,  "UnityEngine","Transform",  "UnityEngine")
    C(Camera,     "UnityEngine","Camera",     "UnityEngine")

    M(AsPlay,       AudioSource,"Play",         0)
    M(AsStop,       AudioSource,"Stop",         0)
    M(AsSetClip,    AudioSource,"set_clip",     1)
    M(AsSetVol,     AudioSource,"set_volume",   1)
    M(AsSetSpatial, AudioSource,"set_spatialBlend",1)
    M(CamGetMain,   Camera,     "get_main",     0)
    M(GoGetTr,      GameObject, "get_transform",0)
    M(GoAddComp,    GameObject, "AddComponent", 1)
    M(TrGetPos,     Transform,  "get_position", 0)
#undef C
#undef M
    // PlayClipAtPoint(AudioClip, Vector3, float) — static on AudioSource
    A.AsNew=IL2.GetMethod(A.AudioSource,"PlayClipAtPoint",3);
    A.ready=A.AudioSource&&A.AsPlay&&A.AsSetClip&&A.AudioClip;
    LOGI("Audio %s",A.ready?"ready":"INCOMPLETE");
    return A.ready;
}

// ============================================================================
// Clip table
// FILL: replace paths with actual streaming-assets clip names from your APK
// ============================================================================
static const char *kClipPaths[]={
    "laugh",          // 0 — FILL: map to actual AudioClip name in assets
    "vine_boom",      // 1
    "rizz",           // 2
    "ohio",           // 3
    "skibidi",        // 4
    "npc",            // 5
    "emotional_dmg",  // 6
    "ratio",          // 7
    "sus",            // 8
    "yap",            // 9
    "brainrot",       // 10
    "slay",           // 11
};
static constexpr int kClipCount=12;

static const char *kClipDisplayNames[]={
    "Laugh","Vine Boom","Rizz","Ohio","Skibidi","NPC",
    "Emotional Damage","Ratio","Sus","Yap","Brainrot","Slay",
};

// ============================================================================
// Clip cache — load via Resources.Load<AudioClip> at first play
// ============================================================================
static Ref g_clipRefs[kClipCount];

static Il2CppObject *LoadClip(int idx){
    if (idx<0||idx>=kClipCount) return nullptr;
    if (g_clipRefs[idx].alive()) return g_clipRefs[idx].get();

    // Resources.Load<AudioClip>(path)
    Il2CppClass *resCls=IL2.GetClass("UnityEngine","Resources","UnityEngine");
    if (!resCls) return nullptr;
    // Load(string, System.Type) overload
    MethodInfo *load=IL2.GetMethod(resCls,"Load",2);
    if (!load) load=IL2.GetMethod(resCls,"Load",1);
    if (!load) return nullptr;

    Il2CppObject *clip=nullptr;
    if (IL2.class_type&&A.AudioClip){
        void *type=IL2.class_type(A.AudioClip);
        void *a[2]={IL2.StrNew(kClipPaths[idx]),(void*)type};
        clip=IL2.Invoke(load,nullptr,a);
    } else {
        void *a[1]={IL2.StrNew(kClipPaths[idx])};
        clip=IL2.Invoke(load,nullptr,a);
    }
    if (clip) g_clipRefs[idx].set(clip);
    LOGI("Clip %d '%s': %s",idx,kClipPaths[idx],clip?"ok":"null");
    return clip;
}

// ============================================================================
// Persistent AudioSource GO (single global source, non-spatial)
// ============================================================================
static Ref g_audioSourceRef;
static Il2CppObject *GetOrCreateSource(){
    if (g_audioSourceRef.alive()) return g_audioSourceRef.get();
    // Create a new GO and attach AudioSource
    Il2CppClass *goCls=IL2.GetClass("UnityEngine","GameObject","UnityEngine");
    if (!goCls||!IL2.object_new) return nullptr;
    Il2CppObject *go=IL2.object_new(goCls);
    MethodInfo *ctor=IL2.GetMethod(goCls,".ctor",1);
    if (ctor){void*a[1]={IL2.StrNew("SoundboardSource")};IL2.Invoke(ctor,go,a);}
    // DontDestroyOnLoad
    Il2CppClass *obj=IL2.GetClass("UnityEngine","Object","UnityEngine");
    MethodInfo *ddol=obj?IL2.GetMethod(obj,"DontDestroyOnLoad",1):nullptr;
    if (ddol){void*a[1]={go};IL2.Invoke(ddol,nullptr,a);}
    // AddComponent<AudioSource>
    Il2CppObject *src=nullptr;
    if (A.GoAddComp&&IL2.class_type){
        void *type=IL2.class_type(A.AudioSource);
        src=IL2.Invoke(A.GoAddComp,go,(void**)&type);
    }
    if (!src) return nullptr;
    // Set non-spatial (2D), volume 1.0
    if (A.AsSetSpatial){float sp=0.0f;void*a[1]={&sp};IL2.Invoke(A.AsSetSpatial,src,a);}
    if (A.AsSetVol)    {float v =1.0f;void*a[1]={&v }; IL2.Invoke(A.AsSetVol,   src,a);}
    g_audioSourceRef.set(go);
    LOGI("SoundboardSource created");
    return src;
}

static Il2CppObject *GetSourceComp(){
    Il2CppObject *go=g_audioSourceRef.get();
    if (!go) return nullptr;
    Il2CppClass *goCls=IL2.GetClass("UnityEngine","GameObject","UnityEngine");
    MethodInfo  *gc  =IL2.GetMethod(goCls,"GetComponent",1);
    if (!gc||!IL2.class_type) return nullptr;
    void *type=IL2.class_type(A.AudioSource);
    return IL2.Invoke(gc,go,(void**)&type);
}

// ============================================================================
// Public API
// ============================================================================
void Soundboard_Init(){
    LOGI("Soundboard init");
    if (!InitAudio()) return;
    GetOrCreateSource();
    // Pre-warm clip cache
    for (int i=0;i<kClipCount;i++) LoadClip(i);
}

void Soundboard_Play(int idx){
    if (!InitAudio()) return;
    if (idx<0||idx>=kClipCount) return;
    Il2CppObject *clip=LoadClip(idx);
    if (!clip){LOGE("Clip %d not loaded",idx);return;}
    Il2CppObject *src=GetOrCreateSource();
    if (!src) src=GetSourceComp();
    if (!src){
        // Fallback: PlayClipAtPoint at local position
        if (!A.AsNew) return;
        Il2CppClass *camCls=IL2.GetClass("UnityEngine","Camera","UnityEngine");
        MethodInfo  *gm   =camCls?IL2.GetMethod(camCls,"get_main",0):nullptr;
        Vec3 pos={0,0,0};
        if (gm){
            Il2CppObject *cam=IL2.Invoke(gm,nullptr);
            if (cam){
                MethodInfo *gtr=IL2.GetMethod(A.GameObject,"get_transform",0);
                MethodInfo *gp =IL2.GetMethod(A.Transform, "get_position", 0);
                if (gtr&&gp){
                    Il2CppObject *tr=IL2.Invoke(gtr,cam);
                    pos=IL2.Unbox<Vec3>(IL2.Invoke(gp,tr));
                }
            }
        }
        float vol=1.0f;
        void*a[3]={clip,&pos,&vol};
        IL2.Invoke(A.AsNew,nullptr,a);
        LOGI("PlayClipAtPoint clip %d",idx);
        return;
    }
    if (A.AsStop)   IL2.Invoke(A.AsStop,  src);
    if (A.AsSetClip){void*a[1]={clip};IL2.Invoke(A.AsSetClip,src,a);}
    if (A.AsPlay)   IL2.Invoke(A.AsPlay,  src);
    LOGI("Soundboard play clip %d '%s'",idx,kClipDisplayNames[idx]);
}

int         Soundboard_Count()    { return kClipCount; }
const char* Soundboard_Name(int i){ return (i>=0&&i<kClipCount)?kClipDisplayNames[i]:""; }
