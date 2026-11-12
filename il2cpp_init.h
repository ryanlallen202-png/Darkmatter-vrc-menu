#pragma once
// il2cpp_init.h — public interface for the v2 IL2CPP resolver
// (obfuscated-export scan + function-table walk for VRChat 2026.4.2)
// Implementation: il2cpp_init.cpp

#include <cstdint>
#include <cstddef>

namespace il2cpp
{
    // ── Function pointers (populated by Init / ResolveFunctionTable) ──────
    using domain_get_fn           = void*(*)();
    using domain_assembly_open_fn = void*(*)(void* domain, const char* name);
    using assembly_get_image_fn   = void*(*)(void* assembly);
    using class_from_name_fn      = void*(*)(void* image, const char* ns, const char* name);
    using class_get_method_fn     = void*(*)(void* klass, const char* name, int argc);
    using method_get_pointer_fn   = void*(*)(void* method);
    using object_new_fn           = void*(*)(void* klass);
    using array_new_fn            = void*(*)(void* klass, size_t length);
    using class_get_type_fn       = void*(*)(void* klass);
    using string_new_fn           = void*(*)(const char* str);
    using runtime_invoke_fn       = void*(*)(void* method, void* obj, void** params, void** exc);

    extern domain_get_fn           domain_get;
    extern domain_assembly_open_fn domain_assembly_open;
    extern assembly_get_image_fn   assembly_get_image;
    extern class_from_name_fn      class_from_name;
    extern class_get_method_fn     class_get_method;
    extern method_get_pointer_fn   method_get_pointer;
    extern object_new_fn           object_new;
    extern array_new_fn            array_new;
    extern class_get_type_fn       class_get_type;
    extern string_new_fn           string_new;
    extern runtime_invoke_fn       runtime_invoke;

    // ── Live domain ptr (cached after ResolveFunctionTable) ───────────────
    extern void* g_domain;

    // ── API ───────────────────────────────────────────────────────────────
    // lib_dir: directory containing libil2cpp.so, e.g. "/data/app/.../lib/arm64"
    bool Init(const char* lib_dir);
    bool Ready();
    void ResolveFunctionTable();

    // High-level helper: resolve a method pointer by assembly/ns/class/method
    void* ResolveMethod(const char* assembly, const char* ns,
                        const char* klass, const char* method, int argc);
}
