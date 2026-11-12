#pragma once
#include <vector>

namespace Gunlib
{
    struct PlayerEntry
    {
        void*   obj     = nullptr;
        int32_t actorNr = -1;
        char    name[64] = {};
    };

    bool Init();
    void CrashPlayer(int actorNr);
    void StartLag(float ratePerSec = 60.0f);
    void StopLag();
    void KickPlayer(int actorNr);
    void SetLagRate(float r);
    bool IsLagging();

    std::vector<PlayerEntry> GetPlayers();

    // Low-level Photon RaiseEvent helpers (used by Soundboard)
    bool PhotonRaiseEvent(uint8_t code, void* content, uint8_t receivers,
                          int32_t* targets, int target_count, bool reliable);
    bool PhotonRaiseEventBytes(uint8_t code, const uint8_t* data, int len,
                               uint8_t receivers, int32_t* targets,
                               int target_count, bool reliable);
}
