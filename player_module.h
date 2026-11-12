#pragma once
// player_module.h — nameplate + invisibility interface
// Implementation in player_module.cpp; wired to features.cpp f_invisible
// and f_nameSpoof flags.

namespace Player
{
    bool NameplateHidden();
    void SetNameplate(bool hide);   // hide=true → hide, false → show

    bool IsInvisible();
    void SetInvisible(bool invis);

    // Wire up AudioSource on local player GO (called by Soundboard::Tick)
    void* GetLocalAudioSource();
}
