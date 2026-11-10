#pragma once

// Initialize before playback. Calls are serialized internally.
void Soundboard_Init();
bool Soundboard_Play(int index);
int Soundboard_Count();
const char* Soundboard_Name(int index);
// Stop playback before freeing the PCM buffers and OpenSL ES objects.
void Soundboard_Shutdown();
