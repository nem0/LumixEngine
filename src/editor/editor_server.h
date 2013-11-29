#pragma once

#include "core/lux.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


extern "C" LUX_ENGINE_API void* __stdcall luxServerInit(HWND hdc, const char* base_path, void* callback_ptr);
extern "C" LUX_ENGINE_API void __stdcall luxServerDraw(HWND hdc, void* ptr);
extern "C" LUX_ENGINE_API void __stdcall luxServerUpdate(void* ptr);
extern "C" LUX_ENGINE_API void __stdcall luxServerResize(HWND hdc, void* ptr);
extern "C" LUX_ENGINE_API void __stdcall luxServerMessage(void* ptr, void* msg, int size);
