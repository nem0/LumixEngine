#pragma once

#include "lumix.h"
#include "core/array.h"
#include "core/delegate_list.h"
#include "core/vec.h"
#include "universe/component.h"


namespace Lumix
{

class Engine;
class EntityTemplateSystem;
class Hierarchy;
class IArrayDescriptor;
class IPlugin;
class IPropertyDescriptor;
class Path;
class Pipeline;
class RenderInterface;
struct Quat;
struct RayCastModelHit;
class Universe;


struct MouseButton
{
	enum Value
	{
		LEFT,
		RIGHT,
		MIDDLE
	};
};


class LUMIX_EDITOR_API WorldEditor
{
public:
	typedef Array<ComponentUID> ComponentList;
	typedef class IEditorCommand* (*EditorCommandCreator)(WorldEditor&);

	enum class MouseFlags : int
	{
		ALT = 1,
		CONTROL = 2
	};

	class LUMIX_EDITOR_API Plugin
	{
	public:
		virtual ~Plugin() {}

		virtual void tick() {}
		virtual bool onEntityMouseDown(const RayCastModelHit& /*hit*/, int /*x*/, int /*y*/)
		{
			return false;
		}
		virtual void onMouseUp(int /*x*/, int /*y*/, MouseButton::Value /*button*/) {}
		virtual void onMouseMove(int /*x*/, int /*y*/, int /*rel_x*/, int /*rel_y*/) {}

		virtual bool showGizmo(ComponentUID /*cmp*/) { return false; }
	};

public:
	static WorldEditor* create(const char* base_path, Engine& engine, IAllocator& allocator);
	static void destroy(WorldEditor* editor, IAllocator& allocator);

	virtual void setRenderInterface(RenderInterface* interface) = 0;
	virtual RenderInterface* getRenderInterface() = 0;
	virtual void update() = 0;
	virtual void updateEngine() = 0;
	virtual void beginCommandGroup(uint32 type) = 0;
	virtual void endCommandGroup() = 0;
	virtual void executeCommand(IEditorCommand* command) = 0;
	virtual IEditorCommand* createEditorCommand(uint32 command_type) = 0;
	virtual Engine& getEngine() = 0;
	virtual Universe* getUniverse() = 0;
	virtual const Array<IScene*>& getScenes() const = 0;
	virtual IScene* getScene(uint32 hash) = 0;
	virtual IScene* getSceneByComponentType(uint32 hash) = 0;
	virtual IAllocator& getAllocator() = 0;
	virtual void renderIcons() = 0;
	virtual ComponentUID getEditCamera() = 0;
	virtual class Gizmo& getGizmo() = 0;
	virtual bool canUndo() const = 0;
	virtual bool canRedo() const = 0;
	virtual void undo() = 0;
	virtual void redo() = 0;
	virtual void loadUniverse(const Path& path) = 0;
	virtual void saveUniverse(const Path& path, bool save_path) = 0;
	virtual void newUniverse() = 0;
	virtual Path getUniversePath() const = 0;
	virtual void showEntities(const Entity* entities, int count) = 0;
	virtual void showSelectedEntities() = 0;
	virtual void hideEntities(const Entity* entities, int count) = 0;
	virtual void hideSelectedEntities() = 0;
	virtual void copyEntities() = 0;
	virtual bool canPasteEntities() const = 0;
	virtual void pasteEntities() = 0;
	virtual ComponentUID getComponent(Entity entity, uint32 type) = 0;
	virtual ComponentList& getComponents(Entity entity) = 0;
	virtual void addComponent(uint32 type_crc) = 0;
	virtual void cloneComponent(const ComponentUID& src, Entity entity) = 0;
	virtual void destroyComponent(const ComponentUID& cmp) = 0;
	virtual bool canRemove(const ComponentUID& cmp) = 0;
	virtual Entity addEntity() = 0;
	virtual void destroyEntities(const Entity* entities, int count) = 0;
	virtual void selectEntities(const Entity* entities, int count) = 0;
	virtual void selectEntitiesWithSameMesh() = 0;
	virtual Entity addEntityAt(int camera_x, int camera_y) = 0;
	virtual void setEntitiesPositions(const Entity* entities, const Vec3* positions, int count) = 0;
	virtual void setEntitiesScales(const Entity* entities, const float* scales, int count) = 0;
	virtual void setEntitiesRotations(const Entity* entity, const Quat* rotations, int count) = 0;
	virtual void setEntitiesPositionsAndRotations(const Entity* entity,
		const Vec3* position,
		const Quat* rotation,
		int count) = 0;
	virtual void setEntityName(Entity entity, const char* name) = 0;
	virtual void snapDown() = 0;
	virtual void toggleGameMode() = 0;
	virtual void navigate(float forward, float right, float speed) = 0;
	virtual void setProperty(uint32 component,
		int index,
		IPropertyDescriptor& property,
		const void* data,
		int size) = 0;
	virtual void setSnapMode(bool enable) = 0;
	virtual void setAdditiveSelection(bool additive) = 0;
	virtual void addArrayPropertyItem(const ComponentUID& cmp, IArrayDescriptor& property) = 0;
	virtual void removeArrayPropertyItem(const ComponentUID& cmp,
		int index,
		IArrayDescriptor& property) = 0;
	virtual bool isMouseDown(MouseButton::Value button) const = 0;
	virtual bool isMouseClick(MouseButton::Value button) const = 0;
	virtual void onMouseDown(int x, int y, MouseButton::Value button) = 0;
	virtual void onMouseMove(int x, int y, int relx, int rely) = 0;
	virtual void onMouseUp(int x, int y, MouseButton::Value button) = 0;
	virtual float getMouseX() const = 0;
	virtual float getMouseY() const = 0;
	virtual void lookAtSelected() = 0;
	virtual bool isOrbitCamera() const = 0;
	virtual void setOrbitCamera(bool enable) = 0;
	virtual const Array<Entity>& getSelectedEntities() const = 0;
	virtual bool isEntitySelected(Entity entity) const = 0;

	virtual DelegateList<void(const Array<Entity>&)>& entitySelected() = 0;
	virtual DelegateList<void()>& universeCreated() = 0;
	virtual DelegateList<void()>& universeDestroyed() = 0;
	virtual DelegateList<void()>& universeLoaded() = 0;
	virtual DelegateList<void(Entity, const char*)>& entityNameSet() = 0;
	virtual DelegateList<void(ComponentUID, const IPropertyDescriptor&)>& propertySet() = 0;

	virtual void addPlugin(Plugin& plugin) = 0;
	virtual void removePlugin(Plugin& plugin) = 0;
	virtual EntityTemplateSystem& getEntityTemplateSystem() = 0;
	virtual Vec3 getCameraRaycastHit() = 0;
	virtual bool isMeasureToolActive() const = 0;
	virtual float getMeasuredDistance() const = 0;
	virtual void toggleMeasure() = 0;
	virtual void setTopView() = 0;
	virtual void setFrontView() = 0;
	virtual void setSideView() = 0;
	virtual class MeasureTool* getMeasureTool() const = 0;

	virtual void saveUndoStack(const Path& path) = 0;
	virtual bool executeUndoStack(const Path& path) = 0;
	virtual bool runTest(const Path& undo_stack_path,
						 const Path& result_universe_path) = 0;
	virtual void registerEditorCommandCreator(const char* command_type,
											  EditorCommandCreator) = 0;
	virtual bool isGameMode() const = 0;
	virtual class EntityGroups& getEntityGroups() = 0;
	virtual void setMouseSensitivity(float x, float y) = 0;
	virtual Vec2 getMouseSensitivity() = 0;

protected:
	virtual ~WorldEditor() {}
};
}