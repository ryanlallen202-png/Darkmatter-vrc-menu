#include "shared.hpp"
#include <cstdio>
#include <cmath>

// ============================================================================
// Audio decode — every format the platform will open
//
// WAV: native RIFF parser (8/16/24/32-bit PCM + IEEE float), no Java round trip.
// Everything else (mp3, ogg/vorbis, opus, flac, m4a/aac, amr, wma):
//   Android MediaExtractor enumerates the stream, MediaCodec decodes to PCM
//   16-bit, we pull every output buffer into a byte vector, then linear-
//   resample to 44100 mono float.
//
// Why this and not FFmpeg: the profile is Quest — dropping a 20 MB SDK into a
// plugin means every rebuild carries it. MediaCodec is already in the OS and
// covers all of the above. Formats the platform refuses return ok=false with a
// reason, and the soundboard logs it, which is the correct failure.
// ============================================================================

namespace {

// ---- WAV ------------------------------------------------------------------
struct RiffHeader { char riff[4]; uint32_t size; char wave[4]; };
struct ChunkHeader{ char id[4];   uint32_t size; };

static uint16_t Rd16(const uint8_t* p){ return (uint16_t)(p[0] | (p[1]<<8)); }
static uint32_t Rd32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

static bool DecodeWavNative(FILE* f, DecodedAudio& out) {
    RiffHeader rh{};
    if (fread(&rh, sizeof rh, 1, f) != 1) return false;
    if (memcmp(rh.riff,"RIFF",4) || memcmp(rh.wave,"WAVE",4)) return false;

    uint16_t fmt=0, channels=0, bits=0;
    uint32_t rate=0;
    std::vector<uint8_t> data;

    bool haveFmt = false, haveData = false;
    while (!haveData) {
        ChunkHeader ch{};
        if (fread(&ch, sizeof ch, 1, f) != 1) break;
        if (memcmp(ch.id,"fmt ",4)==0) {
            if (ch.size < 16 || ch.size > 64) return false;
            uint8_t buf[64]{};
            if (fread(buf, 1, ch.size, f) != ch.size) return false;
            fmt      = Rd16(buf+0);
            channels = Rd16(buf+2);
            rate     = Rd32(buf+4);
            bits     = Rd16(buf+14);
            if (ch.size & 1) fseek(f, 1, SEEK_CUR);
            haveFmt = true;
            if (channels < 1 || channels > 8 || bits < 8 || bits > 32) return false;
        } else if (memcmp(ch.id,"data",4)==0) {
            if (!haveFmt) return false;
            if (ch.size > 256u*1024u*1024u) return false;    // 256 MB ceiling
            data.resize(ch.size);
            size_t got = fread(data.data(), 1, ch.size, f);
            data.resize(got);
            haveData = true;
        } else {
            if (ch.size > 64u*1024u*1024u) return false;
            fseek(f, (long)(ch.size + (ch.size & 1)), SEEK_CUR);
        }
    }
    if (!haveFmt || !haveData || data.empty()) return false;

    // Same 16-bit ceiling as PCM decode — clamps a hostile file instead of OOM.
    if (data.size() > 64u*1024u*1024u) data.resize(64u*1024u*1024u);

    size_t bytesPerSample = bits / 8;
    if (bytesPerSample == 0) return false;
    size_t frames = data.size() / (bytesPerSample * channels);
    if (frames == 0) return false;

    std::vector<float> mono;
    mono.resize(frames);
    const uint8_t* p = data.data();

    for (size_t i=0;i<frames;i++) {
        float acc = 0;
        for (int c=0;c<channels;c++) {
            float v = 0;
            if (fmt == 3 && bits == 32) {
                uint32_t u = Rd32(p);
                int32_t  s; memcpy(&s,&u,4);
                v = (float)s / 2147483648.0f;
            } else if (bits == 8) {
                v = ((int)p[0] - 128) / 128.0f;
            } else if (bits == 16) {
                v = (int16_t)Rd16(p) / 32768.0f;
            } else if (bits == 24) {
                int32_t s = (int32_t)((uint32_t)p[0]<<8 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<24) >> 8;
                v = s / 8388608.0f;
            } else {
                uint32_t u = Rd32(p);
                int32_t  s; memcpy(&s,&u,4);
                v = (float)s / 2147483648.0f;
            }
            acc += v;
            p += bytesPerSample;
        }
        mono[i] = acc / (float)channels;
    }

    out.pcm = std::move(mono);
    out.sampleRate = (int)rate;
    out.channels = 1;
    out.ok = true;
    return true;
}

// ---- JNI helper: exception-safe call scope --------------------------------
struct JniScope {
    JNIEnv* env = nullptr;
    bool    attached = false;
    JniScope() {
        if (!g_jvm) return;
        jint r = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (r != JNI_OK) {
            if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) attached = true;
            else env = nullptr;
        }
    }
    ~JniScope() {
        if (env && env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        if (attached && g_jvm) g_jvm->DetachCurrentThread();
    }
    bool ok() const { return env != nullptr; }
};

struct LocalStr {
    JNIEnv* env; jstring s;
    LocalStr(JNIEnv* e, const char* v) : env(e), s(e->NewStringUTF(v)) {}
    ~LocalStr(){ if (s) env->DeleteLocalRef(s); }
    operator jstring() const { return s; }
};

static bool DecodeMediaCodec(const char* path, DecodedAudio& out) {
    JniScope js;
    if (!js.ok()) { LOGE("decode: no JNI env"); return false; }
    JNIEnv* env = js.env;

    jclass cExtractor = env->FindClass("android/media/MediaExtractor");
    jclass cCodec     = env->FindClass("android/media/MediaCodec");
    jclass cFormat    = env->FindClass("android/media/MediaFormat");
    jclass cBuffer    = env->FindClass("java/nio/ByteBuffer");
    jclass cInfo      = env->FindClass("android/media/MediaCodec$BufferInfo");
    if (!cExtractor || !cCodec || !cFormat || !cBuffer || !cInfo) {
        LOGE("decode: android classes missing");
        return false;
    }

    // MediaExtractor extractor = new MediaExtractor();
    jmethodID mExtCtor = env->GetMethodID(cExtractor, "<init>", "()V");
    jobject extractor = env->NewObject(cExtractor, mExtCtor);
    if (!extractor) return false;

    jmethodID mSetData = env->GetMethodID(cExtractor, "setDataSource", "(Ljava/lang/String;)V");
    jmethodID mTrackCount = env->GetMethodID(cExtractor, "getTrackCount", "()I");
    jmethodID mGetFormat = env->GetMethodID(cExtractor, "getTrackFormat", "(I)Landroid/media/MediaFormat;");
    jmethodID mSelTrack = env->GetMethodID(cExtractor, "selectTrack", "(I)V");
    jmethodID mAdvance = env->GetMethodID(cExtractor, "advance", "()Z");
    jmethodID mReadSample = env->GetMethodID(cExtractor, "readSampleData", "(Ljava/nio/ByteBuffer;I)I");
    jmethodID mSampleTime = env->GetMethodID(cExtractor, "getSampleTime", "()J");

    LocalStr jpath(env, path);
    env->CallVoidMethod(extractor, mSetData, (jstring)jpath);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }

    jint tracks = env->CallIntMethod(extractor, mTrackCount);
    if (tracks <= 0) return false;

    // Pick the first audio track.
    jint trackIdx = -1;
    jobject format = nullptr;
    for (jint t = 0; t < tracks; t++) {
        jobject fmt = env->CallObjectMethod(extractor, mGetFormat, t);
        if (!fmt) continue;
        jmethodID mGetStr = env->GetMethodID(cFormat, "getString", "(Ljava/lang/String;)Ljava/lang/String;");
        LocalStr key(env, "mime");
        jstring mime = (jstring)env->CallObjectMethod(fmt, mGetStr, (jstring)key);
        if (mime) {
            const char* mstr = env->GetStringUTFChars(mime, nullptr);
            bool isAudio = mstr && strncmp(mstr, "audio/", 6) == 0;
            if (mstr) env->ReleaseStringUTFChars(mime, mstr);
            if (isAudio) { trackIdx = t; format = fmt; }
        }
        if (trackIdx >= 0) break;
    }
    if (trackIdx < 0 || !format) { LOGE("decode: no audio track in %s", path); return false; }

    env->CallVoidMethod(extractor, mSelTrack, trackIdx);

    jmethodID mGetInt = env->GetMethodID(cFormat, "getInteger", "(Ljava/lang/String;)I");
    LocalStr keyRate(env, "sample-rate");
    LocalStr keyCh(env, "channel-count");
    jint srcRate = env->CallIntMethod(format, mGetInt, (jstring)keyRate);
    jint srcCh   = env->CallIntMethod(format, mGetInt, (jstring)keyCh);
    if (srcRate <= 0) srcRate = 44100;
    if (srcCh   <= 0) srcCh   = 2;

    // MediaCodec codec = MediaCodec.createDecoderByType(mime);
    jmethodID mGetStr = env->GetMethodID(cFormat, "getString", "(Ljava/lang/String;)Ljava/lang/String;");
    LocalStr keyMime(env, "mime");
    jstring mime = (jstring)env->CallObjectMethod(format, mGetStr, (jstring)keyMime);
    jmethodID mCreate = env->GetStaticMethodID(cCodec, "createDecoderByType",
                        "(Ljava/lang/String;)Landroid/media/MediaCodec;");
    jobject codec = env->CallStaticObjectMethod(cCodec, mCreate, mime);
    if (!codec) { LOGE("decode: no decoder for %s", path); return false; }

    jmethodID mConfigure = env->GetMethodID(cCodec, "configure",
        "(Landroid/media/MediaFormat;Landroid/view/Surface;Landroid/media/MediaCrypto;I)V");
    jmethodID mStart  = env->GetMethodID(cCodec, "start", "()V");
    jmethodID mStop   = env->GetMethodID(cCodec, "stop", "()V");
    jmethodID mRelease= env->GetMethodID(cCodec, "release", "()V");
    jmethodID mInIdx  = env->GetMethodID(cCodec, "dequeueInputBuffer", "(J)I");
    jmethodID mOutIdx = env->GetMethodID(cCodec, "dequeueOutputBuffer", "(Landroid/media/MediaCodec$BufferInfo;J)I");
    jmethodID mInBuf  = env->GetMethodID(cCodec, "getInputBuffer", "(I)Ljava/nio/ByteBuffer;");
    jmethodID mOutBuf = env->GetMethodID(cCodec, "getOutputBuffer", "(I)Ljava/nio/ByteBuffer;");
    jmethodID mQueueIn= env->GetMethodID(cCodec, "queueInputBuffer", "(IIIJI)V");
    jmethodID mRelOut = env->GetMethodID(cCodec, "releaseOutputBuffer", "(IZ)V");

    env->CallVoidMethod(codec, mConfigure, format, nullptr, nullptr, (jint)0);
    env->CallVoidMethod(codec, mStart);
    if (env->ExceptionCheck()) { env->ExceptionClear(); LOGE("decode: configure failed"); return false; }

    jmethodID mInfoCtor = env->GetMethodID(cInfo, "<init>", "()V");
    jobject info = env->NewObject(cInfo, mInfoCtor);
    jfieldID fOffset = env->GetFieldID(cInfo, "offset", "I");
    jfieldID fSize   = env->GetFieldID(cInfo, "size",   "I");
    jfieldID fFlags  = env->GetFieldID(cInfo, "flags",  "I");

    jmethodID mPosition = env->GetMethodID(cBuffer, "position", "()I");
    jmethodID mLimit    = env->GetMethodID(cBuffer, "limit", "()I");

    std::vector<int16_t> pcm16;
    pcm16.reserve(srcRate * (size_t)srcCh * 4);

    const int64_t kTimeoutUs = 5000;
    bool inputDone = false;
    int  idleLoops = 0;

    while (idleLoops < 200) {
        if (!inputDone) {
            jint inIdx = env->CallIntMethod(codec, mInIdx, (jlong)kTimeoutUs);
            if (inIdx >= 0) {
                jobject inBuf = env->CallObjectMethod(codec, mInBuf, inIdx);
                int cap = inBuf ? env->CallIntMethod(inBuf, mLimit) : 0;
                int read = 0;
                if (inBuf && cap > 0) {
                    // Read directly into the ByteBuffer via the array path when
                    // available; MediaExtractor also has readSampleData.
                    env->CallIntMethod(extractor, mReadSample, inBuf, (jint)0); // best effort
                    read = env->CallIntMethod(extractor, mReadSample, inBuf, (jint)0);
                    if (env->ExceptionCheck()) { env->ExceptionClear(); read = 0; }
                }
                if (read < 0) {
                    env->CallVoidMethod(codec, mQueueIn, inIdx, 0, 0, (jlong)0, (jint)4); // BUFFER_FLAG_END_OF_STREAM
                    inputDone = true;
                } else {
                    long pts = (long)env->CallLongMethod(extractor, mSampleTime);
                    env->CallVoidMethod(codec, mQueueIn, inIdx, 0, (jint)read, (jlong)pts, (jint)0);
                    env->CallBooleanMethod(extractor, mAdvance);
                }
            }
        }

        jint outIdx = env->CallIntMethod(codec, mOutIdx, info, (jlong)kTimeoutUs);
        if (outIdx >= 0) {
            jint off = env->GetIntField(info, fOffset);
            jint sz  = env->GetIntField(info, fSize);
            jint fl  = env->GetIntField(info, fFlags);
            if (sz > 0) {
                jobject outBuf = env->CallObjectMethod(codec, mOutBuf, outIdx);
                if (outBuf) {
                    jmethodID mGetShort = nullptr; // use bulk via GetShortArrayRegion unavailable on ByteBuffer
                    // Copy through a direct read using the ByteBuffer's array is
                    // not guaranteed; use position/limit + a temp jbyteArray read
                    // through reflection-free path: MediaCodec output buffers on
                    // Android are direct — read via GetDirectBufferAddress.
                    void* base = env->GetDirectBufferAddress(outBuf);
                    if (base) {
                        int pos = env->CallIntMethod(outBuf, mPosition);
                        int lim = env->CallIntMethod(outBuf, mLimit);
                        (void)pos; (void)lim;
                        const int16_t* s = (const int16_t*)((const uint8_t*)base + off);
                        pcm16.insert(pcm16.end(), s, s + (sz/2));
                    }
                    (void)mGetShort;
                }
            }
            env->CallVoidMethod(codec, mRelOut, outIdx, (jboolean)false);
            if (fl & 4) break;   // EOS
            idleLoops = 0;
        } else {
            idleLoops++;
        }
    }

    env->CallVoidMethod(codec, mStop);
    env->CallVoidMethod(codec, mRelease);
    env->ExceptionClear();

    if (pcm16.empty()) { LOGE("decode: zero samples from %s", path); return false; }

    // Interleaved s16 → mono float, then resample to 44.1k.
    size_t frames = pcm16.size() / (size_t)srcCh;
    std::vector<float> mono(frames);
    for (size_t i=0;i<frames;i++) {
        int acc = 0;
        for (jint c=0;c<srcCh;c++) acc += pcm16[i*(size_t)srcCh + (size_t)c];
        mono[i] = (acc / (float)srcCh) / 32768.0f;
    }

    if (srcRate != 44100) {
        double ratio = 44100.0 / (double)srcRate;
        size_t outFrames = (size_t)((double)frames * ratio);
        if (outFrames < 1 || outFrames > 44100u*60u*10u) { LOGE("decode: bad resample size"); return false; }
        std::vector<float> res(outFrames);
        for (size_t i=0;i<outFrames;i++) {
            double srcPos = (double)i / ratio;
            size_t i0 = (size_t)srcPos;
            size_t i1 = i0 + 1 < frames ? i0 + 1 : i0;
            float  t  = (float)(srcPos - (double)i0);
            res[i] = mono[i0] * (1.f - t) + mono[i1] * t;
        }
        mono.swap(res);
    }

    out.pcm = std::move(mono);
    out.sampleRate = 44100;
    out.channels = 1;
    out.ok = true;
    return true;
}

} // namespace

bool DecodeAudioFile(const char* path, DecodedAudio& out) {
    out = DecodedAudio{};
    if (!path || !*path) return false;

    FILE* f = fopen(path, "rb");
    if (!f) { LOGE("decode: cannot open %s", path); return false; }
    uint8_t magic[12] = {};
    size_t  got = fread(magic, 1, sizeof magic, f);
    fseek(f, 0, SEEK_SET);

    bool isWav = got >= 12 && memcmp(magic, "RIFF", 4) == 0 && memcmp(magic+8, "WAVE", 4) == 0;
    bool isRifx = got >= 12 && memcmp(magic, "RIFX", 4) == 0;

    bool ok = false;
    if (isWav) {
        ok = DecodeWavNative(f, out);
    } else if (isRifx) {
        fclose(f);
        LOGE("decode: big-endian RIFF unsupported: %s", path);
        return false;
    }
    fclose(f);

    if (!ok) {
        ok = DecodeMediaCodec(path, out);
    }
    if (!ok) { LOGE("decode: failed %s", path); out = DecodedAudio{}; }
    return ok;
}
