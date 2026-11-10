#include "tone.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>

static void Require(bool condition) {
    if (!condition) { std::cerr << "tone test failed\n"; std::exit(1); }
}
int main() {
    for (int type = 0; type <= 4; ++type) {
        const auto pcm = MakeTone(440, 1, type);
        Require(pcm.size() == 44100);
        Require(pcm.back() == 0);
        Require(pcm == MakeTone(440, 1, type));
        for (auto value : pcm) Require(value >= -19661 && value <= 19661);
    }
    Require(MakeTone(440, -1, 0).empty());
    Require(MakeTone(440, 11, 0).empty());
    Require(MakeTone(0, 1, 0).empty());
    Require(MakeTone(22050, 1, 0).empty());
    Require(MakeTone(440, 1, 5).empty());
    Require(MakeTone(440, 1, 0, 2).empty());
    Require(MakeTone(440, std::numeric_limits<float>::infinity(), 0).empty());
    Require(MakeTone(std::numeric_limits<float>::quiet_NaN(), 1, 0).empty());
    Require(MakeTone(440, 0.000001f, 0).empty());
    for (auto value : MakeTone(440, 1, 0, 0)) Require(value == 0);
    std::cout << "tone tests passed\n";
}
