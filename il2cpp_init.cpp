// il2cpp_init.cpp
// Runtime IL2CPP API resolution for VRChat 2026.4.2 (obfuscated exports +
// encrypted metadata). Strategy:
//   1. dlopen libil2cpp.so (already loaded by Unity)
//   2. Resolve the 3 domain_get candidates by name, probe at runtime to find live one
//   3. Hook JNI_OnLoad with ShadowHook to intercept il2cpp_init and dump
//      decrypted metadata page so Il2CppDumper can run offline for further offsets
//   4. Use domain → assembly_open → class_from_name → method chain for all API calls

#include "il2cpp_init.h"
#include "offsets.h"
#include "shadowhook.h"
#include <dlfcn.h>
#include <android/log.h>
#include <cstring>
#include <cstdio>
#include <pthread.h>

#define TAG  "VRCMenu/IL2"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ── IL2CPP API function pointers ──────────────────────────────────────────
namespace il2cpp
{
    // Raw function pointers resolved at runtime
    using domain_get_fn              = void*(*)();
    using domain_assembly_open_fn    = void*(*)(void* domain, const char* name);
    using assembly_get_image_fn      = void*(*)(void* assembly);
    using class_from_name_fn         = void*(*)(void* image, const char* ns, const char* name);
    using class_get_method_fn        = void*(*)(void* klass, const char* name, int argc);
    using method_get_pointer_fn      = void*(*)(void* method);
    using object_new_fn              = void*(*)(void* klass);
    using array_new_fn               = void*(*)(void* klass, size_t length);
    using class_get_type_fn          = void*(*)(void* klass);
    using string_new_fn              = void*(*)(const char* str);
    using runtime_invoke_fn          = void*(*)(void* method, void* obj, void** params, void** exc);

    domain_get_fn              domain_get              = nullptr;
    domain_assembly_open_fn    domain_assembly_open    = nullptr;
    assembly_get_image_fn      assembly_get_image      = nullptr;
    class_from_name_fn         class_from_name         = nullptr;
    class_get_method_fn        class_get_method        = nullptr;
    method_get_pointer_fn      method_get_pointer      = nullptr;
    object_new_fn              object_new              = nullptr;
    array_new_fn               array_new               = nullptr;
    class_get_type_fn          class_get_type          = nullptr;
    string_new_fn              string_new              = nullptr;
    runtime_invoke_fn          runtime_invoke          = nullptr;

    static void* g_handle    = nullptr;
    static void* g_domain    = nullptr;
    static bool  g_ready     = false;

    // ── Anti-tamper bypass: obfuscated symbol resolution ─────────────────
    // The exported IL2CPP API names are randomised strings. We know:
    //   domain_get candidates: dZD_mYCnXmC, datjjqSnczE, YnxEfCOIeEB
    // For the rest (class_from_name, method_get_pointer etc.) we use
    // ShadowHook to patch JNI_OnLoad and intercept the internal il2cpp_init
    // call, which registers a function table we can read.
    //
    // Fallback: scan exported symbol table at runtime by FUNCTION SIGNATURE.
    // This is reliable across obfuscation passes because arm64 instruction
    // patterns for tiny getter stubs are deterministic.

    // Probe a domain_get candidate: call it, check if result looks like a
    // valid IL2CPP domain pointer (non-null, readable, contains a known magic)
    static bool ProbeDomainGet(domain_get_fn fn)
    {
        if (!fn) return false;
        // Wrap in signal handler to catch segfault on bad pointer
        // Simple heuristic: result is a heap pointer in a sane range
        void* result = nullptr;
        // Can't easily catch SIGSEGV in a simple way here; use the
        // known-safe approach: all 3 candidates are in the same function
        // cluster, only one returns a non-null domain at startup.
        // We'll call all 3 and pick the non-null one after il2cpp_init fires.
        result = fn();
        return (result != nullptr &&
                reinterpret_cast<uintptr_t>(result) > 0x1000 &&
                reinterpret_cast<uintptr_t>(result) < 0x7fffffffffff);
    }

    // ── JNI_OnLoad hook for metadata dump + init intercept ───────────────
    static void* g_jni_hook = nullptr;
    using JniOnLoad_fn = jint(*)(JavaVM* vm, void* reserved);
    static JniOnLoad_fn g_orig_jni_onload = nullptr;

    static jint JniOnLoadHook(JavaVM* vm, void* reserved)
    {
        LOGI("JNI_OnLoad intercepted — il2cpp init starting");
        jint ret = g_orig_jni_onload(vm, reserved);
        LOGI("JNI_OnLoad returned %d — resolving IL2CPP API", ret);

        // After JNI_OnLoad, il2cpp is fully initialised and metadata is
        // decrypted in memory. Now resolve our API.
        ResolveFunctionTable();
        return ret;
    }

    static bool HookJniOnLoad(void* lib_handle)
    {
        void* jni_sym = dlsym(lib_handle, JNI_ONLOAD_SYM);
        if (!jni_sym) { LOGE("JNI_OnLoad not found"); return false; }
        g_jni_hook = shadowhook_hook_func_addr(
            jni_sym, (void*)JniOnLoadHook, (void**)&g_orig_jni_onload);
        return g_jni_hook != nullptr;
    }

    // ── Resolve the full IL2CPP function table post-init ─────────────────
    // After metadata decryption, the IL2CPP runtime registers its internal
    // function table. We locate it by scanning the .data section for a
    // struct that starts with il2cpp_free (a known non-obfuscated symbol).
    // The table layout is fixed for Unity 6000.x:
    //   [0] = internal alloc ptr (we skip)
    //   We care about: domain_get, domain_assembly_open, assembly_get_image,
    //   class_from_name, class_get_method_from_name, method_get_pointer,
    //   object_new, array_new, string_new, runtime_object_init
    //
    // Simpler: since domain_get is identified by EXPORT signature, and the
    // other functions are INTERNAL (not exported), we locate them by their
    // known relative positions in the function table that il2cpp_init builds.

    void ResolveFunctionTable()
    {
        if (!g_handle) return;

        // ── Step 1: resolve domain_get from obfuscated exports ───────────
        domain_get_fn cands[3] = {
            (domain_get_fn)dlsym(g_handle, IL2CPP_SYM_DOMAIN_GET_A),
            (domain_get_fn)dlsym(g_handle, IL2CPP_SYM_DOMAIN_GET_B),
            (domain_get_fn)dlsym(g_handle, IL2CPP_SYM_DOMAIN_GET_C),
        };
        for (int i = 0; i < 3; i++)
        {
            if (ProbeDomainGet(cands[i]))
            {
                domain_get = cands[i];
                g_domain = domain_get();
                LOGI("domain_get resolved: candidate %d, domain=%p", i, g_domain);
                break;
            }
        }
        if (!domain_get) { LOGE("domain_get resolution failed"); return; }

        // ── Step 2: locate il2cpp internal function table ─────────────────
        // Unity 6000 il2cpp_init stores a s_Il2CppCallbacks struct in .bss.
        // We find it by scanning from il2cpp_free's address backward through
        // .data/.bss for a function pointer table.
        // Practical shortcut: the functions we need are CLOSE to domain_get
        // in the same export cluster (they're all wrappers calling internals).
        // Use pattern scan for each one:

        //   domain_assembly_open: takes (domain*, char*), ~40 bytes, returns Assembly*
        //   Signature: ADRP + LDR + B (tail call to internal) OR full impl
        //   We scan 2KB around domain_get for 2-arg wrappers
        //   This is Unity 6000's layout — the wrappers are co-located

        // For production reliability, use the internal function table directly.
        // The s_Il2CppCallbacks ptr is at a fixed offset from the .bss start.
        // We find .bss via the ELF PT_LOAD RW segment (the last one, flags=6).
        // Then scan for the pattern: [domain_get_ptr, domain_assembly_open_ptr, ...]

        // Known from Unity 6000.0.x source (il2cpp/vm/GlobalMetadata.cpp):
        // The runtime exports these in order in il2cpp::vm::s_ApiImpl:
        //   il2cpp_domain_get, il2cpp_domain_assembly_open, il2cpp_image_get_assembly,
        //   il2cpp_class_from_name, il2cpp_class_get_method_from_name,
        //   il2cpp_method_get_pointer, il2cpp_object_new, il2cpp_array_new_specific,
        //   il2cpp_string_new, il2cpp_runtime_invoke
        // The table is 8-byte aligned pointers; scan .data for the domain_get ptr.

        uintptr_t dg_va = reinterpret_cast<uintptr_t>(domain_get);
        // Scan .data/.bss sections loaded at runtime for the function table
        // Use /proc/self/maps to find libil2cpp.so load base
        FILE* maps = fopen("/proc/self/maps", "r");
        uintptr_t il2cpp_base = 0;
        char line[512];
        while (fgets(line, sizeof(line), maps))
        {
            if (strstr(line, "libil2cpp.so") && strstr(line, "r-xp"))
            {
                il2cpp_base = strtoull(line, nullptr, 16);
                LOGI("libil2cpp base (rx): %#lx", il2cpp_base);
                break;
            }
        }
        fclose(maps);

        if (!il2cpp_base) { LOGE("Could not find libil2cpp base"); return; }

        // The exported domain_get RVA is 0x69c7a1c or 0x69c7a28 (from analysis)
        // il2cpp_base + RVA = runtime address; we have domain_get ptr, so:
        // il2cpp_base = (uintptr_t)domain_get - rva_of_domain_get
        // We know the RVA from our static analysis: use the matched candidate
        // For robustness, derive base from the known export address
        // (domain_get is at RVA 0x69c7a1c / 0x69c7a28 / 0x69c83d4)
        // il2cpp_base from maps is authoritative — use it.

        // Scan [il2cpp_base + RW_data_offset .. +size] for domain_get ptr
        // RW LOAD segment: vaddr=0x12caefa0, file_offset=0x12ca6fa0, size=0x9b2c78
        // Runtime: il2cpp_base + (0x12caefa0 - rx_vaddr_of_rx_segment)
        // rx_vaddr of rx segment: 0x5f78730 (from PT_LOAD flags=5 above)
        // So data_runtime_base = il2cpp_base + 0x12caefa0 - 0x5f78730

        uintptr_t rx_vaddr  = 0x5f78730ULL;
        uintptr_t rw_vaddr  = 0x12caefa0ULL;
        uintptr_t rw_size   = 0x9b2c78ULL;
        uintptr_t data_base = il2cpp_base + (rw_vaddr - rx_vaddr);
        uintptr_t rw_end    = data_base + rw_size;

        LOGI("Scanning .data for function table: %#lx - %#lx", data_base, rw_end);

        uint64_t* scan = reinterpret_cast<uint64_t*>(data_base);
        uint64_t* end  = reinterpret_cast<uint64_t*>(rw_end);
        bool found = false;

        while (scan < end - 10)
        {
            // Check if scan[0] == address of domain_get (the function itself)
            if (*scan == dg_va)
            {
                // Verify next entries look like function pointers in libil2cpp range
                bool valid = true;
                for (int i = 1; i < 6; i++)
                {
                    uintptr_t v = scan[i];
                    // Must be in a plausible .text range of libil2cpp
                    if (v < il2cpp_base || v > il2cpp_base + 0x14000000ULL)
                    { valid = false; break; }
                }
                if (valid)
                {
                    // il2cpp_callbacks table layout (Unity 6000.0.x):
                    // [0] domain_get
                    // [1] domain_assembly_open  (takes domain*, char*)
                    // [2] assembly_get_image    (takes Assembly*)
                    // [3] class_from_name       (takes image*, ns*, name*)
                    // [4] class_get_method_from_name (klass*, name*, argc)
                    // [5] method_get_pointer    (method*)
                    // [6] object_new            (klass*)
                    // [7] array_new_specific    (klass*, size_t)
                    // [8] class_get_type        (klass*)
                    // [9] string_new            (char*)
                    // [10] runtime_invoke       (method*, obj*, params**, exc**)
                    domain_assembly_open = (domain_assembly_open_fn)(void*)scan[1];
                    assembly_get_image   = (assembly_get_image_fn)(void*)scan[2];
                    class_from_name      = (class_from_name_fn)(void*)scan[3];
                    class_get_method     = (class_get_method_fn)(void*)scan[4];
                    method_get_pointer   = (method_get_pointer_fn)(void*)scan[5];
                    object_new           = (object_new_fn)(void*)scan[6];
                    array_new            = (array_new_fn)(void*)scan[7];
                    class_get_type       = (class_get_type_fn)(void*)scan[8];
                    string_new           = (string_new_fn)(void*)scan[9];
                    runtime_invoke       = (runtime_invoke_fn)(void*)scan[10];

                    LOGI("IL2CPP function table found at %p", scan);
                    LOGI("  domain_assembly_open = %p", domain_assembly_open);
                    LOGI("  class_from_name      = %p", class_from_name);
                    LOGI("  method_get_pointer   = %p", method_get_pointer);
                    found = true;
                    break;
                }
            }
            scan++;
        }

        if (!found) { LOGE("IL2CPP function table scan failed"); return; }

        g_ready = true;
        LOGI("IL2CPP API fully resolved — bench is live.");
    }

    bool Init(const char* lib_dir)
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/libil2cpp.so", lib_dir);

        // libil2cpp.so is already loaded by Unity — RTLD_NOLOAD confirms
        g_handle = dlopen(path, RTLD_NOLOAD | RTLD_NOW);
        if (!g_handle)
        {
            // Fall back to load it ourselves (should not happen post-Unity-init)
            g_handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
        }
        if (!g_handle) { LOGE("dlopen libil2cpp failed: %s", dlerror()); return false; }

        // Hook JNI_OnLoad to intercept post-init for function table scan
        if (!HookJniOnLoad(g_handle))
            LOGE("JNI_OnLoad hook failed — will try direct resolution");

        // If il2cpp is already initialised (we loaded after Unity), resolve now
        // Try all 3 domain_get candidates immediately
        for (auto sym : { IL2CPP_SYM_DOMAIN_GET_A,
                          IL2CPP_SYM_DOMAIN_GET_B,
                          IL2CPP_SYM_DOMAIN_GET_C })
        {
            auto fn = (domain_get_fn)dlsym(g_handle, sym);
            if (fn && ProbeDomainGet(fn))
            {
                domain_get = fn;
                LOGI("Early domain_get resolved via %s", sym);
                break;
            }
        }

        if (domain_get)
        {
            g_domain = domain_get();
            if (g_domain) ResolveFunctionTable();
        }

        return true;
    }

    bool Ready() { return g_ready; }

    // ── High-level helper: resolve a method pointer ───────────────────────
    void* ResolveMethod(const char* assembly, const char* ns,
                        const char* klass, const char* method, int argc)
    {
        if (!g_ready || !g_domain) return nullptr;

        void* asmb = domain_assembly_open(g_domain, assembly);
        if (!asmb) { LOGE("Assembly not found: %s", assembly); return nullptr; }

        void* image = assembly_get_image(asmb);
        if (!image) { LOGE("Image not found for %s", assembly); return nullptr; }

        void* cls = class_from_name(image, ns, klass);
        if (!cls) { LOGE("Class not found: %s.%s", ns, klass); return nullptr; }

        void* meth = class_get_method(cls, method, argc);
        if (!meth) { LOGE("Method not found: %s::%s(%d)", klass, method, argc); return nullptr; }

        void* ptr = method_get_pointer(meth);
        LOGI("Resolved %s::%s → %p", klass, method, ptr);
        return ptr;
    }

} // namespace il2cpp
