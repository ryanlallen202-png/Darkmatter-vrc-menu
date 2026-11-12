#pragma once
#include <android/log.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

#define TAG "darkmatter"
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

// ---- IL2CPP thin layer -----------------------------------------------------
struct Il2CppObject; struct Il2CppClass; struct Il2CppType;
struct MethodInfo;   struct Il2CppString; struct Il2CppArray;

typedef Il2CppObject* (*fn_object_new)(Il2CppClass*);
typedef Il2CppClass*  (*fn_class_from_name)(void*,const char*,const char*);
typedef void*         (*fn_domain_get)();
typedef void**        (*fn_domain_assemblies)(void*,size_t*);
typedef void*         (*fn_assembly_image)(void*);
typedef MethodInfo*   (*fn_method_from_name)(Il2CppClass*,const char*,int);
typedef Il2CppObject* (*fn_runtime_invoke)(MethodInfo*,void*,void**,void**);
typedef Il2CppString* (*fn_string_new)(const char*);
typedef Il2CppClass*  (*fn_object_class)(Il2CppObject*);
typedef void*         (*fn_gchandle_new)(void*,bool);
typedef void*         (*fn_gchandle_target)(void*);
typedef void          (*fn_gchandle_free)(void*);
typedef Il2CppArray*  (*fn_array_new)(Il2CppClass*,uintptr_t);
typedef void*         (*fn_value_box)(Il2CppClass*,void*);
typedef void*         (*fn_class_get_type)(Il2CppClass*);

struct IL2CPPApi {
    fn_object_new        object_new  = nullptr;
    fn_class_from_name   class_from  = nullptr;
    fn_domain_get        domain_get  = nullptr;
    fn_domain_assemblies assemblies  = nullptr;
    fn_assembly_image    img         = nullptr;
    fn_method_from_name  method      = nullptr;
    fn_runtime_invoke    invoke      = nullptr;
    fn_string_new        str_new     = nullptr;
    fn_object_class      obj_class   = nullptr;
    fn_gchandle_new      gc_new      = nullptr;
    fn_gchandle_target   gc_target   = nullptr;
    fn_gchandle_free     gc_free     = nullptr;
    fn_array_new         arr_new     = nullptr;
    fn_value_box         val_box     = nullptr;
    fn_class_get_type    class_type  = nullptr;
    bool ready = false;

    bool          Init();
    void*         GetImage(const char* assembly);
    Il2CppClass*  GetClass(const char* ns, const char* name,
                           const char* assembly="Assembly-CSharp");
    MethodInfo*   GetMethod(Il2CppClass* cls, const char* name, int argc=-1);
    Il2CppObject* Invoke(MethodInfo* m, void* obj, void** args=nullptr);
    template<typename T>
    T Unbox(Il2CppObject* o){ return o?*(T*)((uint8_t*)o+sizeof(void*)*2):T{}; }
    std::string   StrGet(Il2CppString* s);
    Il2CppString* StrNew(const char* s){ return str_new?str_new(s):nullptr; }
    void*         TypeOf(Il2CppClass* cls){
        return (cls&&class_type)?(void*)class_type(cls):nullptr;
    }
};

extern IL2CPPApi IL2;

// ---- Ref (rooted GC handle) ------------------------------------------------
// clear() declared first so set() can call it without forward-ref error
struct Ref {
    void *h = nullptr;

    void clear() {
        if (h && IL2.gc_free) IL2.gc_free(h);
        h = nullptr;
    }
    void set(Il2CppObject* o) {
        clear();
        if (o && IL2.gc_new) h = IL2.gc_new(o, false);
    }
    Il2CppObject* get() const {
        return (h && IL2.gc_target)
            ? (Il2CppObject*)IL2.gc_target(h)
            : nullptr;
    }
    bool alive() const;
};

// ---- Patch helpers ---------------------------------------------------------
bool      Patch(uintptr_t addr, const uint8_t* bytes, size_t len);
void      NopRange(uintptr_t addr, int count);
void      RetPatch(uintptr_t addr);
void      RetTrue(uintptr_t addr);
uintptr_t GetLibBase(const char* libname);

// ---- Global time -----------------------------------------------------------
extern double g_time;
extern float  g_dt;
