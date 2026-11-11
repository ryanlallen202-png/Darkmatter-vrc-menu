#pragma once
// Host syntax-check stub — mirrors the subset of JNI the native module calls.
#include <cstdint>
#include <cstddef>
typedef int32_t jint; typedef int8_t jboolean; typedef int8_t jbyte;
typedef uint16_t jchar; typedef int16_t jshort; typedef int64_t jlong;
typedef float jfloat; typedef double jdouble; typedef int32_t jsize;
struct _jobject; typedef _jobject* jobject; typedef jobject jclass; typedef jobject jstring;
typedef jobject jarray; typedef jarray jbyteArray; typedef jarray jfloatArray; typedef jarray jshortArray;
typedef jobject jthrowable; typedef void* jmethodID; typedef void* jfieldID;
struct JavaVM;
struct JNIEnv {
    jclass FindClass(const char*);
    jmethodID GetMethodID(jclass, const char*, const char*);
    jmethodID GetStaticMethodID(jclass, const char*, const char*);
    jfieldID GetFieldID(jclass, const char*, const char*);
    jfieldID GetStaticFieldID(jclass, const char*, const char*);
    jobject NewObject(jclass, jmethodID, ...);
    jobject NewObjectV(jclass, jmethodID, void*);
    jclass GetObjectClass(jobject);
    jobject CallObjectMethod(jobject, jmethodID, ...);
    jint CallIntMethod(jobject, jmethodID, ...);
    jlong CallLongMethod(jobject, jmethodID, ...);
    void CallVoidMethod(jobject, jmethodID, ...);
    jobject CallStaticObjectMethod(jclass, jmethodID, ...);
    jint CallStaticIntMethod(jclass, jmethodID, ...);
    jint GetIntField(jobject, jfieldID);
    jlong GetLongField(jobject, jfieldID);
    void SetIntField(jobject, jfieldID, jint);
    jsize GetArrayLength(jarray);
    jbyteArray NewByteArray(jsize);
    jfloatArray NewFloatArray(jsize);
    void* GetByteArrayElements(jbyteArray, jboolean*);
    void* GetFloatArrayElements(jfloatArray, jboolean*);
    void ReleaseByteArrayElements(jbyteArray, jbyte*, jint);
    void ReleaseFloatArrayElements(jfloatArray, jfloat*, jint);
    void GetByteArrayRegion(jbyteArray, jsize, jsize, jbyte*);
    jstring NewStringUTF(const char*);
    const char* GetStringUTFChars(jstring, jboolean*);
    void ReleaseStringUTFChars(jstring, const char*);
    jint ThrowNew(jclass, const char*);
    jint Throw(jthrowable);
    jthrowable ExceptionOccurred();
    jboolean ExceptionCheck();
    jboolean CallBooleanMethod(jobject, jmethodID, ...);
    void* GetDirectBufferAddress(jobject);
    void ExceptionClear();
    void ExceptionDescribe();
    jint PushLocalFrame(jint);
    jobject PopLocalFrame(jobject);
    jobject NewGlobalRef(jobject);
    void DeleteGlobalRef(jobject);
    void DeleteLocalRef(jobject);
    jboolean IsSameObject(jobject, jobject);
    jint GetJavaVM(JavaVM**);
};
struct JavaVM {
    jint GetEnv(void**, jint);
    jint AttachCurrentThread(JNIEnv**, void*);
    jint DetachCurrentThread();
    jint GetJavaVM(JavaVM**);
};
#define JNI_OK 0
#define JNI_VERSION_1_6 0x00010006
#define JNI_ABORT 2
