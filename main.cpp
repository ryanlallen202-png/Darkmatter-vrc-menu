// vrc-quest v2 — VRChat Meta Quest mod
// Open menu: press LEFT JOYSTICK DOWN (L3)
// Menu anchor: left hand, cursor: right hand index finger
// Tabs: Player | Exploits | Visual | Players | Soundboard | Text | Misc | Config
// FILL: verify package name com.vrchat.mobile.android on your build

#include "shared.hpp"
#include <jni.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <ctime>

// ---- globals ---------------------------------------------------------------
IL2CPPApi IL2;
double    g_time = 0;
float     g_dt   = 0;

// ---- IL2CPPApi impl --------------------------------------------------------
bool IL2CPPApi::Init() {
    void *h = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    if (!h) h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { LOGE("dlopen libil2cpp.so: %s", dlerror()); return false; }
#define L(field,sym) field=(decltype(field))dlsym(h,"il2cpp_"#sym); if(!field){LOGE("missing il2cpp_"#sym);}
    L(object_new,  object_new)
    L(class_from,  class_from_name)
    L(domain_get,  domain_get)
    L(assemblies,  domain_get_assemblies)
    L(img,         assembly_get_image)
    L(method,      class_get_method_from_name)
    L(invoke,      runtime_invoke)
    L(str_new,     string_new)
    L(obj_class,   object_get_class)
    L(gc_new,      gchandle_new)
    L(gc_target,   gchandle_get_target)
    L(gc_free,     gchandle_free)
    L(arr_new,     array_new)
    L(val_box,     value_box)
#undef L
    ready = object_new && class_from && domain_get && assemblies && img && method && invoke && str_new;
    LOGI("IL2CPP %s", ready ? "ready" : "INCOMPLETE");
    return ready;
}

void* IL2CPPApi::GetImage(const char* assembly) {
    if (!ready) return nullptr;
    void *domain = domain_get();
    size_t count = 0;
    void **asms = assemblies(domain, &count);
    for (size_t i = 0; i < count; i++) {
        void *image = img(asms[i]);
        if (!image) continue;
        const char *name = *(const char**)((uint8_t*)image + 0x18);
        if (name && strstr(name, assembly)) return image;
    }
    return nullptr;
}

Il2CppClass* IL2CPPApi::GetClass(const char* ns, const char* name, const char* assembly) {
    void *image = GetImage(assembly);
    if (!image) return nullptr;
    return class_from(image, ns, name);
}

MethodInfo* IL2CPPApi::GetMethod(Il2CppClass* cls, const char* name, int argc) {
    if (!cls || !method) return nullptr;
    return method(cls, name, argc);
}

Il2CppObject* IL2CPPApi::Invoke(MethodInfo* m, void* obj, void** args) {
    if (!m || !invoke) return nullptr;
    void *ex = nullptr;
    Il2CppObject *r = invoke(m, obj, args, &ex);
    if (ex) { LOGE("invoke ex on %p", (void*)m); return nullptr; }
    return r;
}

std::string IL2CPPApi::StrGet(Il2CppString* s) {
    if (!s) return "";
    int32_t len = *(int32_t*)((uint8_t*)s + 16);
    if (len <= 0 || len > 1024) return "";
    char buf[2048];
    uint16_t *chars = (uint16_t*)((uint8_t*)s + 20);
    int n = len < 1023 ? len : 1023;
    for (int i = 0; i < n; i++) buf[i] = (char)(chars[i] & 0x7f);
    buf[n] = 0;
    return buf;
}

bool Ref::alive() const {
    Il2CppObject *o = get();
    if (!o) return false;
    // UnityEngine.Object.op_Implicit check via obj_class != nullptr
    return IL2.obj_class ? (IL2.obj_class(o) != nullptr) : true;
}

// ---- Patch helpers ---------------------------------------------------------
bool Patch(uintptr_t addr, const uint8_t* bytes, size_t len) {
    uintptr_t page = addr & ~(uintptr_t)(getpagesize()-1);
    if (mprotect((void*)page, len+(addr-page), PROT_READ|PROT_WRITE|PROT_EXEC) != 0) return false;
    memcpy((void*)addr, bytes, len);
    __builtin___clear_cache((char*)addr, (char*)(addr+len));
    return true;
}
void NopRange(uintptr_t addr, int count) {
    static const uint8_t NOP[4]={0x1F,0x20,0x03,0xD5};
    for (int i=0;i<count;i++) Patch(addr+i*4,NOP,4);
    LOGI("NOP'd %d @ 0x%zx", count, (size_t)addr);
}
void RetPatch(uintptr_t addr) {
    static const uint8_t RET[4]={0xC0,0x03,0x5F,0xD6};
    Patch(addr,RET,4);
    LOGI("RET @ 0x%zx",(size_t)addr);
}
void RetTrue(uintptr_t addr) {
    static const uint8_t SEQ[8]={0x20,0x00,0x80,0x52,0xC0,0x03,0x5F,0xD6};
    Patch(addr,SEQ,8);
    LOGI("RetTrue @ 0x%zx",(size_t)addr);
}
uintptr_t GetLibBase(const char* libname) {
    FILE *f=fopen("/proc/self/maps","r");
    if(!f)return 0;
    char line[512]; uintptr_t base=0;
    while(fgets(line,sizeof line,f))
        if(strstr(line,libname)&&strstr(line,"r-xp"))
            {base=(uintptr_t)strtoull(line,nullptr,16);break;}
    fclose(f);
    return base;
}

// ---- Forward decls for modules ---------------------------------------------
void Menu_Init();
void Menu_Update();
void Features_Init();
void Features_Update();
void ESP_Init();
void ESP_Update();
void Soundboard_Init();
void PatchEAC(uintptr_t base);

// ---- Update thread ---------------------------------------------------------
static bool g_running = false;

static void* UpdateThread(void*) {
    LOGI("Update thread started");
    for (int i=0;i<120&&!IL2.ready;i++){IL2.Init();usleep(500000);}
    if (!IL2.ready){LOGE("IL2CPP never ready");return nullptr;}

    uintptr_t base=GetLibBase("libil2cpp.so");
    if(base) PatchEAC(base);

    Features_Init();
    ESP_Init();
    Soundboard_Init();
    Menu_Init();

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    double last=ts.tv_sec+ts.tv_nsec*1e-9;

    while(g_running){
        clock_gettime(CLOCK_MONOTONIC,&ts);
        double now=ts.tv_sec+ts.tv_nsec*1e-9;
        g_dt=(float)(now-last); last=now; g_time+=g_dt;

        Features_Update();
        ESP_Update();
        Menu_Update();

        usleep(16667); // ~60Hz
    }
    return nullptr;
}

// ---- JNI entry -------------------------------------------------------------
extern "C" __attribute__((visibility("default")))
jint JNI_OnLoad(JavaVM*, void*) {
    LOGI("vrc-quest v2 loading");
    g_running=true;
    pthread_t tid;
    pthread_create(&tid,nullptr,UpdateThread,nullptr);
    pthread_detach(tid);
    return JNI_VERSION_1_6;
}
extern "C" __attribute__((visibility("default")))
void JNI_OnUnload(JavaVM*,void*) { g_running=false; }
