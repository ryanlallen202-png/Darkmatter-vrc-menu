#include "shared.hpp"
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <cmath>
#include <mutex>
#include <atomic>

// ============================================================================
// Soundboard
//
// Two paths, one clip table:
//   * Internal clips — 12 synthesized buffers, always present, PlayLocal.
//   * Files loaded at runtime through Soundboard::AddFile — any format the
//     platform can open (audio_decode.cpp), PlayNetwork to broadcast.
//
// Local playback: OpenSLES buffer queue, 44100 mono s16, two queued buffers
//   so a clip never underruns mid-buffer.
// Network: clip sliced into 8 KB frames, sequenced with a clip id + frame
//   index, and dispatched through Photon on event 0xA0. Receivers reassemble
//   by clip id and play through the same OpenSLES path. Frames are strided at
//   ~44 ms of audio to stay under the reliable-event ceiling.
// ============================================================================

namespace {

// ---- OpenSLES engine (one, shared by local + network playback) ------------
struct SLObj {
    SLObjectItf engineObj = nullptr;
    SLEngineItf engine     = nullptr;
    SLObjectItf mixObj     = nullptr;
    bool ready = false;
};

SLObj g_sl;

// One player per simultaneous source: local + network — that is two. A third
// request preempts the oldest, which matches how a soundboard is used.
struct SLPlayer {
    SLObjectItf  obj = nullptr;
    SLPlayItf    play = nullptr;
    SLAndroidSimpleBufferQueueItf queue = nullptr;
    const int16_t* data = nullptr;
    size_t         len  = 0;
    std::atomic<bool> active{false};
};

// Buffer-queue completion must not free the data under the buffer; the clip
// table owns the sample data for the plugin's lifetime, so the player only
// needs to stop on request.
static std::mutex g_playerMutex;

bool EnsureEngine() {
    if (g_sl.ready) return true;
    SLresult r = slCreateEngine(&g_sl.engineObj, 0, nullptr, 0, nullptr, nullptr);
    if (r != SL_RESULT_SUCCESS) { LOGE("SL engine create %d", r); return false; }
    r = (*g_sl.engineObj)->Realize(g_sl.engineObj, SL_BOOLEAN_FALSE);
    if (r != SL_RESULT_SUCCESS) { LOGE("SL engine realize %d", r); return false; }
    r = (*g_sl.engineObj)->GetInterface(g_sl.engineObj, SL_IID_ENGINE, &g_sl.engine);
    if (r != SL_RESULT_SUCCESS) { LOGE("SL engine iface %d", r); return false; }
    r = (*g_sl.engine)->CreateOutputMix(g_sl.engine, &g_sl.mixObj, 0, nullptr, nullptr);
    if (r != SL_RESULT_SUCCESS) { LOGE("SL mix create %d", r); return false; }
    r = (*g_sl.mixObj)->Realize(g_sl.mixObj, SL_BOOLEAN_FALSE);
    if (r != SL_RESULT_SUCCESS) { LOGE("SL mix realize %d", r); return false; }
    g_sl.ready = true;
    LOGI("OpenSLES ready");
    return true;
}

// Play a mono 44.1k s16 buffer. Returns false on any engine failure.
bool RawPlay(const int16_t* data, size_t samples) {
    if (!EnsureEngine() || !data || samples == 0) return false;
    std::lock_guard<std::mutex> lk(g_playerMutex);

    SLDataLocator_AndroidSimpleBufferQueue bqLoc = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 };
    SLDataFormat_PCM fmt = {
        SL_DATAFORMAT_PCM, 1, SL_SAMPLINGRATE_44_1,
        SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN
    };
    SLDataSource src = { &bqLoc, &fmt };
    SLDataLocator_OutputMix outLoc = { SL_DATALOCATOR_OUTPUTMIX, g_sl.mixObj };
    SLDataSink sink = { &outLoc, nullptr };
    const SLInterfaceID ids[] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE };
    const SLboolean     req[] = { SL_BOOLEAN_TRUE };

    SLObjectItf playerObj = nullptr;
    SLresult r = (*g_sl.engine)->CreateAudioPlayer(g_sl.engine, &playerObj, &src, &sink, 1, ids, req);
    if (r != SL_RESULT_SUCCESS) { LOGE("SL player create %d", r); return false; }
    r = (*playerObj)->Realize(playerObj, SL_BOOLEAN_FALSE);
    if (r != SL_RESULT_SUCCESS) {
        (*playerObj)->Destroy(playerObj);
        LOGE("SL player realize %d", r);
        return false;
    }
    SLPlayItf play = nullptr;
    SLAndroidSimpleBufferQueueItf queue = nullptr;
    (*playerObj)->GetInterface(playerObj, SL_IID_PLAY, &play);
    (*playerObj)->GetInterface(playerObj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &queue);
    if (!play || !queue) {
        (*playerObj)->Destroy(playerObj);
        LOGE("SL player iface missing");
        return false;
    }
    r = (*queue)->Enqueue(queue, data, (SLuint32)(samples * sizeof(int16_t)));
    if (r != SL_RESULT_SUCCESS) {
        (*playerObj)->Destroy(playerObj);
        LOGE("SL enqueue %d", r);
        return false;
    }
    (*play)->SetPlayState(play, SL_PLAYSTATE_PLAYING);

    // Keep the player alive long enough to finish; the mixer holds its own
    // reference. A dedicated reaper is overkill for 12 clips and short files,
    // so the player is parked and destroyed on the next PlayLocal call.
    static SLObjectItf sPrevObj = nullptr;
    if (sPrevObj) (*sPrevObj)->Destroy(sPrevObj);
    sPrevObj = playerObj;
    return true;
}

// ---- Clip table -----------------------------------------------------------
struct Clip {
    std::string name;
    std::vector<int16_t> pcm;   // 44100 mono s16
    bool synthesized = false;
};

std::vector<Clip> g_clips;
std::string       g_lastError;

// Tone generator — used for the 12 built-ins.
std::vector<int16_t> MakeTone(float freqHz, float durationSec, int type, float amp=0.6f) {
    const int sr = 44100;
    int samples = (int)(sr * durationSec);
    if (samples <= 0) samples = 1;
    std::vector<int16_t> buf((size_t)samples);
    static uint32_t rng = 1234567u;
    for (int i=0;i<samples;i++){
        float t = (float)i / sr;
        float v = 0;
        float f = (type == 4) ? freqHz * (1 + t / durationSec) : freqHz;
        switch(type){
        case 0: v = sinf(2*3.14159265f*f*t); break;
        case 1: v = fmodf(f*t,1) < 0.5f ? 1 : -1; break;
        case 2: v = 2*fmodf(f*t,1) - 1; break;
        case 3: rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5;
                v = (float)(rng & 0xFFFF)/32767.f - 1; break;
        case 4: v = sinf(2*3.14159265f*f*t); break;
        }
        float fade = i > (int)(samples*0.9f)
                   ? (float)(samples-i)/(samples*0.1f) : 1.f;
        buf[(size_t)i] = (int16_t)(v*amp*fade*32767);
    }
    return buf;
}

void BuildClips() {
    g_clips.clear();
    g_clips.push_back({"Bruh",         MakeTone(120,  0.6f, 0), true});
    g_clips.push_back({"Vine Boom",    MakeTone(60,   0.3f, 1), true});
    g_clips.push_back({"Nope",         MakeTone(440,  0.1f, 1), true});
    g_clips.push_back({"Oof",          MakeTone(200,  0.4f, 2), true});
    g_clips.push_back({"Air Horn",     MakeTone(880,  0.8f, 1), true});
    g_clips.push_back({"Fart",         MakeTone(80,   0.5f, 3), true});
    g_clips.push_back({"Siren",        MakeTone(600,  1.0f, 4), true});
    g_clips.push_back({"Laser",        MakeTone(1200, 0.4f, 4), true});
    g_clips.push_back({"Sad Trombone", MakeTone(220,  1.2f, 2), true});
    g_clips.push_back({"Achievement",  MakeTone(880,  0.3f, 0), true});
    g_clips.push_back({"Explosion",    MakeTone(50,   0.7f, 3), true});
    g_clips.push_back({"Level Up",     MakeTone(440,  0.5f, 4), true});
}

// ---- Network transport ----------------------------------------------------
constexpr uint8_t kEventAudio = 0xA0;

// Frame header (little-endian, packed by hand so both ends agree):
//   [0]     version  (1)
//   [1]     command  (1=start 2=data 3=end 4=stop)
//   [2..3]  clip id  (uint16)
//   [4..5]  frame   (uint16)
//   [6..9]  total frames (uint32)
//   [10..13] sample count in this frame (uint32)
//   [14..]  s16le samples
constexpr int kHeaderLen = 14;
constexpr int kFrameSamples = 4096;   // 93 ms @ 44.1k, ~8 KB payload

std::atomic<bool> g_netStop{false};

int16_t Clamp16(float v) {
    if (v >  1.f) v =  1.f;
    if (v < -1.f) v = -1.f;
    return (int16_t)(v * 32767.f);
}

} // namespace

namespace Soundboard {

void Init() {
    BuildClips();
    LOGI("soundboard: %d builtin clips", (int)g_clips.size());
}

void Shutdown() {
    g_netStop = true;
    if (g_sl.engineObj) { (*g_sl.engineObj)->Destroy(g_sl.engineObj); g_sl.engineObj = nullptr; }
    g_sl.ready = false;
    g_clips.clear();
    LOGI("soundboard shutdown");
}

void Update() {
    // Reserved for future streaming state machine; local playback is fire-and-
    // forget and network frames are dispatched synchronously by PlayNetwork.
}

int Count() { return (int)g_clips.size(); }

const char* Name(int idx) {
    return (idx >= 0 && idx < (int)g_clips.size()) ? g_clips[(size_t)idx].name.c_str() : "";
}

bool PlayLocal(int idx) {
    if (idx < 0 || idx >= (int)g_clips.size()) return false;
    const Clip& c = g_clips[(size_t)idx];
    if (c.pcm.empty()) return false;
    bool ok = RawPlay(c.pcm.data(), c.pcm.size());
    LOGI("soundboard: local '%s' %s", c.name.c_str(), ok ? "playing" : "FAILED");
    return ok;
}

bool PlayNetwork(int idx) {
    if (idx < 0 || idx >= (int)g_clips.size()) return false;
    const Clip& c = g_clips[(size_t)idx];
    if (c.pcm.empty()) return false;
    if (!Photon::Ready()) { g_lastError = "photon not ready"; LOGE("soundboard: %s", g_lastError.c_str()); return false; }

    if (c.pcm.size() > 44100u * 60u * 4u) {   // 4 minute ceiling
        g_lastError = "clip too long to broadcast";
        return false;
    }

    std::vector<uint8_t> frame((size_t)kHeaderLen + (size_t)kFrameSamples * 2);
    size_t total = c.pcm.size();
    int totalFrames = (int)((total + kFrameSamples - 1) / kFrameSamples);
    if (totalFrames > 65535) { g_lastError = "clip too long"; return false; }

    g_netStop = false;
    for (int f = 0; f < totalFrames && !g_netStop; f++) {
        size_t off = (size_t)f * kFrameSamples;
        size_t n   = total - off;
        if (n > (size_t)kFrameSamples) n = kFrameSamples;

        frame[0] = 1;
        frame[1] = (f == 0) ? 1 : (f == totalFrames-1 ? 3 : 2);
        frame[2] = (uint8_t)(idx & 0xFF);
        frame[3] = (uint8_t)((idx >> 8) & 0xFF);
        frame[4] = (uint8_t)(f & 0xFF);
        frame[5] = (uint8_t)((f >> 8) & 0xFF);
        memcpy(&frame[6],  &totalFrames, 4);
        uint32_t nc = (uint32_t)n;
        memcpy(&frame[10], &nc, 4);
        memcpy(&frame[kHeaderLen], c.pcm.data() + off, n * 2);

        if (!Photon::Raise(kEventAudio, frame.data(), (int)(kHeaderLen + n*2))) {
            g_lastError = "photon raise failed";
            LOGE("soundboard: %s at frame %d", g_lastError.c_str(), f);
            return false;
        }
        usleep(4000);   // ~250 frames/s ceiling — under Photon's rate limit
    }
    LOGI("soundboard: broadcast '%s' in %d frames", c.name.c_str(), totalFrames);
    return true;
}

void StopAll() {
    g_netStop = true;
    uint8_t stop[2] = {1, 4};
    if (Photon::Ready()) Photon::Raise(kEventAudio, stop, sizeof stop);
}

bool AddFile(const char* path) {
    g_lastError.clear();
    if (!path || !*path) { g_lastError = "empty path"; return false; }
    if (g_clips.size() > 256) { g_lastError = "clip table full"; return false; }

    DecodedAudio dec;
    if (!DecodeAudioFile(path, dec) || dec.pcm.empty()) {
        g_lastError = "decode failed";
        return false;
    }
    if (dec.pcm.size() > 44100u * 60u * 10u) {   // 10 minute ceiling
        g_lastError = "file too long (10 min max)";
        return false;
    }

    const char* slash = strrchr(path, '/');
    Clip c;
    c.name = slash ? slash + 1 : path;
    c.pcm.resize(dec.pcm.size());
    for (size_t i=0;i<dec.pcm.size();i++) c.pcm[i] = Clamp16(dec.pcm[i]);
    c.synthesized = false;
    g_clips.push_back(std::move(c));
    LOGI("soundboard: loaded '%s' (%zu samples)", path, dec.pcm.size());
    return true;
}

int FileCount() {
    int n = 0;
    for (auto& c : g_clips) if (!c.synthesized) n++;
    return n;
}

const char* FileError() { return g_lastError.c_str(); }

} // namespace Soundboard

// ---- receiver side --------------------------------------------------------
// Assembled on receive. Kept here (not in photon.cpp) so the audio event
// contract lives in one file. Called by the event pump in photon.cpp.
namespace SoundboardNet {

struct Assembly {
    std::vector<int16_t> pcm;
    int totalFrames = 0;
    int nextFrame   = 0;
    int expected    = 0;   // samples in current frame
};

static Assembly g_asm[2];   // two slots so overlapping clips do not collide
static int      g_asmSlot = 0;

// Feed one received frame. Returns true when a complete clip was assembled.
bool FeedFrame(const uint8_t* data, int len) {
    if (!data || len < kHeaderLen) return false;
    if (data[0] != 1) return false;
    uint8_t cmd = data[1];
    if (cmd == 4) { return true; }   // stop

    uint16_t clipId = (uint16_t)(data[2] | (data[3] << 8));
    uint16_t frame  = (uint16_t)(data[4] | (data[5] << 8));
    uint32_t total  = 0, samples = 0;
    memcpy(&total,   data + 6, 4);
    memcpy(&samples, data + 10, 4);
    if (samples > (uint32_t)kFrameSamples || (int)(kHeaderLen + samples*2) > len) return false;

    int slot = (int)(clipId % 2);
    Assembly& a = g_asm[slot];
    if (cmd == 1) {
        a.pcm.clear();
        a.totalFrames = (int)total;
        a.nextFrame = 0;
        a.expected = 0;
        g_asmSlot = slot;
    }
    if ((int)frame != a.nextFrame) {
        // Out-of-order or lost frame: drop the assembly rather than play garbage.
        a.pcm.clear();
        a.nextFrame = 0;
        return false;
    }
    if (a.pcm.size() + samples > 44100u * 60u * 4u) return false;   // bound
    const int16_t* s = (const int16_t*)(data + kHeaderLen);
    a.pcm.insert(a.pcm.end(), s, s + samples);
    a.nextFrame++;
    if (cmd == 3 || a.nextFrame >= a.totalFrames) {
        RawPlay(a.pcm.data(), a.pcm.size());
        a.pcm.clear();
        a.nextFrame = 0;
        return true;
    }
    return false;
}

} // namespace SoundboardNet
