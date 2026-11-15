// eac_bypass.cpp — metadata XOR key bypass + tamper report kill
#include "shared.hpp"
#include "offsets.h"
#include <sys/mman.h>
#include <cstring>
#include <dlfcn.h>

static const uint8_t RET_ZERO[8] = {0x00,0x00,0x80,0x52,0xC0,0x03,0x5F,0xD6};
static const uint8_t RET_TRUE[8] = {0x20,0x00,0x80,0x52,0xC0,0x03,0x5F,0xD6};

static bool PatchAddr(uintptr_t addr, const uint8_t* bytes, size_t len) {
    uintptr_t page = addr & ~((uintptr_t)getpagesize()-1);
    if (mprotect((void*)page, len + (addr - page) + getpagesize(), PROT_READ|PROT_WRITE|PROT_EXEC) != 0)
        return false;
    memcpy((void*)addr, bytes, len);
    __builtin___clear_cache((char*)addr, (char*)(addr+len));
    return true;
}

void PatchEAC(uintptr_t base) {
    if (!base) { LOGE("PatchEAC: no base"); return; }
    LOGI("PatchEAC: base=0x%zx", (size_t)base);

    // ── 1. Zero the metadata XOR key so metadata decrypts clean ─────────
    uintptr_t key_addr = base + METADATA_XOR_KEY_RVA;
    uintptr_t page = key_addr & ~((uintptr_t)getpagesize()-1);
    if (mprotect((void*)page, getpagesize()*2, PROT_READ|PROT_WRITE|PROT_EXEC) == 0) {
        memset((void*)key_addr, 0, 4);
        __builtin___clear_cache((char*)key_addr, (char*)(key_addr+4));
        LOGI("PatchEAC: XOR key zeroed at 0x%zx", (size_t)key_addr);
    } else {
        LOGE("PatchEAC: mprotect XOR key failed");
    }

    // ── 2. Runtime-patch libvrc_fast_crypto via dlsym ────────────────────
    void* h = dlopen("libvrc_fast_crypto.so", RTLD_NOW | RTLD_NOLOAD);
    if (!h) h = dlopen("libvrc_fast_crypto.so", RTLD_NOW);
    if (h) {
        const struct { const char* sym; const uint8_t* patch; } fns[] = {
            {"verify_message",       RET_TRUE},
            {"sign_message",         RET_TRUE},
            {"generate_signing_key", RET_ZERO},
            {nullptr, nullptr}
        };
        for (int i = 0; fns[i].sym; i++) {
            void* addr = dlsym(h, fns[i].sym);
            if (addr) {
                PatchAddr((uintptr_t)addr, fns[i].patch, 8);
                LOGI("PatchEAC: patched %s @ %p", fns[i].sym, addr);
            } else {
                LOGE("PatchEAC: %s not found", fns[i].sym);
            }
        }
    } else {
        LOGE("PatchEAC: libvrc_fast_crypto.so not loaded");
    }

    LOGI("PatchEAC: done");
}
