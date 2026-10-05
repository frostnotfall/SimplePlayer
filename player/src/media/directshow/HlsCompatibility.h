#pragma once
#include <Windows.h>
namespace bp {
// Set supported FFmpeg AVOptions at the LAV import boundary, before its opaque
// demuxer context is opened. Only the current process's import slot is changed.
bool installHlsCompatibility(HMODULE splitter);
unsigned long long guardedHttpOpens();
const char* hlsCompatibilityStatus();
}
