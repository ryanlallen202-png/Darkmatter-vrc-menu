// soundboard.cpp — all-format audio decode + server-sided Photon broadcast
// FFmpegKit handles wav/mp3/ogg/flac/aiff/wma/m4a decode → mono PCM float[]
// Server-side: PCM chunks sent via Photon RaiseEvent 0xA0 to all clients
// Receiving clients reconstruct and play via VRChat's AudioSource invoke path

#include "soundboard.h"
#include "gunlib.h"
#include "il2cpp_init.h"
#include "offsets.h"
#include <android/log.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <mutex>
#include <thread>

// FFmpegKit Android — bundled AAR, all codecs
#include "ffmpegkit/FFmpegKit.h"
#include "ffmpegkit/FFmpegSession.h"

#define TAG "VRCMenu/Sound"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace Soundboard
{
    static const uint8_t kAudioEvent  = 0xA0;
    static const int     kChunkSamples = 4096;   // samples per Photon packet
    static const int     kSampleRate   = 44100;
    static const char*   kTmpPCM       = "/data/local/tmp/vrcmenu_pcm.raw";

    struct Clip
    {
        std::string       path;
        std::vector<float> pcm;
        int               sampleRate = 44100;
        bool              valid      = false;
    };

    static std::vector<Clip> g_clips;
    static std::mutex        g_clipMu;

    // ── IL2CPP resolved methods for remote audio playback ─────────────────
    // Receiving clients need to construct an AudioClip and play it.
    // We hook the Photon event dispatcher to intercept 0xA0 on all clients.
    static void* g_AudioClipCreate   = nullptr; // AudioClip.Create(name,len,ch,freq,stream)
    static void* g_AudioClipSetData  = nullptr; // AudioClip.SetData(float[],offset)
    static void* g_AudioSourcePlay   = nullptr; // AudioSource.Play()
    static void* g_AudioSourceStop   = nullptr; // AudioSource.Stop()
    static void* g_AudioSourceSetClip= nullptr; // AudioSource.set_clip
    static void* g_AudioSourceSetVol = nullptr; // AudioSource.set_volume
    static void* g_AudioSourceSetLoop= nullptr; // AudioSource.set_loop
    static void* g_PhotonEventHook   = nullptr; // ShadowHook on Photon event dispatch

    // Accumulation buffer for incoming PCM chunks (all-client receive path)
    struct RemoteBuf
    {
        std::vector<float> pcm;
        int  sampleRate = 44100;
        bool loop       = false;
        float vol       = 1.0f;
        bool ready      = false;
    };
    static RemoteBuf g_remoteBuf;
    static std::mutex g_remoteMu;
    static void* g_audioSource = nullptr;  // persistent AudioSource on local player GO

    // ── FFmpegKit decode → mono float PCM ────────────────────────────────
    static bool DecodeFile(const std::string& path,
                           std::vector<float>& outPCM, int& outSR)
    {
        // Remove stale tmp file
        remove(kTmpPCM);

        // Build FFmpeg command: decode any input → s16le mono 44100 raw
        std::string cmd =
            "-y -i \"" + path + "\""
            " -f s16le -acodec pcm_s16le -ac 1 -ar 44100"
            " \"" + kTmpPCM + "\"";

        LOGI("Decoding: %s", path.c_str());

        // Execute synchronously (FFmpegKit C API)
        // ffmpegkit_execute blocks until done
        auto session = FFmpegKit::execute(cmd.c_str());
        auto rc = session->getReturnCode();
        if (!ReturnCode::isSuccess(rc))
        {
            LOGE("FFmpeg decode failed: %s",
                 session->getFailStackTrace() ? session->getFailStackTrace() : "unknown");
            return false;
        }

        // Read raw PCM
        FILE* f = fopen(kTmpPCM, "rb");
        if (!f) { LOGE("PCM tmp file open failed"); return false; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0) { fclose(f); LOGE("PCM empty"); return false; }

        std::vector<int16_t> raw(sz / 2);
        fread(raw.data(), 2, raw.size(), f);
        fclose(f);
        remove(kTmpPCM);

        outPCM.resize(raw.size());
        for (size_t i = 0; i < raw.size(); i++)
            outPCM[i] = raw[i] / 32768.0f;

        outSR = kSampleRate;
        LOGI("Decoded %zu samples (%.2f sec) from %s",
             outPCM.size(), outPCM.size() / (float)kSampleRate, path.c_str());
        return true;
    }

    // ── Photon event dispatcher hook ──────────────────────────────────────
    // VRChat processes incoming Photon events in LoadBalancingClient.OnEvent
    // or a generated dispatch method. We hook it to intercept 0xA0 for playback.
    // Method name from IL2CPP patterns:
    //   ExitGames.Client.Photon.LoadBalancingClient.OnEvent(EventData)
    using OnEventFn = void(*)(void* client, void* eventData);
    static OnEventFn g_origOnEvent = nullptr;
    static void* g_onEventHook = nullptr;

    static void OnEventHook(void* client, void* eventData)
    {
        // Extract event code from EventData object
        // EventData.Code field at offset 0x10 (uint8_t after Il2CppObject header)
        uint8_t code = *reinterpret_cast<uint8_t*>(
            reinterpret_cast<uint8_t*>(eventData) + 0x10);

        if (code == kAudioEvent)
        {
            // CustomData field at offset 0x18 (object* = byte[] payload)
            void* customData = *reinterpret_cast<void**>(
                reinterpret_cast<uint8_t*>(eventData) + 0x18);

            if (customData)
            {
                // Il2CppArray: max_length @ 0x18, data @ 0x20
                int32_t len = *reinterpret_cast<int32_t*>(
                    reinterpret_cast<uint8_t*>(customData) + 0x18);
                uint8_t* payload = reinterpret_cast<uint8_t*>(customData) + 0x20;

                if (len >= 1)
                {
                    uint8_t cmd = payload[0];
                    std::lock_guard<std::mutex> lk(g_remoteMu);

                    if (cmd == 0xFF)  // STOP
                    {
                        g_remoteBuf.ready = false;
                        g_remoteBuf.pcm.clear();
                        // Stop playback — will be picked up in Update tick
                    }
                    else if (cmd == 0x01 && len >= 10)  // START
                    {
                        g_remoteBuf.pcm.clear();
                        g_remoteBuf.sampleRate = *reinterpret_cast<int32_t*>(payload + 1);
                        g_remoteBuf.loop       = payload[5] != 0;
                        memcpy(&g_remoteBuf.vol, payload + 6, 4);
                        // PCM data follows header (10 bytes)
                        int samples = (len - 10) / 4;
                        float* pcm_in = reinterpret_cast<float*>(payload + 10);
                        g_remoteBuf.pcm.insert(g_remoteBuf.pcm.end(),
                                               pcm_in, pcm_in + samples);
                    }
                    else if ((cmd == 0x02 || cmd == 0x03) && len >= 10)  // DATA / END
                    {
                        int samples = (len - 10) / 4;
                        float* pcm_in = reinterpret_cast<float*>(payload + 10);
                        g_remoteBuf.pcm.insert(g_remoteBuf.pcm.end(),
                                               pcm_in, pcm_in + samples);
                        if (cmd == 0x03) g_remoteBuf.ready = true;  // END → trigger play
                    }
                }
            }
        }

        // Always call original
        if (g_origOnEvent) g_origOnEvent(client, eventData);
    }

    // ── Init ──────────────────────────────────────────────────────────────
    void Init()
    {
        // Module deferred — resolve after IL2CPP ready
        LOGI("Soundboard init deferred.");
    }

    static bool EnsureResolved()
    {
        if (!il2cpp::Ready()) return false;
        if (g_AudioClipCreate) return true;

        g_AudioClipCreate = il2cpp::ResolveMethod(
            UNITY_ASSEMBLY, UNITY_NS, "AudioClip",
            "Create", 5);  // (string name, int len, int ch, int freq, bool stream)
        g_AudioClipSetData = il2cpp::ResolveMethod(
            UNITY_ASSEMBLY, UNITY_NS, "AudioClip",
            "SetData", 2);  // (float[] data, int offsetSamples)
        g_AudioSourcePlay = il2cpp::ResolveMethod(
            UNITY_ASSEMBLY, UNITY_NS, "AudioSource",
            "Play", 0);
        g_AudioSourceStop = il2cpp::ResolveMethod(
            UNITY_ASSEMBLY, UNITY_NS, "AudioSource",
            "Stop", 0);
        g_AudioSourceSetClip = il2cpp::ResolveMethod(
            UNITY_ASSEMBLY, UNITY_NS, "AudioSource",
            "set_clip", 1);
        g_AudioSourceSetVol = il2cpp::ResolveMethod(
            UNITY_ASSEMBLY, UNITY_NS, "AudioSource",
            "set_volume", 1);
        g_AudioSourceSetLoop = il2cpp::ResolveMethod(
            UNITY_ASSEMBLY, UNITY_NS, "AudioSource",
            "set_loop", 1);

        // Hook Photon OnEvent for receive path
        void* onEventPtr = il2cpp::ResolveMethod(
            PHOTON_ASSEMBLY, PHOTON_NS, PHOTON_CLASS_LBC,
            "OnEvent", 1);
        if (onEventPtr)
        {
            g_onEventHook = shadowhook_hook_func_addr(
                onEventPtr, (void*)OnEventHook, (void**)&g_origOnEvent);
            LOGI("Photon OnEvent hook: %s",
                 g_onEventHook ? "ok" : shadowhook_to_errmsg(shadowhook_get_errno()));
        }

        LOGI("Soundboard resolved: create=%p setdata=%p play=%p",
             g_AudioClipCreate, g_AudioClipSetData, g_AudioSourcePlay);
        return g_AudioClipCreate != nullptr;
    }

    // ── Build IL2CPP float[] from PCM data ────────────────────────────────
    static void* BuildFloatArray(const float* pcm, int len)
    {
        static void* s_float_class = nullptr;
        if (!s_float_class)
        {
            void* asmb = il2cpp::domain_assembly_open(il2cpp::g_domain, "mscorlib.dll");
            void* img  = il2cpp::assembly_get_image(asmb);
            s_float_class = il2cpp::class_from_name(img, "System", "Single");
        }
        if (!s_float_class || !il2cpp::array_new) return nullptr;
        void* arr = il2cpp::array_new(s_float_class, len);
        if (!arr) return nullptr;
        float* d = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(arr) + 0x20);
        memcpy(d, pcm, len * sizeof(float));
        return arr;
    }

    // ── Build an AudioClip IL2CPP object ──────────────────────────────────
    static void* BuildAudioClip(const float* pcm, int len,
                                 int sampleRate, const char* name)
    {
        if (!g_AudioClipCreate || !g_AudioClipSetData) return nullptr;
        void* exc = nullptr;

        void* nameStr = il2cpp::string_new(name);
        int32_t ch    = 1;
        bool    stream = false;
        void* args[5] = { nameStr, &len, &ch, &sampleRate, &stream };
        void* clip = il2cpp::runtime_invoke(g_AudioClipCreate, nullptr, args, &exc);
        if (!clip || exc) { LOGE("AudioClip.Create failed"); return nullptr; }

        void* floatArr = BuildFloatArray(pcm, len);
        if (!floatArr) return nullptr;

        int32_t offset = 0;
        void* sd_args[2] = { floatArr, &offset };
        il2cpp::runtime_invoke(g_AudioClipSetData, clip, sd_args, &exc);

        return clip;
    }

    // ── Tick — called from Java UI thread periodically to trigger playback ─
    // (Java side calls MenuBridge.nativeTick() from a Handler every 100ms)
    void Tick()
    {
        if (!EnsureResolved()) return;

        bool doPlay = false;
        RemoteBuf snap;
        {
            std::lock_guard<std::mutex> lk(g_remoteMu);
            if (g_remoteBuf.ready)
            {
                snap = g_remoteBuf;
                g_remoteBuf.ready = false;
                g_remoteBuf.pcm.clear();
                doPlay = true;
            }
        }

        if (!doPlay || snap.pcm.empty()) return;

        void* clip = BuildAudioClip(snap.pcm.data(), (int)snap.pcm.size(),
                                    snap.sampleRate, "vrcmenu_remote");
        if (!clip) return;

        // Get AudioSource from local player (cached)
        if (!g_audioSource)
        {
            // Allocate a new AudioSource on the local player GO via AddComponent
            // Simplified: resolve AddComponent and AudioSource type
            void* addComp = il2cpp::ResolveMethod(
                UNITY_ASSEMBLY, UNITY_NS, "GameObject",
                "AddComponent", 1);  // AddComponent(Type)
            static void* s_as_class = nullptr;
            if (!s_as_class)
            {
                void* asmb = il2cpp::domain_assembly_open(il2cpp::g_domain, UNITY_ASSEMBLY);
                void* img  = il2cpp::assembly_get_image(asmb);
                s_as_class = il2cpp::class_from_name(img, UNITY_NS, "AudioSource");
            }
            if (addComp && s_as_class && il2cpp::class_get_type)
            {
                // Need local GO — reuse Player module's GetLocalGO
                // For now: piggyback on a known GO; full impl hooks avatar load
                // FILL: wire up to Player::GetLocalGO() once modules share header
            }
        }

        if (!g_audioSource) return;

        void* exc = nullptr;
        // set_clip, set_loop, set_volume, Play
        void* clip_args[1]  = { clip };
        bool  loop          = snap.loop;
        float vol           = snap.vol;
        void* loop_args[1]  = { &loop };
        void* vol_args[1]   = { &vol };

        il2cpp::runtime_invoke(g_AudioSourceSetClip, g_audioSource, clip_args, &exc);
        il2cpp::runtime_invoke(g_AudioSourceSetLoop, g_audioSource, loop_args, &exc);
        il2cpp::runtime_invoke(g_AudioSourceSetVol,  g_audioSource, vol_args,  &exc);
        il2cpp::runtime_invoke(g_AudioSourcePlay,    g_audioSource, nullptr,   &exc);
        LOGI("Remote audio playing (%zu samples)", snap.pcm.size());
    }

    // ── Public API ────────────────────────────────────────────────────────
    bool LoadClip(const char* path)
    {
        Clip c;
        c.path = path;
        c.valid = DecodeFile(path, c.pcm, c.sampleRate);
        if (!c.valid) { LOGE("Load failed: %s", path); return false; }
        std::lock_guard<std::mutex> lk(g_clipMu);
        g_clips.push_back(std::move(c));
        LOGI("Clip loaded [%zu]: %s", g_clips.size() - 1, path);
        return true;
    }

    void PlayServer(int idx, bool loop, float vol)
    {
        if (!EnsureResolved()) { LOGE("IL2CPP not ready"); return; }

        Clip clip;
        {
            std::lock_guard<std::mutex> lk(g_clipMu);
            if (idx < 0 || idx >= (int)g_clips.size()) return;
            if (!g_clips[idx].valid) return;
            clip = g_clips[idx];  // copy for thread safety
        }

        // Send PCM in chunks via Photon
        // Packet layout (bytes):
        //   [0]     cmd (0x01=START, 0x02=DATA, 0x03=END)
        //   [1-4]   sampleRate (int32 LE)
        //   [5]     loop (bool)
        //   [6-9]   volume (float32 LE)
        //   [10..N] PCM chunk (float32 LE samples)
        const int kHeaderSize = 10;
        int total  = (int)clip.pcm.size();
        int offset = 0;
        bool first = true;

        std::vector<uint8_t> packet(kHeaderSize + kChunkSamples * 4);

        while (offset < total)
        {
            int chunkLen = std::min(kChunkSamples, total - offset);
            bool last    = (offset + chunkLen) >= total;

            uint8_t cmd = first ? 0x01 : (last ? 0x03 : 0x02);
            packet[0] = cmd;
            int32_t sr = clip.sampleRate;
            memcpy(&packet[1], &sr, 4);
            packet[5] = loop ? 1 : 0;
            memcpy(&packet[6], &vol, 4);
            memcpy(&packet[kHeaderSize], clip.pcm.data() + offset, chunkLen * 4);

            int pktLen = kHeaderSize + chunkLen * 4;
            Gunlib::PhotonRaiseEventBytes(kAudioEvent, packet.data(), pktLen,
                                          1 /* All */, nullptr, 0, true);
            offset += chunkLen;
            first   = false;
        }
        LOGI("Server audio sent: %d samples, loop=%d vol=%.2f", total, loop, vol);
    }

    void StopAll()
    {
        uint8_t stop = 0xFF;
        Gunlib::PhotonRaiseEventBytes(kAudioEvent, &stop, 1,
                                      1 /* All */, nullptr, 0, true);
        LOGI("StopAll sent.");
    }

    int  ClipCount()
    {
        std::lock_guard<std::mutex> lk(g_clipMu);
        return (int)g_clips.size();
    }

    const char* ClipName(int idx)
    {
        std::lock_guard<std::mutex> lk(g_clipMu);
        if (idx < 0 || idx >= (int)g_clips.size()) return "";
        // Return just filename portion
        const std::string& p = g_clips[idx].path;
        size_t pos = p.rfind('/');
        return (pos == std::string::npos) ? p.c_str() : p.c_str() + pos + 1;
    }

    void SetAudioSource(void* src) { g_audioSource = src; }
}
