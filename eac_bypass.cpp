// eac_bypass.cpp — metadata XOR key bypass + tamper report kill
#include "shared.hpp"
#include "offsets.h"
#include <sys/mman.h>

static const uint8_t kRetZero[8] = {0x00,0x00,0x80,0x52,0xC0,0x03,0x5F,0xD6};
static const uint8_t kRetTrue[8] = {0x20,0x00,0x80,0x52,0xC0,0x03,0x5F,0xD6};

static bool eac_patch(uintptr_t addr, const uint8_t* bytes, size_t len) {
    uintptr_t page = addr & ~((uintptr_t)(getpagesize()-1));
    size_t plen = len + (addr - page) + getpagesize();
    if (mprotect((void*)page, plen, PROT_READ|PROT_WRITE|PROT_EXEC) != 0) return false;
    memcpy((void*)addr, bytes, len);
    __builtin___clear_cache((char*)addr, (char*)(addr+len));
    return true;
}

void PatchEAC(uintptr_t base) {
    if (!base) { LOGE("PatchEAC: no base"); return; }
    LOGI("PatchEAC: base=0x%zx", (size_t)base);

    // ── 1. Zero the metadata XOR key ─────────────────────────────────────
    uintptr_t key_addr = base + (uintptr_t)METADATA_XOR_KEY_RVA;
    uintptr_t page = key_addr & ~((uintptr_t)(getpagesize()-1));
    if (mprotect((void*)page, getpagesize()*2, PROT_READ|PROT_WRITE|PROT_EXEC) == 0) {
        memset((void*)key_addr, 0, 4);
        __builtin___clear_cache((char*)key_addr, (char*)(key_addr+4));
        LOGI("PatchEAC: XOR key zeroed at 0x%zx", (size_t)key_addr);
    } else {
        LOGE("PatchEAC: mprotect XOR key failed");
    }

    // ── 2. Runtime-patch libvrc_fast_crypto via dlsym ─────────────────────
    void* h = dlopen("libvrc_fast_crypto.so", RTLD_NOW | RTLD_NOLOAD);
    if (!h) h = dlopen("libvrc_fast_crypto.so", RTLD_NOW);
    if (h) {
        const char* sym_true[]  = {"verify_message", "sign_message", nullptr};
        const char* sym_zero[]  = {"generate_signing_key", nullptr};
        for (int i = 0; sym_true[i]; i++) {
            void* addr = dlsym(h, sym_true[i]);
            if (addr) { eac_patch((uintptr_t)addr, kRetTrue, 8); LOGI("PatchEAC: RetTrue %s", sym_true[i]); }
            else LOGE("PatchEAC: missing %s", sym_true[i]);
        }
        for (int i = 0; sym_zero[i]; i++) {
            void* addr = dlsym(h, sym_zero[i]);
            if (addr) { eac_patch((uintptr_t)addr, kRetZero, 8); LOGI("PatchEAC: RetZero %s", sym_zero[i]); }
            else LOGE("PatchEAC: missing %s", sym_zero[i]);
        }
    } else {
        LOGE("PatchEAC: libvrc_fast_crypto.so not loaded");
    }

    LOGI("PatchEAC: done");
}
