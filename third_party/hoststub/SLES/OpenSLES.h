#pragma once
// Host syntax-check stub — mirrors the OpenSLES surface used by soundboard.cpp.
// Shapes match the NDK headers: interface handles are pointer-to-vtable.
#include <cstdint>
typedef int32_t  SLresult;
typedef uint32_t SLuint32;
typedef int8_t   SLboolean;
typedef const void* SLInterfaceID;

struct SLObjectItf_;
typedef const struct SLObjectItf_ * const * SLObjectItf;
struct SLObjectItf_ {
    SLresult (*Realize)(SLObjectItf, SLboolean);
    SLresult (*Resume)(SLObjectItf, SLboolean);
    SLresult (*GetState)(SLObjectItf, SLuint32*);
    SLresult (*GetInterface)(SLObjectItf, SLInterfaceID, void*);
    SLresult (*Destroy)(SLObjectItf);
};

struct SLEngineItf_;
typedef const struct SLEngineItf_ * const * SLEngineItf;
struct SLEngineItf_ {
    SLresult (*CreateAudioPlayer)(SLEngineItf, SLObjectItf*, void*, void*, SLuint32, const SLInterfaceID*, const SLboolean*);
    SLresult (*CreateOutputMix)(SLEngineItf, SLObjectItf*, SLuint32, const SLInterfaceID*, const SLboolean*);
};

struct SLPlayItf_;
typedef const struct SLPlayItf_ * const * SLPlayItf;
struct SLPlayItf_ { SLresult (*SetPlayState)(SLPlayItf, SLuint32); };

struct SLAndroidSimpleBufferQueueItf_;
typedef const struct SLAndroidSimpleBufferQueueItf_ * const * SLAndroidSimpleBufferQueueItf;
struct SLAndroidSimpleBufferQueueItf_ {
    SLresult (*Enqueue)(SLAndroidSimpleBufferQueueItf, const void*, SLuint32);
    SLresult (*Clear)(SLAndroidSimpleBufferQueueItf);
};

struct SLDataLocator_AndroidSimpleBufferQueue { SLuint32 locatorType; SLuint32 numBuffers; };
struct SLDataFormat_PCM { SLuint32 formatType, numChannels, samplesPerSec, bitsPerSample, containerSize, channelMask, endianness; };
struct SLDataSource { void* pLocator; void* pFormat; };
struct SLDataLocator_OutputMix { SLuint32 locatorType; SLObjectItf outputMix; };
struct SLDataSink { void* pLocator; void* pFormat; };

extern "C" SLresult slCreateEngine(SLObjectItf* pEngine, SLuint32 numOptions, const void* pEngineOptions,
                                   SLuint32 numInterfaces, const SLInterfaceID* pInterfaceIds,
                                   const SLboolean* pInterfaceRequired);
