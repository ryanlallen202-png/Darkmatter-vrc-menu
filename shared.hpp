#pragma once
// vrc-quest — shared surface for every module in the plugin.
//
// Layout contract:
//   * IL2CPP only, resolved through dlsym on libil2cpp.so exports.
//   * No hardcoded field offsets outside the one Offset{} block below.
//   * Every IL2CPP object that outlives a frame is held by Ref (GC handle).
//   * Every call into managed code is null-guarded and exception-checked.

#include <android/log.h>
#include <jni.h>
#include <dlfcn.h>
#include <unistd.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <functional>

#define TAG  "vrc-quest"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ---- Math ------------------------------------------------------------------
struct Vec2  { float x=0,y=0; };
struct Vec3  { float x=0,y=0,z=0; };
struct Vec4  { float x=0,y=0,z=0,w=0; };
struct Quat  { float x=0,y=0,z=0,w=1; };
struct Color { float r=1,g=1,b=1,a=1; };
struct Matrix4x4 { float m[16]={}; };

inline Vec3  operator+(Vec3 a,Vec3 b){return{a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3  operator-(Vec3 a,Vec3 b){return{a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3  operator*(Vec3 a,float s){return{a.x*s,a.y*s,a.z*s};}
inline Vec3& operator+=(Vec3&a,Vec3 b){a=a+b;return a;}
inline float V3Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline float V3Len(Vec3 v){return sqrtf(V3Dot(v,v));}
inline Vec3  V3Norm(Vec3 v){float l=V3Len(v);return l>1e-5f?v*(1/l):Vec3{0,1,0};}
inline Vec3  V3Cross(Vec3 a,Vec3 b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}

inline Color ColorLerp(Color a,Color b,float t){
    return{a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t,1};
}
inline Color ColorRainbow(float t){
    t=t-floorf(t);
    float r=fabsf(t*6-3)-1,g=2-fabsf(t*6-2),b=2-fabsf(t*6-4);
    auto cl=[](float v){return v<0?0.f:v>1?1.f:v;};
    return{cl(r),cl(g),cl(b),1};
}
inline Color ColorHex(uint32_t rgb,float a=1){
    return{((rgb>>16)&0xFF)/255.f,((rgb>>8)&0xFF)/255.f,(rgb&0xFF)/255.f,a};
}

// ---- IL2CPP ABI ------------------------------------------------------------
// Opaque handles. Nothing here dereferences except the two documented helpers
// in IL2CPPApi (string chars, array data) whose layouts are stable on arm64.
struct Il2CppObject;
struct Il2CppClass;
struct Il2CppType;
struct MethodInfo;
struct Il2CppString;
struct Il2CppArray;
struct Il2CppImage;
struct Il2CppAssembly;
struct Il2CppDomain;

// arm64 IL2CPP object header: klass*, monitor*, then instance fields.
static constexpr size_t kObjHeader   = sizeof(void*) * 2;   // 0x10
// System.String: header, int32 length, then UTF-16 chars.
static constexpr size_t kStrLengthAt = kObjHeader;          // +0x10
static constexpr size_t kStrCharsAt  = kObjHeader + 4;      // +0x14
// System.Array: header, bounds*, uintptr_t max_length, data...
static constexpr size_t kArrLengthAt = kObjHeader + sizeof(void*); // +0x18
static constexpr size_t kArrDataAt   = kObjHeader + sizeof(void*) * 2; // +0x20

// ---- Runtime field offsets (single block, FILL from your dump) --------------
// Nothing else in the tree may carry a raw offset constant.
namespace Offset {
    // VRCPlayer.isLocal — bool, relative to the VRCPlayer object.
    // FILL: Il2CppDumper → VRCPlayer fields, grep isLocal / isLocalPlayer.
    static constexpr int kIsLocal = 0x00;   // 0x00 = unresolved, feature refuses to run
    // VRCPlayer.Descriptor / VRCPlayerApi backing pointer if your build splits them.
    // FILL: only needed if get_displayName returns empty on your build.
    static constexpr int kDescriptor = 0x00;
}

// ---- Resolved function pointers -------------------------------------------
typedef Il2CppObject *(*fn_object_new)(Il2CppClass*);
typedef Il2CppClass  *(*fn_class_from_name)(Il2CppImage*,const char*,const char*);
typedef Il2CppDomain *(*fn_domain_get)();
typedef Il2CppAssembly **(*fn_domain_assemblies)(Il2CppDomain*,size_t*);
typedef Il2CppImage  *(*fn_assembly_image)(Il2CppAssembly*);
typedef const char   *(*fn_image_name)(Il2CppImage*);
typedef MethodInfo   *(*fn_method_from_name)(Il2CppClass*,const char*,int);
typedef Il2CppObject *(*fn_runtime_invoke)(MethodInfo*,void*,void**,void**);
typedef Il2CppString *(*fn_string_new)(const char*);
typedef Il2CppClass  *(*fn_object_class)(Il2CppObject*);
typedef uint32_t      (*fn_gchandle_new)(Il2CppObject*,bool);
typedef Il2CppObject *(*fn_gchandle_target)(uint32_t);
typedef void          (*fn_gchandle_free)(uint32_t);
typedef Il2CppArray  *(*fn_array_new)(Il2CppClass*,uintptr_t);
typedef Il2CppObject *(*fn_value_box)(Il2CppClass*,void*);
typedef Il2CppType   *(*fn_class_get_type)(Il2CppClass*);
typedef Il2CppClass  *(*fn_class_get_parent)(Il2CppClass*);
typedef void          (*fn_free)(void*);
typedef Il2CppObject *(*fn_reflection_type)(Il2CppType*); // il2cpp_type_get_object
typedef void         *(*fn_field_from_name)(Il2CppClass*,const char*);
typedef int32_t       (*fn_field_offset)(void*);

struct IL2CPPApi {
    fn_object_new      object_new    = nullptr;
    fn_class_from_name class_from    = nullptr;
    fn_domain_get      domain_get    = nullptr;
    fn_domain_assemblies assemblies  = nullptr;
    fn_assembly_image  img           = nullptr;
    fn_image_name      image_name    = nullptr;
    fn_method_from_name method       = nullptr;
    fn_runtime_invoke  invoke        = nullptr;
    fn_string_new      str_new       = nullptr;
    fn_object_class    obj_class     = nullptr;
    fn_gchandle_new    gc_new        = nullptr;
    fn_gchandle_target gc_target     = nullptr;
    fn_gchandle_free   gc_free       = nullptr;
    fn_array_new       arr_new       = nullptr;
    fn_value_box       val_box       = nullptr;
    fn_class_get_type  class_type    = nullptr;
    fn_class_get_parent class_parent = nullptr;
    fn_reflection_type type_object   = nullptr;
    fn_field_from_name field_from    = nullptr;
    fn_field_offset    field_off     = nullptr;
    bool ready = false;

    bool Init();                                        // source of truth: dlsym
    Il2CppImage* GetImage(const char* assembly);
    Il2CppClass* GetClass(const char* ns,const char* name,const char* assembly="Assembly-CSharp");
    MethodInfo*  GetMethod(Il2CppClass* cls,const char* name,int argc=-1);
    Il2CppObject* Invoke(MethodInfo* m,void* obj,void** args=nullptr);
    // System.Type* for AddComponent(Type) / GetComponent(Type).
    Il2CppObject* TypeOf(Il2CppClass* cls);
    // Field offset resolved by name from the live dump — never a literal.
    // Returns -1 when the field does not exist on this build.
    int FieldOffset(Il2CppClass* cls, const char* name);

    template<typename T> static T Unbox(Il2CppObject* o){
        return o ? *(T*)((uint8_t*)o+kObjHeader) : T{};
    }
    std::string StrGet(Il2CppString* s);
    Il2CppString* StrNew(const char* s){ return str_new?str_new(s):nullptr; }
};
extern IL2CPPApi IL2;

// ---- Ref (owning GC handle) -------------------------------------------------
// Copyable on purpose: handles are process-wide; copying does not duplicate.
struct Ref {
    uint32_t h = 0;
    Ref() = default;
    Ref(Il2CppObject* o){ set(o); }
    Ref(const Ref& o) : h(o.h) {}
    Ref& operator=(const Ref& o){ if(this!=&o){clear();h=o.h;} return *this; }
    Ref(Ref&& o) noexcept : h(o.h){ o.h=0; }
    Ref& operator=(Ref&& o) noexcept { if(this!=&o){clear();h=o.h;o.h=0;} return *this; }
    void set(Il2CppObject* o){ clear(); if(o&&IL2.gc_new) h=IL2.gc_new(o,false); }
    Il2CppObject* get() const { return (h&&IL2.gc_target)?IL2.gc_target(h):nullptr; }
    void clear(){ if(h&&IL2.gc_free) IL2.gc_free(h); h=0; }
    bool alive() const;
    explicit operator bool() const { return get()!=nullptr; }
    Il2CppObject* operator->() const { return get(); }
};

// ---- Player cache ----------------------------------------------------------
// One vector, refreshed in Features_Update at 2 Hz, read by ESP, menu, gunlib.
// api is a Ref so a player leaving mid-frame cannot dangle the handle.
struct PlayerInfo {
    Ref         api;
    std::string name;
    Vec3        pos;
    float       dist   = 0;
    bool        isLocal= false;
    int         actor  = 0;      // Photon ActorNumber, 0 = unresolved
    int         playerId = 0;
};
extern std::vector<PlayerInfo> g_players;
extern int g_localActor;

// ---- Patch helpers ---------------------------------------------------------
bool  Patch(uintptr_t addr, const uint8_t* bytes, size_t len);
void  NopRange(uintptr_t addr, int count);
void  RetPatch(uintptr_t addr);
void  RetTrue(uintptr_t addr);
uintptr_t GetLibBase(const char* libname);

// ---- Global time -----------------------------------------------------------
extern double g_time;
extern float  g_dt;

// ---- Photon bridge (photon.hpp) -------------------------------------------
// Declared here so every module can broadcast without including the bridge.
namespace Photon {
    bool  Init();
    bool  Ready();
    int   LocalActor();
    int   PlayerCount();
    int   ActorAt(int idx);                 // nth player in room, 0 if out of range
    bool  Raise(uint8_t code, const void* payload, int len, int targetActor = -1);
    bool  RaiseRaw(uint8_t code, void* managedObj, int targetActor = -1);
    bool  IsMaster();
    bool  Kick(int actor);                  // master-client path only
    bool  SetActorProperties(int actor, void* managedHashtable);
    void* HashtableClass();                 // System.Collections.Hashtable
    void* ByteClass();                      // System.Byte (for byte[] alloc)
    void  Shutdown();
}

// ---- Photon event pump (event_pump.cpp) -----------------------------------
namespace EventPump {
    bool Attach();
    void Detach();
    bool Active();
    uint64_t EventsSeen();
    uint64_t AudioFrames();
    uint64_t Dropped();
}

// ---- Inline hook engine (hook.cpp) ---------------------------------------
// Fail-closed: refuses any target whose first 4 instructions are PC-relative.
bool  HookAttach(uintptr_t target, void* detour, void** original);
bool  HookDetach(uintptr_t target);
bool  HookDetachByDetour(void* detour);

// ---- Audio decode (audio_decode.cpp) -------------------------------------
struct DecodedAudio {
    std::vector<float> pcm;      // mono, 44100 Hz, [-1,1]
    int  sampleRate = 44100;
    int  channels   = 1;
    bool ok = false;
};
// WAV: native parser (PCM 8/16/24/32 + float). Everything else: Android
// MediaExtractor + MediaCodec through JNI, then linear-resampled to 44.1k mono.
bool DecodeAudioFile(const char* path, DecodedAudio& out);
extern JavaVM* g_jvm;

// ---- Soundboard (soundboard.cpp) -----------------------------------------
namespace Soundboard {
    void  Init();
    void  Shutdown();
    void  Update();
    int   Count();
    const char* Name(int idx);
    bool  PlayLocal(int idx);
    bool  PlayNetwork(int idx);
    void  StopAll();
    bool  AddFile(const char* path);
    int   FileCount();
    const char* FileError();
}
