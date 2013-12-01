#pragma once

#include "core/lux.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


extern "C" LUX_ENGINE_API void* __stdcall luxServerInit(HWND hwnd, HWND game_hwnd, const char* base_path);
extern "C" LUX_ENGINE_API void __stdcall luxServerTick(void* ptr);
extern "C" LUX_ENGINE_API void __stdcall luxServerResize(void* ptr);

