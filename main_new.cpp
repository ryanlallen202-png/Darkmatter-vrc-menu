// main.cpp — Darkmatter VRC Quest menu, v2 engine
// IL2CPP init: obfuscated-export scan + function-table walk (il2cpp_init.cpp)
// then bridged to IL2CPPApi (il2cpp_bridge.h) for existing features/esp/menu.
// Menu toggle: right thumbstick click (wired via MenuRenderer input hook)
//              OR NativeToggleMenu() called from Java OverlayService.
// Build: see CMakeLists.txt

#include "shared.hpp"
#include "il2cpp_init.h"
#include "il2cpp_bridge.h"
#include "menu_renderer.h"
#include "gunlib.h"
#include "soundboard.h"
#include <jni.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <ctime>

// ── Globals ------------------------------------------------------------------
IL2CPPApi IL2;
double g_time = 0;
float  g_dt   = 0;

// ── Forward declarations (features.cpp / esp.cpp / menu.cpp) ----------------
void Menu_Init();
void Menu_Update();
void Features_Init();
void Features_Update();
void ESP_Init();
void ESP_Update();
void PatchEAC(uintptr_t base);

// ── NativeToggleMenu — called by MenuRenderer input hook or Java side --------
static bool g_menuVisible = false;

void NativeToggleMenu()
{
    g_menuVisible = !g_menuVisible;
    MenuRenderer::SetVisible(g_menuVisible);
    LOGI("Menu %s", g_menuVisible ? "OPEN" : "CLOSED");
}

// ── Patch helpers (used by features.cpp) ------------------------------------
bool Patch(uintptr_t addr, const uint8_t* bytes, size_t len)
{
    uintptr_t page = addr & ~(uintptr_t)(getpagesize() - 1);
    if (mprotect((void*)page, len + (addr - page), PROT_READ|PROT_WRITE|PROT_EXEC) != 0)
        return false;
    memcpy((void*)addr, bytes, len);
    __builtin___clear_cache((char*)addr, (char*)(addr + len));
    return true;
}

void NopRange(uintptr_t addr, int count)
{
    static const uint8_t NOP[4] = {0x1F,0x20,0x03,0xD5};
    for (int i = 0; i < count; i++) Patch(addr + i*4, NOP, 4);
    LOGI("NOP'd %d @ 0x%zx", count, (size_t)addr);
}

void RetPatch(uintptr_t addr)
{
    static const uint8_t RET[4] = {0xC0,0x03,0x5F,0xD6};
    Patch(addr, RET, 4);
    LOGI("RET @ 0x%zx", (size_t)addr);
}

void RetTrue(uintptr_t addr)
{
    static const uint8_t SEQ[8] = {0x20,0x00,0x80,0x52, 0xC0,0x03,0x5F,0xD6};
    Patch(addr, SEQ, 8);
    LOGI("RetTrue @ 0x%zx", (size_t)addr);
}

uintptr_t GetLibBase(const char* libname)
{
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512]; uintptr_t base = 0;
    while (fgets(line, sizeof(line), f))
        if (strstr(line, libname) && strstr(line, "r-xp"))
        { base = (uintptr_t)strtoull(line, nullptr, 16); break; }
    fclose(f);
    return base;
}

// ── IL2CPPApi compat shim — Init() now delegates to v2 resolver -------------
// The old Init() path (dlsym "il2cpp_*") still works as a fallback for
// non-obfuscated builds; the bridge overwrites fn ptrs with the v2 ones
// once il2cpp::Ready() fires.
bool IL2CPPApi::Init()
{
    void *h = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    if (!h) h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { LOGE("dlopen libil2cpp.so: %s", dlerror()); return false; }

#define L(field,sym) field=(decltype(field))dlsym(h,"il2cpp_"#sym);
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

    ready = object_new && class_from && domain_get &&
            assemblies && img && method && invoke && str_new;
    LOGI("IL2CPP dlsym path: %s", ready ? "ready" : "INCOMPLETE");
    return ready;
}

void* IL2CPPApi::GetImage(const char* assembly)
{
    if (!ready) return nullptr;
    // v2 path: domain_assembly_open gives us the right image even on
    // obfuscated builds where the name-at-+0x18 trick is unreliable.
    if (il2cpp::Ready() && il2cpp::domain_assembly_open && il2cpp::assembly_get_image)
    {
        void *a = il2cpp::domain_assembly_open(il2cpp::g_domain, assembly);
        if (a) return il2cpp::assembly_get_image(a);
    }
    // Fallback: scan via old loop
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

Il2CppClass* IL2CPPApi::GetClass(const char* ns, const char* name, const char* assembly)
{
    void *image = GetImage(assembly);
    if (!image) return nullptr;
    return class_from(image, ns, name);
}

MethodInfo* IL2CPPApi::GetMethod(Il2CppClass* cls, const char* mname, int argc)
{
    if (!cls || !method) return nullptr;
    return method(cls, mname, argc);
}

Il2CppObject* IL2CPPApi::Invoke(MethodInfo* m, void* obj, void** args)
{
    if (!m || !invoke) return nullptr;
    void *ex = nullptr;
    Il2CppObject *r = invoke(m, obj, args, &ex);
    if (ex) { LOGE("invoke ex on %p", (void*)m); return nullptr; }
    return r;
}

std::string IL2CPPApi::StrGet(Il2CppString* s)
{
    if (!s) return "";
    int32_t len = *(int32_t*)((uint8_t*)s + 16);
    if (len <= 0 || len > 1024) return "";
    char buf[2048];
    uint16_t *chars = (uint16_t*)((uint8_t*)s + 20);
    int n = len < 1023 ? len : 1023;
    for (int i = 0; i < n; i++) buf[i] = (char)(chars[i] & 0x7f);
    buf[n] = '\0';
    return buf;
}

bool Ref::alive() const
{
    Il2CppObject *o = get();
    if (!o) return false;
    return IL2.obj_class ? (IL2.obj_class(o) != nullptr) : true;
}

// ── Update thread ------------------------------------------------------------
static bool g_running = false;

static void* UpdateThread(void*)
{
    LOGI("Darkmatter update thread started");

    // ── Phase 1: kick off v2 il2cpp init (JNI_OnLoad hook + early probe) ──
    uintptr_t base = GetLibBase("libil2cpp.so");
    char lib_dir[512] = "/data/app";   // Unity loads from here on Quest
    // Derive lib_dir from /proc/self/maps entry
    {
        FILE *f = fopen("/proc/self/maps", "r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof(line), f))
            {
                if (strstr(line, "libil2cpp.so") && strstr(line, "r-xp"))
                {
                    // Extract path portion (last field after space)
                    char *path = strrchr(line, ' ');
                    if (path) {
                        path++; // skip space
                        char *slash = strrchr(path, '/');
                        if (slash) {
                            *slash = '\0';
                            snprintf(lib_dir, sizeof(lib_dir), "%s", path);
                        }
                    }
                    break;
                }
            }
            fclose(f);
        }
    }
    il2cpp::Init(lib_dir);

    // ── Phase 2: wait for v2 resolver OR fall back to dlsym path ──────────
    for (int i = 0; i < 240; i++)
    {
        if (il2cpp::Ready())   break;
        if (!IL2.ready) IL2.Init();
        if (IL2.ready)         break;
        usleep(500000);
    }

    // ── Phase 3: bridge v2 resolver → IL2CPPApi ───────────────────────────
    if (il2cpp::Ready())
        IL2_BridgeInit();
    else if (!IL2.ready)
    {
        LOGE("IL2CPP never ready — aborting");
        return nullptr;
    }

    // ── Phase 4: EAC patch ─────────────────────────────────────────────────
    if (base) PatchEAC(base);

    // ── Phase 5: module init ───────────────────────────────────────────────
    Gunlib::Init();
    Soundboard::Init();
    Features_Init();
    ESP_Init();
    Menu_Init();

    // ── Phase 6: 60 Hz tick loop ───────────────────────────────────────────
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double last = ts.tv_sec + ts.tv_nsec * 1e-9;

    while (g_running)
    {
        clock_gettime(CLOCK_MONOTONIC, &ts);
        double now = ts.tv_sec + ts.tv_nsec * 1e-9;
        g_dt = (float)(now - last);
        last = now;
        g_time += g_dt;

        Features_Update();
        ESP_Update();
        Menu_Update();

        usleep(16667);
    }
    return nullptr;
}

// ── JNI entry ----------------------------------------------------------------
extern "C" __attribute__((visibility("default")))
jint JNI_OnLoad(JavaVM*, void*)
{
    LOGI("Darkmatter vrc-menu loading — absolute ollie, built by olivia");
    g_running = true;
    pthread_t tid;
    pthread_create(&tid, nullptr, UpdateThread, nullptr);
    pthread_detach(tid);
    return JNI_VERSION_1_6;
}

extern "C" __attribute__((visibility("default")))
void JNI_OnUnload(JavaVM*, void*)
{
    g_running = false;
    MenuRenderer::OnSurfaceDestroyed();
}

// ── JNI exports for Java OverlayService / MenuBridge ─────────────────────────
extern "C" __attribute__((visibility("default")))
void Java_com_vrchat_oculus_quest_MenuBridge_nativeSurfaceCreated(
    JNIEnv* env, jobject /*thiz*/, jobject surface)
{
    MenuRenderer::OnSurfaceCreated(env, surface);
}

extern "C" __attribute__((visibility("default")))
void Java_com_vrchat_oculus_quest_MenuBridge_nativeSurfaceDestroyed(
    JNIEnv* /*env*/, jobject /*thiz*/)
{
    MenuRenderer::OnSurfaceDestroyed();
}

extern "C" __attribute__((visibility("default")))
void Java_com_vrchat_oculus_quest_MenuBridge_nativeTouch(
    JNIEnv* /*env*/, jobject /*thiz*/, jfloat x, jfloat y, jboolean down)
{
    MenuRenderer::OnTouch(x, y, down);
}

extern "C" __attribute__((visibility("default")))
void Java_com_vrchat_oculus_quest_MenuBridge_nativeTick(
    JNIEnv* /*env*/, jobject /*thiz*/)
{
    Soundboard::Tick();
}

extern "C" __attribute__((visibility("default")))
void Java_com_vrchat_oculus_quest_MenuBridge_nativeToggle(
    JNIEnv* /*env*/, jobject /*thiz*/)
{
    NativeToggleMenu();
}
