#pragma once
#include "Core/Defines.h"

#define GFX_CHECK_HR(hr) if(FAILED(hr)) ADRIA_DEBUGBREAK();

#define GFX_BACKBUFFER_COUNT 3
#define GFX_MULTITHREADED 0

#define GFX_PROFILING 1

#if GFX_PROFILING
#define GFX_PROFILING_USE_TRACY 0
#endif



