#pragma once

#include "core/lux.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "editor/property_descriptor.h"


namespace Lux
{


	class IPlugin;
	class IRenderDevice;
	class Engine;
	namespace FS
	{
		class TCPFileServer;
	}

	class LUX_ENGINE_API EditorServer
	{
		public:
			EditorServer() { m_impl = 0; }

			bool create(HWND hwnd, HWND game_hwnd, const char* base_path);
			void destroy();
			void tick();
			void registerCreator(uint32_t type, IPlugin& creator);
			void registerProperty(const char* component_type, IPropertyDescriptor* descriptor);
			Engine& getEngine();
			void render(IRenderDevice& render_device);
			void renderIcons(IRenderDevice& render_device);
			Component getEditCamera() const;
			HGLRC getHGLRC();
			class Gizmo& getGizmo();
			class FS::TCPFileServer& getTCPFileServer();
			void setEditViewRenderDevice(IRenderDevice& render_device);

		private:
			struct EditorServerImpl* m_impl;
	};


}