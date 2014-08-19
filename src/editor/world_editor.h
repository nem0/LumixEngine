#pragma once

#include "core/lumix.h"
#include "core/delegate_list.h"
#include "editor/property_descriptor.h"


namespace Lumix
{
	
	class Engine;
	class EntityTemplateSystem;
	class IPlugin;
	class IRenderDevice;
	class Path;
	class RayCastModelHit;
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

	class LUMIX_ENGINE_API WorldEditor
	{
		public:
			enum class MouseFlags : int
			{
				ALT = 1,
				CONTROL = 2
			};

			class Plugin
			{
				public:
					virtual ~Plugin() {}

					virtual void tick() = 0;
					virtual bool onEntityMouseDown(const RayCastModelHit& hit, int x, int y) = 0;
					virtual void onMouseMove(int x, int y, int rel_x, int rel_y, int mouse_flags) = 0;
					virtual void onMouseUp(int x, int y, MouseButton::Value button) = 0;
			};

		public:
			static WorldEditor* create(const char* base_path);
			static void destroy(WorldEditor* server);

			virtual void tick() = 0;
			virtual void registerCreator(uint32_t type, IPlugin& creator) = 0;
			virtual IPlugin* getCreator(uint32_t type) = 0;
			virtual void registerProperty(const char* component_type, IPropertyDescriptor* descriptor) = 0;
			virtual void executeCommand(class IEditorCommand* command) = 0;
			virtual Engine& getEngine() = 0;
			virtual void render(IRenderDevice& render_device) = 0;
			virtual void renderIcons(IRenderDevice& render_device) = 0;
			virtual Component getEditCamera() const = 0;
			virtual class Gizmo& getGizmo() = 0;
			virtual class FS::TCPFileServer& getTCPFileServer() = 0;
			virtual void setEditViewRenderDevice(IRenderDevice& render_device) = 0;
			virtual void undo() = 0;
			virtual void redo() = 0;
			virtual void loadUniverse(const Path& path) = 0;
			virtual void saveUniverse(const Path& path) = 0;
			virtual void newUniverse() = 0;
			virtual Path getUniversePath() const = 0;
			virtual void addComponent(uint32_t type_crc) = 0;
			virtual void cloneComponent(const Component& src, Entity& entity) = 0;
			virtual void removeComponent(const Component& crc) = 0;
			virtual Entity addEntity() = 0;
			virtual void selectEntity(Entity e) = 0;
			virtual Entity addEntityAt(int camera_x, int camera_y) = 0;
			virtual void setEntityPosition(const Entity& entity, const Vec3& position) = 0;
			virtual void setEntityPositionAndRotaion(const Entity& entity, const Vec3& position, const Quat& rotation) = 0;
			virtual void snapToTerrain() = 0;
			virtual void toggleGameMode() = 0;
			virtual void navigate(float forward, float right, float speed) = 0;
			virtual void setProperty(const char* component, const char* property, const void* data, int size) = 0;
			virtual void onMouseDown(int x, int y, MouseButton::Value button) = 0;
			virtual void onMouseMove(int x, int y, int relx, int rely, int mouse_flags) = 0;
			virtual void onMouseUp(int x, int y, MouseButton::Value button) = 0;
			virtual float getMouseX() const = 0;
			virtual float getMouseY() const = 0;
			virtual void setWireframe(bool is_wireframe) = 0;
			virtual void lookAtSelected() = 0;
			virtual const char* getBasePath() = 0;
			virtual Entity getSelectedEntity() const = 0;
			virtual const IPropertyDescriptor& getPropertyDescriptor(uint32_t type, uint32_t name_hash) = 0;
			virtual DelegateList<void(Entity&)>& entitySelected() = 0;
			virtual DelegateList<void()>& universeCreated() = 0;
			virtual DelegateList<void()>& universeDestroyed() = 0;
			virtual void addPlugin(Plugin* plugin) = 0;
			virtual void getRelativePath(char* relative_path, int max_length, const Path& source) = 0;
			virtual EntityTemplateSystem& getEntityTemplateSystem() = 0;

		protected:
			virtual ~WorldEditor() {}
	};


}