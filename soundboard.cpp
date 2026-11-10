#include "soundboard.hpp"
#include "tone.hpp"
#include <android/log.h>
#include <mutex>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "soundboard", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "soundboard", __VA_ARGS__)
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <cmath>

// ============================================================================
// Soundboard — 12 generated tone / effect clips
// Plays locally through the Quest headset speakers via OpenSLES.
// Each clip is a short synthesized PCM buffer (44100Hz, mono, 16-bit).
// ============================================================================

static SLObjectItf  g_engineObj  = nullptr;
static SLEngineItf  g_engine     = nullptr;
static SLObjectItf  g_mixObj     = nullptr;
static SLObjectItf  g_playerObj  = nullptr;
static SLPlayItf    g_play       = nullptr;
static SLAndroidSimpleBufferQueueItf g_bufQueue = nullptr;

static bool g_slReady = false;
static std::mutex g_audioMutex;
static void StopCurrent();
static void ShutdownSL() {
    StopCurrent();
    if (g_mixObj) (*g_mixObj)->Destroy(g_mixObj);
    if (g_engineObj) (*g_engineObj)->Destroy(g_engineObj);
    g_mixObj = nullptr;
    g_engineObj = nullptr;
    g_engine = nullptr;
    g_slReady = false;
}
static bool CheckSL(SLresult result, const char* operation) {
    if (result == SL_RESULT_SUCCESS) return true;
    LOGE("%s failed: %u", operation, static_cast<unsigned>(result));
    ShutdownSL();
    return false;
}

struct Clip {
    const char *name;
    std::vector<int16_t> pcm;
};

static std::vector<Clip> g_clips;

static void BuildClips() {
    g_clips.clear();
    // 12 fun clips
    g_clips.push_back({"Bruh",          MakeTone(120,  0.6f, 0)});   // low sine drop
    g_clips.push_back({"Vine Boom",     MakeTone(60,   0.3f, 1)});   // square thud
    g_clips.push_back({"Nope",          MakeTone(440,  0.1f, 1)});   // short buzz
    g_clips.push_back({"Oof",           MakeTone(200,  0.4f, 2)});   // sawtooth mid
    g_clips.push_back({"Air Horn",      MakeTone(880,  0.8f, 1)});   // square high
    g_clips.push_back({"Fart",          MakeTone(80,   0.5f, 3)});   // noise low
    g_clips.push_back({"Siren",         MakeTone(600,  1.0f, 4)});   // chirp up
    g_clips.push_back({"Laser",         MakeTone(1200, 0.4f, 4)});   // chirp high
    g_clips.push_back({"Sad Trombone",  MakeTone(220,  1.2f, 2)});   // saw down
    g_clips.push_back({"Achievement",   MakeTone(880,  0.3f, 0)});   // sine ding
    g_clips.push_back({"Explosion",     MakeTone(50,   0.7f, 3)});   // noise bass
    g_clips.push_back({"Level Up",      MakeTone(440,  0.5f, 4)});   // chirp medium
}

static bool InitSL() {
    if (g_slReady) return true;
    SLresult r;
    r = slCreateEngine(&g_engineObj, 0, nullptr, 0, nullptr, nullptr);
    if (!CheckSL(r, "SL engine create failed")) return false;
    r = (*g_engineObj)->Realize(g_engineObj, SL_BOOLEAN_FALSE);
    if (!CheckSL(r, "SL engine realize failed")) return false;
    r = (*g_engineObj)->GetInterface(g_engineObj, SL_IID_ENGINE, &g_engine);
    if (!CheckSL(r, "SL engine iface failed")) return false;
    r = (*g_engine)->CreateOutputMix(g_engine, &g_mixObj, 0, nullptr, nullptr);
    if (!CheckSL(r, "SL mix create failed")) return false;
    r = (*g_mixObj)->Realize(g_mixObj, SL_BOOLEAN_FALSE);
    if (!CheckSL(r, "SL mix realize failed")) return false;
    g_slReady = true;
    LOGI("OpenSLES ready");
    return true;
}

static void StopCurrent() {
    if (g_playerObj){(*g_playerObj)->Destroy(g_playerObj);g_playerObj=nullptr;g_play=nullptr;g_bufQueue=nullptr;}
}

bool Soundboard_Play(int idx) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    if (idx<0||idx>=(int)g_clips.size()) return false;
    if (!InitSL()) return false;
    StopCurrent();
    Clip &clip = g_clips[idx];
    if (clip.pcm.empty()) return false;

    SLDataLocator_AndroidSimpleBufferQueue bqLoc={SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,2};
    SLDataFormat_PCM fmt={
        SL_DATAFORMAT_PCM, 1, SL_SAMPLINGRATE_44_1,
        SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN
    };
    SLDataSource src={&bqLoc,&fmt};
    SLDataLocator_OutputMix outLoc={SL_DATALOCATOR_OUTPUTMIX,g_mixObj};
    SLDataSink sink={&outLoc,nullptr};
    const SLInterfaceID ids[]={SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean req[]={SL_BOOLEAN_TRUE};
    SLresult r=(*g_engine)->CreateAudioPlayer(g_engine,&g_playerObj,&src,&sink,1,ids,req);
    if (!CheckSL(r, "SL player create failed")) return false;
    r=(*g_playerObj)->Realize(g_playerObj,SL_BOOLEAN_FALSE);
    if (!CheckSL(r, "SL player realize failed")) return false;
    if (!CheckSL((*g_playerObj)->GetInterface(g_playerObj,SL_IID_PLAY,&g_play), "audio playback")) return false;
    if (!CheckSL((*g_playerObj)->GetInterface(g_playerObj,SL_IID_ANDROIDSIMPLEBUFFERQUEUE,&g_bufQueue), "audio playback")) return false;
    if (!CheckSL((*g_bufQueue)->Enqueue(g_bufQueue,clip.pcm.data(),(SLuint32)(clip.pcm.size()*sizeof(int16_t))), "audio playback")) return false;
    if (!CheckSL((*g_play)->SetPlayState(g_play,SL_PLAYSTATE_PLAYING), "audio playback")) return false;
    LOGI("Soundboard: playing '%s'", clip.name);
    return true;
}

int Soundboard_Count() { return 12; }
const char* Soundboard_Name(int i) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    return (i>=0&&i<(int)g_clips.size())?g_clips[i].name:"";
}

void Soundboard_Init() {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    if (!g_clips.empty()) return;
    BuildClips();
    LOGI("Soundboard: %d clips built", (int)g_clips.size());
}

void Soundboard_Shutdown() {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    ShutdownSL();
    g_clips.clear();
}
