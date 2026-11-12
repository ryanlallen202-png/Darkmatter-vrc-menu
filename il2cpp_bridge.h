#pragma once
// il2cpp_bridge.h — wires il2cpp:: (obfuscated-export v2 resolver) into the
// IL2CPPApi struct that features.cpp / menu.cpp / esp.cpp already use.
// Include this AFTER il2cpp_init.h and shared.hpp.
// Call IL2_BridgeInit() from the update thread once il2cpp::Ready() is true.

#include "shared.hpp"
#include "il2cpp_init.h"

// Forward the existing IL2CPPApi::GetImage / GetClass / GetMethod / Invoke
// down to the il2cpp:: namespace for callers that go through IL2 directly.
// The v2 resolver doesn't use the assembly-scan loop — it uses
// il2cpp::domain_assembly_open + il2cpp::class_from_name, which is both
// faster and correct across the obfuscated 2026 build.

inline bool IL2_BridgeInit()
{
    if (!il2cpp::Ready()) return false;
    if (IL2.ready)        return true;   // already initialised via old path

    // Rebind IL2CPPApi fn ptrs to the v2-resolved functions so the entire
    // existing codebase keeps working without modification.
    // il2cpp:: exposes raw fn ptrs as typed statics in the namespace.

    IL2.object_new = reinterpret_cast<fn_object_new>(
        reinterpret_cast<void*>(il2cpp::object_new));

    IL2.class_from = reinterpret_cast<fn_class_from_name>(
        reinterpret_cast<void*>(il2cpp::class_from_name));

    // domain_get in IL2CPPApi takes no args and returns void*;
    // il2cpp::domain_get is the same sig after resolution.
    // We also cache the domain so IL2.GetImage works.
    IL2.domain_get = reinterpret_cast<fn_domain_get>(
        reinterpret_cast<void*>(il2cpp::domain_get));

    // domain_assemblies: IL2CPPApi uses il2cpp_domain_get_assemblies
    // (takes domain*, size_t*). The v2 build doesn't expose this symbol
    // by name after obfuscation — but we can build a shim that iterates
    // known assemblies via domain_assembly_open for the ones features.cpp
    // actually needs. Alternatively, dlsym the non-obfuscated variant:
    {
        void *h = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_NOW);
        if (!h) h = dlopen("libil2cpp.so", RTLD_NOW);
        if (h) {
            // Try non-obfuscated (may succeed on some builds / fallback):
            auto fn = dlsym(h, "il2cpp_domain_get_assemblies");
            if (fn)
                IL2.assemblies = reinterpret_cast<fn_domain_assemblies>(fn);
            // Other stable non-obfuscated exports:
            IL2.str_new    = reinterpret_cast<fn_string_new>(
                dlsym(h, "il2cpp_string_new"));
            IL2.obj_class  = reinterpret_cast<fn_object_class>(
                dlsym(h, "il2cpp_object_get_class"));
            IL2.gc_new     = reinterpret_cast<fn_gchandle_new>(
                dlsym(h, "il2cpp_gchandle_new"));
            IL2.gc_target  = reinterpret_cast<fn_gchandle_target>(
                dlsym(h, "il2cpp_gchandle_get_target"));
            IL2.gc_free    = reinterpret_cast<fn_gchandle_free>(
                dlsym(h, "il2cpp_gchandle_free"));
            IL2.arr_new    = reinterpret_cast<fn_array_new>(
                reinterpret_cast<void*>(il2cpp::array_new));
            IL2.val_box    = reinterpret_cast<fn_value_box>(
                dlsym(h, "il2cpp_value_box"));
        }
    }

    IL2.img    = reinterpret_cast<fn_assembly_image>(
        reinterpret_cast<void*>(il2cpp::assembly_get_image));
    IL2.method = reinterpret_cast<fn_method_from_name>(
        reinterpret_cast<void*>(il2cpp::class_get_method));
    IL2.invoke = reinterpret_cast<fn_runtime_invoke>(
        reinterpret_cast<void*>(il2cpp::runtime_invoke));

    // string_new: il2cpp::string_new was resolved from function table
    if (!IL2.str_new && il2cpp::string_new)
        IL2.str_new = reinterpret_cast<fn_string_new>(
            reinterpret_cast<void*>(il2cpp::string_new));

    // array_new from function table
    if (!IL2.arr_new && il2cpp::array_new)
        IL2.arr_new = reinterpret_cast<fn_array_new>(
            reinterpret_cast<void*>(il2cpp::array_new));

    IL2.ready = (IL2.object_new  != nullptr &&
                 IL2.class_from  != nullptr &&
                 IL2.domain_get  != nullptr &&
                 IL2.img         != nullptr &&
                 IL2.method      != nullptr &&
                 IL2.invoke      != nullptr);

    // Override GetImage to use v2 assembly-open path (bypasses the stale
    // assembly scan loop that relied on reading image+0x18 for name ptr).
    // We do this by patching the vtable-equivalent: IL2CPPApi::GetImage is
    // a non-virtual method, so callers call it directly. We can't override
    // it at link time, but we can ensure the fn ptrs it uses are correct —
    // which the assignments above already do.

    LOGI("IL2_BridgeInit: %s (object_new=%p class_from=%p img=%p invoke=%p)",
         IL2.ready ? "READY" : "PARTIAL",
         (void*)IL2.object_new, (void*)IL2.class_from,
         (void*)IL2.img, (void*)IL2.invoke);

    return IL2.ready;
}
