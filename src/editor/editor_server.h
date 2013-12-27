#pragma once

#include "core/lux.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "editor/property_descriptor.h"


extern "C" LUX_ENGINE_API void* __stdcall luxServerInit(HWND hwnd, HWND game_hwnd, const char* base_path);
extern "C" LUX_ENGINE_API void __stdcall luxServerTick(HWND hwnd, HWND game_hwnd, void* ptr);
extern "C" LUX_ENGINE_API void __stdcall luxServerResize(HWND hwnd, void* ptr);


namespace Lux
{


	class IPlugin;
	class Engine;


	class LUX_ENGINE_API EditorServer
	{
		public:
			EditorServer() { m_impl = 0; }

			bool create(HWND hwnd, HWND game_hwnd, const char* base_path);
			void destroy();
			void onResize(int w, int h);
			void tick(HWND hwnd, HWND game_hwnd);
			void registerCreator(uint32_t type, IPlugin& creator);
			void registerProperty(const char* component_type, PropertyDescriptor& descriptor);
			Engine& getEngine();

		private:
			struct EditorServerImpl* m_impl;
	};


}