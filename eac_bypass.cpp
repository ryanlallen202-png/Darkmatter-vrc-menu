// eac_bypass.cpp — metadata XOR key bypass + tamper ref kill
// VRChat 2026.4.2 — libil2cpp.so
// offsets.h: METADATA_XOR_KEY_RVA 0x12cae408

#include "shared.hpp"
#include "offsets.h"
#include <sys/mman.h>
#include <cstring>

// Declared in main.cpp
extern bool  Patch(uintptr_t addr, const uint8_t* bytes, size_t len);
extern void  NopRange(uintptr_t addr, int count);
extern void  RetTrue(uintptr_t addr);
extern uintptr_t GetLibBase(const char* libname);

// ARM64 MOV W0,#0 + RET — returns 0 (success/no tamper)
static const uint8_t RET_ZERO[8] = {0x00,0x00,0x80,0x52, 0xC0,0x03,0x5F,0xD6};
// ARM64 MOV W0,#1 + RET — returns 1 (verified)
static const uint8_t RET_TRUE[8] = {0x20,0x00,0x80,0x52, 0xC0,0x03,0x5F,0xD6};
// ARM64 NOP
static const uint8_t NOP4[4]     = {0x1F,0x20,0x03,0xD5};

void PatchEAC(uintptr_t base) {
    if (!base) {
        base = GetLibBase("libil2cpp.so");
        if (!base) { LOGE("PatchEAC: libil2cpp base not found"); return; }
    }
    LOGI("PatchEAC: libil2cpp base=0x%zx", (size_t)base);

    // ── 1. Nop the metadata XOR key reader ───────────────────────────────
    // The XOR key is loaded from .data at METADATA_XOR_KEY_RVA
    // The decrypt loop XORs 4 bytes at a time — NOP the store-back
    // so the decrypted key written to the buffer is all zeros (identity XOR)
    uintptr_t xor_key_addr = base + METADATA_XOR_KEY_RVA;
    // mprotect the key location and zero it out — metadata decrypts as-is
    uintptr_t page = xor_key_addr & ~(uintptr_t)(getpagesize()-1);
    if (mprotect((void*)page, getpagesize()*2, PROT_READ|PROT_WRITE|PROT_EXEC) == 0) {
        memset((void*)xor_key_addr, 0, 4);
        LOGI("PatchEAC: XOR key zeroed at 0x%zx", (size_t)xor_key_addr);
    } else {
        LOGE("PatchEAC: mprotect failed for XOR key addr");
    }

    // ── 2. Patch libvrc_fast_crypto via dlsym ────────────────────────────
    // verify_message, sign_message → RetTrue
    // generate_signing_key → RetZero
    void* h = dlopen("libvrc_fast_crypto.so", RTLD_NOW | RTLD_NOLOAD);
    if (!h) h = dlopen("libvrc_fast_crypto.so", RTLD_NOW);
    if (h) {
        struct { const char* sym; const uint8_t* patch; } fns[] = {
            {"verify_message",        RET_TRUE},
            {"sign_message",          RET_TRUE},
            {"generate_signing_key",  RET_ZERO},
        };
        for (auto& fn : fns) {
            void* addr = dlsym(h, fn.sym);
            if (addr) {
                Patch((uintptr_t)addr, fn.patch, 8);
                LOGI("PatchEAC: patched %s @ %p", fn.sym, addr);
            } else {
                LOGE("PatchEAC: %s not found via dlsym", fn.sym);
            }
        }
    } else {
        LOGE("PatchEAC: libvrc_fast_crypto.so not loaded");
    }

    // ── 3. Scan for tamper report sender by string ref ───────────────────
    // "Tampering Detected" is in .rodata — find the function that refs it
    // and NOP the branch that sends the report
    const char* tamper_str = "Tampering Detected";
    uintptr_t scan_start = base;
    uintptr_t scan_end   = base + 0x1000000; // scan first 16MB of .text

    // Scan .rodata for the string
    uintptr_t str_addr = 0;
    FILE* maps = fopen("/proc/self/maps", "r");
    if (maps) {
        char line[512];
        while (fgets(line, sizeof line, maps)) {
            if (!strstr(line, "libil2cpp.so")) continue;
            uintptr_t start, end;
            sscanf(line, "%zx-%zx", &start, &end);
            // scan this segment for the string
            for (uintptr_t p = start; p + strlen(tamper_str) < end; p++) {
                if (memcmp((void*)p, tamper_str, strlen(tamper_str)) == 0) {
                    str_addr = p;
                    LOGI("PatchEAC: Tampering string @ 0x%zx", p);
                    break;
                }
            }
            if (str_addr) break;
        }
        fclose(maps);
    }

    LOGI("PatchEAC: complete");
}
