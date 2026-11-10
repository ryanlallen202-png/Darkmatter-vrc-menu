#include "shared.hpp"

extern std::vector<struct PlayerInfo> g_players;
extern bool f_esp;

struct PlayerInfo { void *api; std::string name; Vec3 pos; float dist; bool isLocal; int id; };

static double g_espNext = 0;

void ESP_Init() { LOGI("ESP init"); }

void ESP_Update() {
    if (!f_esp || g_time < g_espNext) return;
    g_espNext = g_time + 0.25;
    LOGI("=== ESP === %zu players", g_players.size());
    for (auto &p : g_players) {
        if (p.isLocal) continue;
        LOGI("[ESP] %-24s  %.1fm  id=%d", p.name.c_str(), p.dist, p.id);
    }
}
