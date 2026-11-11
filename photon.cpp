#include "shared.hpp"
#include <atomic>

// ============================================================================
// Photon bridge
//
// VRChat splits across two Phaser/Photon client surfaces on Quest:
//   * Photon.Realtime.LoadBalancingClient — OpRaiseEvent / OpKickPlayer
//   * Photon.Pun.PhotonNetwork      — the VRChat-facing facade
//
// Everything resolves through IL2CPP; nothing here hardcodes a vtable slot.
// RaiseEvent is called with a real managed byte[] and a real RaiseEventOptions,
// because the native side of Photon validates both.
// ============================================================================

namespace {

struct PhotonClasses {
    bool ready = false;
    Il2CppClass *LBClient       = nullptr;
    Il2CppClass *RaiseOpts      = nullptr;
    Il2CppClass *SendOpts       = nullptr;
    Il2CppClass *ReceiverGroup  = nullptr;
    Il2CppClass *ByteArray      = nullptr;   // Il2CppArray class for System.Byte
    Il2CppClass *PhotonNetwork  = nullptr;
    Il2CppClass *PlayerListCls  = nullptr;
    Il2CppClass *PlayerCls      = nullptr;

    MethodInfo  *OpRaiseEvent   = nullptr;
    MethodInfo  *OpKickPlayer   = nullptr;
    MethodInfo  *IsMaster       = nullptr;
    MethodInfo  *GetClient      = nullptr;   // PhotonNetwork.NetworkingClient
    MethodInfo  *GetLocalActor  = nullptr;   // PhotonNetwork.LocalPlayer.ActorNumber
    MethodInfo  *GetPlayerList  = nullptr;
    MethodInfo  *GetActorNumber = nullptr;
    MethodInfo  *GetNickName    = nullptr;
    MethodInfo  *RaiseOptsCtor  = nullptr;
    MethodInfo  *SendOptsCtor   = nullptr;
    MethodInfo  *SetIntProp     = nullptr;
    MethodInfo  *OpSetProps     = nullptr;
} P;

Ref   g_client;              // LoadBalancingClient* held alive
int   g_cachedLocalActor = 0;

// ---- small managed-value helpers ------------------------------------------
Il2CppObject* NewByteArray(size_t len) {
    if (!P.ByteArray || !IL2.arr_new) return nullptr;
    return (Il2CppObject*)IL2.arr_new(P.ByteArray, (uintptr_t)len);
}
void ByteArrayCopy(Il2CppObject* arr, const void* src, size_t len) {
    if (!arr || !src || !len) return;
    memcpy((uint8_t*)arr + kArrDataAt, src, len);
}

// int property write on RaiseEventOptions (TargetActors, Receivers, etc).
bool SetIntField(Il2CppObject* obj, const char* field, int value) {
    int off = IL2.FieldOffset(IL2.GetClass("", "RaiseEventOptions", "Photon.Realtime"), field);
    if (off < 0 || !obj) return false;
    *(int32_t*)((uint8_t*)obj + kObjHeader + off) = value;
    return true;
}

// ---- receiver constants ---------------------------------------------------
// Photon.Realtime.ReceiverGroup : All=0, Others=1, MasterClient=2
constexpr int kReceiverAll    = 0;
constexpr int kReceiverOthers = 1;

} // namespace

namespace Photon {

bool Init() {
    if (P.ready) return true;
    if (!IL2.ready) return false;

    P.LBClient      = IL2.GetClass("Photon.Realtime", "LoadBalancingClient", "PhotonRealtime");
    if (!P.LBClient) P.LBClient = IL2.GetClass("Photon.Realtime", "LoadBalancingClient", "Photon.Realtime");
    P.RaiseOpts     = IL2.GetClass("Photon.Realtime", "RaiseEventOptions", "PhotonRealtime");
    if (!P.RaiseOpts) P.RaiseOpts = IL2.GetClass("Photon.Realtime", "RaiseEventOptions", "Photon.Realtime");
    P.SendOpts      = IL2.GetClass("Photon.Realtime", "SendOptions", "PhotonRealtime");
    P.PhotonNetwork = IL2.GetClass("Photon.Pun", "PhotonNetwork", "PhotonUnityNetworking");
    if (!P.PhotonNetwork) P.PhotonNetwork = IL2.GetClass("Photon.Pun", "PhotonNetwork", "Assembly-CSharp");

    if (!P.LBClient) { LOGE("photon: LoadBalancingClient not found"); return false; }

    // OpRaiseEvent(byte, object, RaiseEventOptions, SendOptions) — 4 args
    P.OpRaiseEvent = IL2.GetMethod(P.LBClient, "OpRaiseEvent", 4);
    // OpKickPlayer(int, bool) — master-client only
    P.OpKickPlayer = IL2.GetMethod(P.LBClient, "OpKickPlayer", 2);
    // RaiseEventOptions ctor sets nothing; fields are written directly
    P.RaiseOptsCtor = IL2.GetMethod(P.RaiseOpts, ".ctor", 0);

    if (P.PhotonNetwork) {
        P.IsMaster      = IL2.GetMethod(P.PhotonNetwork, "get_IsMasterClient", 0);
        P.GetLocalActor = IL2.GetMethod(P.PhotonNetwork, "get_LocalPlayer", 0);
        P.GetPlayerList = IL2.GetMethod(P.PhotonNetwork, "get_PlayerList", 0);
        P.GetClient     = IL2.GetMethod(P.PhotonNetwork, "get_NetworkingClient", 0);
        // NetworkingClient is a static-ish instance; if the getter is absent we
        // walk LocalPlayer.ActorNumber from the same class.
    }

    P.PlayerCls     = IL2.GetClass("Photon.Realtime", "Player", "PhotonRealtime");
    if (P.PlayerCls) {
        P.GetActorNumber = IL2.GetMethod(P.PlayerCls, "get_ActorNumber", 0);
        P.GetNickName    = IL2.GetMethod(P.PlayerCls, "get_NickName", 0);
    }

    // Fetch the live client once. NetworkingClient is the canonical handle.
    if (P.GetClient) {
        Il2CppObject *c = IL2.Invoke(P.GetClient, nullptr);
        if (c) g_client.set(c);
    }

    P.ready = P.OpRaiseEvent && P.RaiseOpts && g_client;
    LOGI("photon %s (raise=%p kick=%p master=%p)", P.ready?"ready":"INCOMPLETE",
         (void*)P.OpRaiseEvent, (void*)P.OpKickPlayer, (void*)P.IsMaster);
    return P.ready;
}

bool Ready() { return P.ready && g_client; }

int LocalActor() {
    if (g_cachedLocalActor) return g_cachedLocalActor;
    if (!P.GetLocalActor || !P.GetActorNumber) return 0;
    Il2CppObject *pl = IL2.Invoke(P.GetLocalActor, nullptr);
    if (!pl) return 0;
    Il2CppObject *an = IL2.Invoke(P.GetActorNumber, pl);
    g_cachedLocalActor = an ? IL2.Unbox<int32_t>(an) : 0;
    return g_cachedLocalActor;
}

int PlayerCount() {
    if (!P.GetPlayerList) return 0;
    Il2CppArray *arr = (Il2CppArray*)IL2.Invoke(P.GetPlayerList, nullptr);
    if (!arr) return 0;
    size_t n = *(uintptr_t*)((uint8_t*)arr + kArrLengthAt);
    return (int)(n > 80 ? 80 : n);
}

int ActorAt(int idx) {
    if (!P.GetPlayerList || !P.GetActorNumber || idx < 0) return 0;
    Il2CppArray *arr = (Il2CppArray*)IL2.Invoke(P.GetPlayerList, nullptr);
    if (!arr) return 0;
    size_t n = *(uintptr_t*)((uint8_t*)arr + kArrLengthAt);
    if ((size_t)idx >= n || n > 80) return 0;
    Il2CppObject *pl = ((Il2CppObject**)((uint8_t*)arr + kArrDataAt))[idx];
    if (!pl) return 0;
    Il2CppObject *an = IL2.Invoke(P.GetActorNumber, pl);
    return an ? IL2.Unbox<int32_t>(an) : 0;
}

bool IsMaster() {
    if (!P.IsMaster) return false;
    Il2CppObject *r = IL2.Invoke(P.IsMaster, nullptr);
    return r && IL2.Unbox<bool>(r);
}

// Raw bytes → managed byte[] → OpRaiseEvent. Payload capped at 60 KB (Photon
// reliable ceiling is 64 KB with overhead; >60 KB is dropped, not fragmented).
bool Raise(uint8_t code, const void* payload, int len, int targetActor) {
    if (!Ready() || !P.OpRaiseEvent) return false;
    if (len < 0 || len > 60 * 1024) { LOGE("photon: payload %d out of bounds", len); return false; }

    Il2CppObject *bytes = NewByteArray((size_t)len);
    if (!bytes) return false;
    if (len > 0) ByteArrayCopy(bytes, payload, (size_t)len);

    Il2CppObject *opts = P.RaiseOptsCtor ? IL2.object_new(P.RaiseOpts) : nullptr;
    if (opts && P.RaiseOptsCtor) { IL2.Invoke(P.RaiseOptsCtor, opts); }

    int receivers = targetActor > 0 ? kReceiverOthers : kReceiverAll;
    if (opts) {
        SetIntField(opts, "Receivers", receivers);
        if (targetActor > 0) {
            // TargetActors is int[]; one actor. Build it, set the field.
            Il2CppClass *intCls = IL2.GetClass("System", "Int32", "mscorlib");
            if (intCls && IL2.arr_new) {
                Il2CppArray *ta = (Il2CppArray*)IL2.arr_new(intCls, 1);
                if (ta) {
                    ((int32_t*)((uint8_t*)ta + kArrDataAt))[0] = targetActor;
                    int off = IL2.FieldOffset(P.RaiseOpts, "TargetActors");
                    if (off >= 0) *(Il2CppObject**)((uint8_t*)opts + kObjHeader + off) = (Il2CppObject*)ta;
                }
            }
        }
    }

    // OpRaiseEvent(byte code, object payload, RaiseEventOptions, SendOptions)
    // SendOptions is a struct: pass a byte value (reliable = 1) with its address.
    uint8_t s = 1;
    uint8_t c = code;
    void *args[4] = { &c, bytes, opts, &s };
    IL2.Invoke(P.OpRaiseEvent, g_client.get(), args);   // exceptions logged in Invoke
    return true;
}

// Already-managed payload (used when the caller built a string/object).
bool RaiseRaw(uint8_t code, void* managedObj, int targetActor) {
    if (!Ready() || !P.OpRaiseEvent) return false;
    Il2CppObject *opts = P.RaiseOptsCtor ? IL2.object_new(P.RaiseOpts) : nullptr;
    if (opts && P.RaiseOptsCtor) { IL2.Invoke(P.RaiseOptsCtor, opts); }
    if (opts) {
        SetIntField(opts, "Receivers", targetActor > 0 ? kReceiverOthers : kReceiverAll);
        if (targetActor > 0) {
            Il2CppClass *intCls = IL2.GetClass("System", "Int32", "mscorlib");
            if (intCls && IL2.arr_new) {
                Il2CppArray *ta = (Il2CppArray*)IL2.arr_new(intCls, 1);
                if (ta) {
                    ((int32_t*)((uint8_t*)ta + kArrDataAt))[0] = targetActor;
                    int off = IL2.FieldOffset(P.RaiseOpts, "TargetActors");
                    if (off >= 0) *(Il2CppObject**)((uint8_t*)opts + kObjHeader + off) = (Il2CppObject*)ta;
                }
            }
        }
    }
    uint8_t s = 1;
    uint8_t c = code;
    void *args[4] = { &c, managedObj, opts, &s };
    IL2.Invoke(P.OpRaiseEvent, g_client.get(), args);
    return true;
}

bool SetActorProperties(int actor, void* managedHashtable) {
    if (!Ready() || actor <= 0 || !managedHashtable) return false;
    if (!P.OpSetProps) {
        P.OpSetProps = IL2.GetMethod(P.LBClient, "OpSetPropertiesOfActor", 4);
        if (!P.OpSetProps) return false;
    }
    void* args[4] = { &actor, managedHashtable, nullptr, nullptr };
    IL2.Invoke(P.OpSetProps, g_client.get(), args);
    return true;
}

void* HashtableClass() {
    return IL2.GetClass("System.Collections", "Hashtable", "mscorlib");
}

void* ByteClass() {
    return IL2.GetClass("System", "Byte", "mscorlib");
}

bool Kick(int actor) {
    if (!Ready() || !P.OpKickPlayer || actor <= 0) return false;
    if (!IsMaster()) { LOGE("photon: kick refused — not master client"); return false; }
    bool authCookie = false;
    void *args[2] = { &actor, &authCookie };
    IL2.Invoke(P.OpKickPlayer, g_client.get(), args);
    LOGI("photon: kick sent -> %d", actor);
    return true;
}

void Shutdown() {
    g_client.clear();
    g_cachedLocalActor = 0;
    P.ready = false;
}

} // namespace Photon
