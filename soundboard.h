#pragma once

namespace Soundboard
{
    void        Init();
    void        Tick();   // call from Java tick handler or render loop

    bool        LoadClip(const char* path);
    void        PlayServer(int idx, bool loop, float vol);
    void        StopAll();

    int         ClipCount();
    const char* ClipName(int idx);

    void        SetAudioSource(void* src);
}
