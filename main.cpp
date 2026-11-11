// vrc-quest — runtime entry.
//
// Open menu: L3 (left joystick click). Tabs: Player | Exploits | Visual |
// Players | Soundboard | Text | Misc | Config. Menu anchors to the left hand,
// cursor follows the right index finger, right trigger clicks.
//
// Nothing in this file knows about game internals; it resolves IL2CPP, brings
// the update thread up in a fixed order, and tears it down in reverse.

#include "shared.hpp"
#include <jni.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <ctime>

// ---- globals ---------------------------------------------------------------
IL2CPPApi IL2;
JavaVM*  g_jvm   = nullptr;
double    g_time = 0;
float     g_dt   = 0;

// ---- IL2CPPApi impl --------------------------------------------------------
bool IL2CPPApi::Init() {
    void *h = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    if (!h) h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { LOGE("dlopen libil2cpp.so: %s", dlerror()); return false; }

#define L(field,sym,required) \
    field=(decltype(field))dlsym(h,"il2cpp_"#sym); \
    if(!field && required) { LOGE("missing il2cpp_"#sym); }
    L(object_new,     object_new,                true)
    L(class_from,     class_from_name,           true)
    L(domain_get,     domain_get,                true)
    L(assemblies,     domain_get_assemblies,     true)
    L(img,            assembly_get_image,        true)
    L(image_name,     image_get_name,            false)
    L(method,         class_get_method_from_name,true)
    L(invoke,         runtime_invoke,            true)
    L(str_new,        string_new,                true)
    L(obj_class,      object_get_class,          true)
    L(gc_new,         gchandle_new,              true)
    L(gc_target,      gchandle_get_target,       true)
    L(gc_free,        gchandle_free,             true)
    L(arr_new,        array_new,                 true)
    L(val_box,        value_box,                 false)
    L(class_type,     class_get_type,            false)
    L(class_parent,   class_get_parent,          false)
    L(type_object,    type_get_object,           false)
    L(field_from,     class_get_field_from_name, false)
    L(field_off,      field_get_offset,          false)
#undef L

    ready = object_new && class_from && domain_get && assemblies && img &&
            method && invoke && str_new && obj_class &&
            gc_new && gc_target && gc_free && arr_new;
    LOGI("IL2CPP %s", ready ? "ready" : "INCOMPLETE");
    return ready;
}

Il2CppImage* IL2CPPApi::GetImage(const char* assembly) {
    if (!ready) return nullptr;
    Il2CppDomain *domain = domain_get();
    if (!domain) return nullptr;
    size_t count = 0;
    Il2CppAssembly **asms = assemblies(domain, &count);
    if (!asms) return nullptr;
    for (size_t i = 0; i < count; i++) {
        Il2CppImage *image = img(asms[i]);
        if (!image) continue;
        // Preferred: il2cpp_image_get_name. Fallback reads the image name slot.
        const char *name = image_name ? image_name(image)
                                      : *(const char**)((uint8_t*)image + 0x18);
        if (name && strstr(name, assembly)) return image;
    }
    return nullptr;
}

Il2CppClass* IL2CPPApi::GetClass(const char* ns, const char* name, const char* assembly) {
    Il2CppImage *image = GetImage(assembly);
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
    if (ex) { LOGE("managed exception on %p", (void*)m); return nullptr; }
    return r;
}

int IL2CPPApi::FieldOffset(Il2CppClass* cls, const char* name) {
    if (!cls || !name || !field_from || !field_off) return -1;
    void *f = field_from(cls, name);
    if (!f) return -1;
    return field_off(f);
}

Il2CppObject* IL2CPPApi::TypeOf(Il2CppClass* cls) {
    if (!cls || !class_type || !type_object) return nullptr;
    Il2CppType *t = class_type(cls);
    return t ? type_object(t) : nullptr;
}

std::string IL2CPPApi::StrGet(Il2CppString* s) {
    if (!s) return "";
    int32_t len = *(int32_t*)((uint8_t*)s + kStrLengthAt);
    if (len <= 0) return "";
    if (len > 4096) len = 4096;                 // bounded: hostile string never overflows
    const uint16_t *chars = (const uint16_t*)((uint8_t*)s + kStrCharsAt);
    std::string out;
    out.reserve((size_t)len);
    for (int i = 0; i < len; i++) {
        uint16_t c = chars[i];
        out.push_back(c < 0x80 ? (char)c : '?');  // ASCII fold; names render, no UB
    }
    return out;
}

bool Ref::alive() const {
    Il2CppObject *o = get();
    if (!o) return false;
    return IL2.obj_class ? (IL2.obj_class(o) != nullptr) : true;
}

// ---- Patch helpers ---------------------------------------------------------
bool Patch(uintptr_t addr, const uint8_t* bytes, size_t len) {
    if (!addr || !bytes || !len) return false;
    uintptr_t pageSize = (uintptr_t)getpagesize();
    uintptr_t page = addr & ~(pageSize - 1);
    size_t span = len + (addr - page);
    // Two-step: make writable, write, restore execute-only-page protections.
    if (mprotect((void*)page, span, PROT_READ|PROT_WRITE) != 0) return false;
    memcpy((void*)addr, bytes, len);
    __builtin___clear_cache((char*)addr, (char*)(addr+len));
    mprotect((void*)page, span, PROT_READ|PROT_EXEC);
    return true;
}
void NopRange(uintptr_t addr, int count) {
    static const uint8_t NOP[4]={0x1F,0x20,0x03,0xD5};
    if (count <= 0 || count > 4096) return;
    for (int i=0;i<count;i++) Patch(addr+(uintptr_t)i*4,NOP,4);
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
void Menu_Shutdown();
void Features_Init();
void Features_Update();
void Features_Shutdown();
void ESP_Init();
void ESP_Update();

void PatchEAC(uintptr_t base);

// ---- Update thread ---------------------------------------------------------
static bool g_running = false;
static pthread_t g_thread;
static bool g_threadUp = false;

static void* UpdateThread(void*) {
    LOGI("update thread up");

    for (int i=0;i<240 && g_running && !IL2.ready;i++){ IL2.Init(); usleep(250000); }
    if (!IL2.ready || !g_running){ LOGE("IL2CPP never ready"); return nullptr; }

    uintptr_t base = GetLibBase("libil2cpp.so");
    if (base) PatchEAC(base);

    // Fixed bring-up order: state first, then the bridges that carry it,
    // then the UI that reads all of them.
    Features_Init();
    ESP_Init();
    Photon::Init();
    Soundboard::Init();
    EventPump::Attach();     // audio receive path; failure is logged, not fatal
    Menu_Init();

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    double last=ts.tv_sec+ts.tv_nsec*1e-9;

    while(g_running){
        clock_gettime(CLOCK_MONOTONIC,&ts);
        double now=ts.tv_sec+ts.tv_nsec*1e-9;
        g_dt=(float)(now-last); last=now;
        if (g_dt > 0.25f) g_dt = 0.25f;      // never let a stall race the tick
        g_time+=g_dt;

        Features_Update();
        ESP_Update();
        Menu_Update();

        usleep(16667); // ~60Hz
    }

    // Reverse teardown: UI first, then consumers, then the source of state.
    Menu_Shutdown();
    EventPump::Detach();
    Soundboard::Shutdown();
    Photon::Shutdown();
    Features_Shutdown();
    LOGI("update thread down");
    return nullptr;
}

// ---- JNI entry -------------------------------------------------------------
extern "C" __attribute__((visibility("default")))
jint JNI_OnLoad(JavaVM* vm, void*) {
    if (g_running) return JNI_VERSION_1_6;   // idempotent: loader may call twice
    g_jvm = vm;
    LOGI("vrc-quest v3 loading");
    g_running = true;
    if (pthread_create(&g_thread,nullptr,UpdateThread,nullptr) == 0) {
        pthread_detach(g_thread);
        g_threadUp = true;
    } else {
        LOGE("pthread_create failed");
        g_running = false;
    }
    return JNI_VERSION_1_6;
}
extern "C" __attribute__((visibility("default")))
void JNI_OnUnload(JavaVM*,void*) {
    g_running = false;
    g_threadUp = false;
}
