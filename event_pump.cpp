#include "shared.hpp"
#include <atomic>
#include <mutex>

// ============================================================================
// Photon event pump
//
// LoadBalancingClient fires EventReceived as a managed event. Subscribing from
// native code means constructing a delegate, which needs a managed static
// method to target — that indirection is why the clean path is to attach to the
// client's already-serialized event dispatch:
//
//   1. Resolve LoadBalancingClient.EventReceived (public event field).
//   2. Build an Action<EventData> from a managed shim class we generate with
//      il2cpp_reflection + a pre-compiled shim assembly is out of scope here.
//
// Instead this module drains the *incoming* queue through the client's own
// op-state callback, which VRChat already executes on the network thread:
// OnEvent(EventData) is patched with the inline hook engine, and every event
// that reaches the game is mirrored here before being forwarded to the
// original handler. That gives us the full event stream with zero delegate
// marshalling and no reflection at runtime.
//
// Dispatch contract:
//   * Runs on the game's network thread.
//   * Never allocates a managed object on the hot path (soundboard frames are
//     copied into a preallocated slot inside SoundboardNet).
//   * Never blocks: the assembly step is O(frame) copy.
// ============================================================================

// Receiver hook into the soundboard (defined in soundboard.cpp).
namespace SoundboardNet { bool FeedFrame(const uint8_t* data, int len); }

namespace {

using OnEventFn = void(*)(void* self, void* eventData);

OnEventFn g_origOnEvent = nullptr;
std::atomic<bool> g_pumpReady{false};
std::atomic<uint64_t> g_eventsSeen{0};
std::atomic<uint64_t> g_audioFrames{0};
std::atomic<uint64_t> g_eventsDropped{0};

// EventData layout on arm64 IL2CPP:
//   +0x10 klass*, +0x18 monitor*, +0x20 code (byte), +0x28 parameters (object)
// Those two offsets are the ABI's, not the game's: every IL2CPP EventData has
// them. Verified against Photon.Realtime's public EventData definition.
constexpr size_t kEvCodeOff = 0x20;
constexpr size_t kEvDataOff = 0x28;

inline uint8_t EventCode(void* eventData) {
    return *(uint8_t*)((uint8_t*)eventData + kEvCodeOff);
}
inline void* EventParams(void* eventData) {
    return *(void**)((uint8_t*)eventData + kEvDataOff);
}

// Pull bytes out of a managed byte[] (Il2CppArray).
struct ManagedBytes {
    const uint8_t* data = nullptr;
    size_t         len  = 0;
    explicit ManagedBytes(void* arr) {
        if (!arr) return;
        len = *(uintptr_t*)((uint8_t*)arr + kArrLengthAt);
        if (len == 0 || len > 64u*1024u) { len = 0; return; }
        data = (const uint8_t*)arr + kArrDataAt;
    }
    bool ok() const { return data && len; }
};

constexpr uint8_t kEventAudio = 0xA0;

void HandleEvent(void* eventData) {
    if (!eventData) return;
    uint8_t code = EventCode(eventData);
    if (code != kEventAudio) return;

    void* params = EventParams(eventData);
    ManagedBytes bytes(params);
    if (!bytes.ok()) { g_eventsDropped++; return; }
    g_audioFrames++;
    g_eventsSeen++;
    SoundboardNet::FeedFrame(bytes.data, (int)bytes.len);
}

void OnEventDetour(void* self, void* eventData) {
    // Mirror first: the game's own handler may tear down state we read.
    HandleEvent(eventData);
    if (g_origOnEvent) g_origOnEvent(self, eventData);
}

} // namespace

namespace EventPump {

bool Attach() {
    if (g_pumpReady) return true;
    if (!IL2.ready) return false;

    Il2CppClass* client = IL2.GetClass("Photon.Realtime", "LoadBalancingClient", "PhotonRealtime");
    if (!client) { LOGE("pump: client class missing"); return false; }

    // OnEvent is an override of IPhotonPeerListener.OnEvent(EventData).
    // Method name on the IL2CPP side is preserved as "OnEvent" — VRChat does
    // not strip it because Photon is a managed plugin, not game code.
    MethodInfo* m = IL2.GetMethod(client, "OnEvent", 1);
    if (!m) { LOGE("pump: OnEvent unresolved"); return false; }

    // MethodInfo->methodPointer is at +0x0 on IL2CPP 27+ arm64. Resolve
    // through il2cpp_method_get_pointer when exported, else the field.
    void* target = nullptr;
    void* h = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    if (h) {
        typedef void* (*fnGetPtr)(const MethodInfo*);
        auto getPtr = (fnGetPtr)dlsym(h, "il2cpp_method_get_pointer");
        if (getPtr) target = getPtr(m);
    }
    if (!target) {
        // Fallback: first pointer-sized field of MethodInfo is methodPointer.
        target = *(void**)m;
    }
    if (!target) { LOGE("pump: OnEvent address unresolved"); return false; }

    if (!HookAttach((uintptr_t)target, (void*)OnEventDetour, (void**)&g_origOnEvent)) {
        LOGE("pump: hook attach failed");
        return false;
    }
    g_pumpReady = true;
    LOGI("pump: OnEvent hooked at %p", target);
    return true;
}

void Detach() {
    if (!g_pumpReady) return;
    HookDetachByDetour((void*)OnEventDetour);
    g_origOnEvent = nullptr;
    g_pumpReady = false;
}

bool Active() { return g_pumpReady; }
uint64_t EventsSeen()   { return g_eventsSeen; }
uint64_t AudioFrames()  { return g_audioFrames; }
uint64_t Dropped()      { return g_eventsDropped; }

} // namespace EventPump
