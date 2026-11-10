#include "shared.hpp"
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

// Generate a simple tone into a buffer
// type: 0=sine, 1=square, 2=sawtooth, 3=noise, 4=chirp
static std::vector<int16_t> MakeTone(float freqHz, float durationSec, int type, float amp=0.6f) {
    int sr = 44100;
    int samples = (int)(sr * durationSec);
    std::vector<int16_t> buf(samples);
    static uint32_t rng = 1234567u;
    for (int i=0;i<samples;i++){
        float t = (float)i / sr;
        float v = 0;
        float f = type==4 ? freqHz*(1+t/durationSec) : freqHz;
        switch(type){
        case 0: v=sinf(2*3.14159f*f*t); break;
        case 1: v=fmodf(f*t,1)<0.5f?1:-1; break;
        case 2: v=2*fmodf(f*t,1)-1; break;
        case 3: rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;v=(float)(rng&0xffff)/32767.f-1; break;
        case 4: v=sinf(2*3.14159f*f*t); break;
        }
        // Fade out last 10%
        float fade = i>(int)(samples*0.9f)?(float)(samples-i)/(samples*0.1f):1.f;
        buf[i]=(int16_t)(v*amp*fade*32767);
    }
    return buf;
}

struct Clip {
    const char *name;
    std::vector<int16_t> pcm;
};

static std::vector<Clip> g_clips;
static int g_playingClip = -1;

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
    if (r!=SL_RESULT_SUCCESS){LOGE("SL engine create failed %d",r);return false;}
    r = (*g_engineObj)->Realize(g_engineObj, SL_BOOLEAN_FALSE);
    if (r!=SL_RESULT_SUCCESS){LOGE("SL engine realize failed %d",r);return false;}
    r = (*g_engineObj)->GetInterface(g_engineObj, SL_IID_ENGINE, &g_engine);
    if (r!=SL_RESULT_SUCCESS){LOGE("SL engine iface failed %d",r);return false;}
    SLInterfaceID mixIds[]={};
    SLboolean mixReq[]={};
    r = (*g_engine)->CreateOutputMix(g_engine, &g_mixObj, 0, mixIds, mixReq);
    if (r!=SL_RESULT_SUCCESS){LOGE("SL mix create failed %d",r);return false;}
    r = (*g_mixObj)->Realize(g_mixObj, SL_BOOLEAN_FALSE);
    if (r!=SL_RESULT_SUCCESS){LOGE("SL mix realize failed %d",r);return false;}
    g_slReady = true;
    LOGI("OpenSLES ready");
    return true;
}

static void StopCurrent() {
    if (g_playerObj){(*g_playerObj)->Destroy(g_playerObj);g_playerObj=nullptr;g_play=nullptr;g_bufQueue=nullptr;}
}

void Soundboard_Play(int idx) {
    if (idx<0||idx>=(int)g_clips.size()) return;
    if (!InitSL()) return;
    StopCurrent();
    Clip &clip = g_clips[idx];
    if (clip.pcm.empty()) return;

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
    if(r!=SL_RESULT_SUCCESS){LOGE("SL player create failed %d",r);return;}
    r=(*g_playerObj)->Realize(g_playerObj,SL_BOOLEAN_FALSE);
    if(r!=SL_RESULT_SUCCESS){LOGE("SL player realize failed %d",r);return;}
    (*g_playerObj)->GetInterface(g_playerObj,SL_IID_PLAY,&g_play);
    (*g_playerObj)->GetInterface(g_playerObj,SL_IID_ANDROIDSIMPLEBUFFERQUEUE,&g_bufQueue);
    (*g_bufQueue)->Enqueue(g_bufQueue,clip.pcm.data(),(SLuint32)(clip.pcm.size()*sizeof(int16_t)));
    (*g_play)->SetPlayState(g_play,SL_PLAYSTATE_PLAYING);
    g_playingClip = idx;
    LOGI("Soundboard: playing '%s'", clip.name);
}

int Soundboard_Count()            { return (int)g_clips.size(); }
const char* Soundboard_Name(int i){ return (i>=0&&i<(int)g_clips.size())?g_clips[i].name:""; }

void Soundboard_Init() {
    BuildClips();
    LOGI("Soundboard: %d clips built", (int)g_clips.size());
}
