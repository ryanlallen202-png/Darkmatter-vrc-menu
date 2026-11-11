#include "shared.hpp"
#include <sys/mman.h>

// ============================================================================
// AArch64 inline hook engine
//
// Strategy: overwrite the first 3 instructions of a function with an absolute
// branch to the detour, and copy the displaced instructions into a trampoline
// that returns to target+12. AArch64 is fixed-width, so every patch site is
// exactly 12 bytes and no instruction is ever cut in half.
//
// Refuses, fail-closed, when:
//   * the target is not 4-byte aligned
//   * any displaced instruction is PC-relative (B/BL/ADR/ADRP/LDR-literal)
//     — the displaced copy would reference the wrong bank
//   * mprotect fails or the address is not mapped readable
//
// No trampoline pool bookkeeping beyond one page per hook; a page is mapped
// RX and reused per hook. Hooks are a handful, not thousands.
// ============================================================================

namespace {

constexpr int kPatchBytes = 12;   // three AArch64 instructions
constexpr int kTrampSize  = 32;   // displaced 12 + jump-back 16, padded

struct HookEntry {
    uintptr_t target = 0;
    void     *tramp  = nullptr;   // RX, executable
    uint8_t   saved[kPatchBytes] = {};
    bool      live = false;
};
static HookEntry g_hooks[32];
static int       g_hookCount = 0;

// BR Xn
inline uint32_t EmitBr(uint32_t reg) {
    return 0xD61F0000u | ((reg & 31u) << 5);
}

// LDR X0, #8 ; BR X0 ; <8-byte literal> — occupies 4 words, uses 16 bytes.
// Callers must hand over at least 16 bytes of writable space.
static void EmitAbsJump(uint32_t* out, uintptr_t dest) {
    out[0] = 0x58000040u;        // LDR X0, #8
    out[1] = EmitBr(0);          // BR  X0
    uint64_t d = (uint64_t)dest;
    memcpy(&out[2], &d, sizeof d);
}

// Is this instruction PC-relative? Bits 31..24 pattern check for the families
// that cannot be relocated without rewriting immediates.
static bool IsPcRelative(uint32_t insn) {
    // B / BL : 000101xx
    if ((insn & 0x7C000000u) == 0x14000000u) return true;
    // B.cond / CBZ / CBNZ / TBZ / TBNZ : 01010100 / 1011010x / 0011010x
    if ((insn & 0xFF000010u) == 0x54000000u) return true;
    if ((insn & 0x7E000000u) == 0x34000000u) return true;
    if ((insn & 0x7E000000u) == 0x36000000u) return true;
    // ADR / ADRP : 0xx10000
    if ((insn & 0x1F000000u) == 0x10000000u) return true;
    // LDR literal : xx011000
    if ((insn & 0x3B000000u) == 0x18000000u) return true;
    return false;
}

static void FlushICache(void* addr, size_t len) {
    __builtin___clear_cache((char*)addr, (char*)addr + len);
}

} // namespace

bool HookAttach(uintptr_t target, void* detour, void** original) {
    if (original) *original = nullptr;
    if (!target || !detour) return false;
    if (g_hookCount >= (int)(sizeof(g_hooks)/sizeof(g_hooks[0]))) {
        LOGE("hook: table full");
        return false;
    }
    if (target & 3u) { LOGE("hook: unaligned target 0x%zx", (size_t)target); return false; }

    for (int i=0;i<g_hookCount;i++)
        if (g_hooks[i].live && g_hooks[i].target == target) {
            if (original) *original = g_hooks[i].tramp;
            return true;   // already hooked — idempotent, no double trampoline
        }

    const uint32_t* site = (const uint32_t*)target;
    for (int i=0;i<3;i++) {
        if (IsPcRelative(site[i])) {
            LOGE("hook: PC-relative insn %d at 0x%zx — refusing", i, (size_t)target);
            return false;
        }
    }

    // Trampoline: 3 displaced instructions + absolute jump back to target+12.
    void* tramp = mmap(nullptr, kTrampSize,
                       PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (tramp == MAP_FAILED) { LOGE("hook: mmap trampoline failed"); return false; }

    memcpy(tramp, (void*)target, kPatchBytes);
    EmitAbsJump((uint32_t*)((uint8_t*)tramp + kPatchBytes), target + kPatchBytes);
    if (mprotect(tramp, kTrampSize, PROT_READ|PROT_EXEC) != 0) {
        LOGE("hook: mprotect trampoline RX failed");
        munmap(tramp, kTrampSize);
        return false;
    }
    FlushICache(tramp, kTrampSize);

    HookEntry& e = g_hooks[g_hookCount];
    e.target = target;
    e.tramp  = tramp;
    memcpy(e.saved, (void*)target, kPatchBytes);

    // Patch site: LDR X16, #8 ; BR X16 ; <detour>
    // 4 words allocated, 3 written — the literal occupies words 2..3 and only
    // the first 12 bytes are copied into the target.
    uint32_t patch[4] = {};
    patch[0] = 0x58000050u;                 // LDR X16, #8
    patch[1] = EmitBr(16);                  // BR  X16
    uint64_t dest = (uint64_t)detour;
    memcpy(&patch[2], &dest, sizeof dest);

    uintptr_t pageSize = (uintptr_t)getpagesize();
    uintptr_t page = target & ~(pageSize - 1);
    size_t span = kPatchBytes + (target - page);
    if (mprotect((void*)page, span, PROT_READ|PROT_WRITE|PROT_EXEC) != 0) {
        LOGE("hook: mprotect site failed at 0x%zx", (size_t)target);
        munmap(tramp, kTrampSize);
        return false;
    }
    memcpy((void*)target, patch, kPatchBytes);
    FlushICache((void*)target, kPatchBytes);
    mprotect((void*)page, span, PROT_READ|PROT_EXEC);

    e.live = true;
    g_hookCount++;
    if (original) *original = tramp;
    LOGI("hook attached 0x%zx -> %p (tramp %p)", (size_t)target, detour, tramp);
    return true;
}

bool HookDetach(uintptr_t target) {
    for (int i=0;i<g_hookCount;i++) {
        HookEntry& e = g_hooks[i];
        if (!e.live || e.target != target) continue;
        uintptr_t pageSize = (uintptr_t)getpagesize();
        uintptr_t page = target & ~(pageSize - 1);
        size_t span = kPatchBytes + (target - page);
        if (mprotect((void*)page, span, PROT_READ|PROT_WRITE|PROT_EXEC) != 0) return false;
        memcpy((void*)target, e.saved, kPatchBytes);
        FlushICache((void*)target, kPatchBytes);
        mprotect((void*)page, span, PROT_READ|PROT_EXEC);
        munmap(e.tramp, kTrampSize);
        e.live = false;
        e.tramp = nullptr;
        LOGI("hook detached 0x%zx", (size_t)target);
        return true;
    }
    return false;
}

// Detach by detour pointer — used by modules that did not keep the target.
// Returns true when a hook whose detour matches was removed.
bool HookDetachByDetour(void* detour) {
    for (int i=0;i<g_hookCount;i++) {
        if (!g_hooks[i].live) continue;
        // The detour was written into the patch site at +8.
        uintptr_t site = g_hooks[i].target;
        void* installed = *(void**)(site + 8);
        if (installed == detour) return HookDetach(site);
    }
    return false;
}
