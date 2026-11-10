#include "soundboard.hpp"
#include <jni.h>
#include <exception>

extern "C" JNIEXPORT void JNICALL
Java_com_darkmatter_companion_NativeAudio_init(JNIEnv* env, jclass) {
    try {
        Soundboard_Init();
    } catch (const std::exception& error) {
        jclass cls = env->FindClass("java/lang/IllegalStateException");
        if (cls) env->ThrowNew(cls, error.what());
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_darkmatter_companion_NativeAudio_play(JNIEnv*, jclass, jint index) {
    return Soundboard_Play(index) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_darkmatter_companion_NativeAudio_name(JNIEnv* env, jclass, jint index) {
    return env->NewStringUTF(Soundboard_Name(index));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_darkmatter_companion_NativeAudio_count(JNIEnv*, jclass) {
    return Soundboard_Count();
}

extern "C" JNIEXPORT void JNICALL
Java_com_darkmatter_companion_NativeAudio_shutdown(JNIEnv*, jclass) {
    Soundboard_Shutdown();
}
