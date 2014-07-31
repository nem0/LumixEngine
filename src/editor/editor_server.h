#pragma once

#include "core/lumix.h"
#include "core/delegate_list.h"
#include "editor/property_descriptor.h"


namespace Lumix
{
	
	class Engine;
	class IPlugin;
	class IRenderDevice;
	class Path;
	namespace FS
	{
		class TCPFileServer;
	}

	struct MouseButton
	{
		enum Value
		{
			LEFT,
			MIDDLE,
			RIGHT
		};
	};

	class LUMIX_ENGINE_API EditorServer
	{
		public:
			enum class MouseFlags : int
			{
				ALT = 1,
				CONTROL = 2
			};

		public:
			EditorServer() { m_impl = 0; }

			bool create(const char* base_path);
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
			void loadUniverse(const Path& path);
			void saveUniverse(const Path& path);
			void newUniverse();
			Path getUniversePath() const;
			void addComponent(uint32_t type_crc);
			void addEntity();
			void toggleGameMode();
			void navigate(float forward, float right, float speed);
			void setProperty(const char* component, const char* property, const void* data, int size);
			void onMouseDown(int x, int y, MouseButton::Value button);
			void onMouseMove(int x, int y, int relx, int rely, int mouse_flags);
			void onMouseUp(int x, int y, MouseButton::Value button);
			void setWireframe(bool is_wireframe);
			void lookAtSelected();
			const char* getBasePath();
			Entity getSelectedEntity() const;
			const IPropertyDescriptor& getPropertyDescriptor(uint32_t type, uint32_t name_hash);
			DelegateList<void(Entity&)>& entitySelected();
			DelegateList<void()>& universeCreated();
			DelegateList<void ()>& universeDestroyed();

		private:
			struct EditorServerImpl* m_impl;
	};


}