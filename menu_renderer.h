#pragma once
#include <jni.h>

namespace MenuRenderer
{
    jobject BuildSurfaceView(JNIEnv* env, jobject ctx);
    void    OnSurfaceCreated(JNIEnv* env, jobject surface);
    void    OnSurfaceDestroyed();
    void    OnTouch(float x, float y, bool down);
    void    SetVisible(bool v);
    bool    IsVisible();
}
