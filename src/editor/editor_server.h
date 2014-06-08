#pragma once

#include "core/lux.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "editor/property_descriptor.h"


namespace Lux
{


	class EditorClient;
	class Engine;
	class IPlugin;
	class IRenderDevice;
	namespace FS
	{
		class TCPFileServer;
	}

	class LUX_ENGINE_API EditorServer
	{
		public:
			EditorServer() { m_impl = 0; }

			bool create(const char* base_path, EditorClient& client);
			void destroy();
			void onMessage(const uint8_t* data, int32_t size);
			void tick();
			void registerCreator(uint32_t type, IPlugin& creator);
			void registerProperty(const char* component_type, IPropertyDescriptor* descriptor);
			Engine& getEngine();
			void render(IRenderDevice& render_device);
			void renderIcons(IRenderDevice& render_device);
			Component getEditCamera() const;
			class Gizmo& getGizmo();
			class FS::TCPFileServer& getTCPFileServer();
			void setEditViewRenderDevice(IRenderDevice& render_device);

		private:
			struct EditorServerImpl* m_impl;
	};


}