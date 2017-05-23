#pragma once

#include "engine/lumix.h"
#include "engine/array.h"
#include "engine/delegate_list.h"
#include "engine/universe/component.h"
#include "engine/vec.h"


namespace Lumix
{

class Engine;
struct EntityGUID;
class PrefabSystem;
class Hierarchy;
class ArrayDescriptorBase;
class InputBlob;
struct IPlugin;
class PropertyDescriptorBase;
class OutputBlob;
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
	typedef struct IEditorCommand* (*EditorCommandCreator)(WorldEditor&);

	enum class Coordinate : int
	{
		X,
		Y,
		Z
	};

	struct RayHit
	{
		bool is_hit;
		float t;
		Entity entity;
		Vec3 pos;
	};

	struct LUMIX_EDITOR_API Plugin
	{
		virtual ~Plugin() {}

		virtual bool onEntityMouseDown(const RayHit& /*hit*/, int /*x*/, int /*y*/) { return false; }
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
	virtual void beginCommandGroup(u32 type) = 0;
	virtual void endCommandGroup() = 0;
	virtual IEditorCommand* executeCommand(IEditorCommand* command) = 0;
	virtual IEditorCommand* createEditorCommand(u32 command_type) = 0;
	virtual Engine& getEngine() = 0;
	virtual Universe* getUniverse() = 0;
	virtual IAllocator& getAllocator() = 0;
	virtual void renderIcons() = 0;
	virtual ComponentUID getEditCamera() = 0;
	virtual class Gizmo& getGizmo() = 0;
	virtual bool canUndo() const = 0;
	virtual bool canRedo() const = 0;
	virtual void undo() = 0;
	virtual void redo() = 0;
	virtual void loadUniverse(const char* basename) = 0;
	virtual void saveUniverse(const char* basename, bool save_path) = 0;
	virtual void newUniverse() = 0;
	virtual void showEntities(const Entity* entities, int count) = 0;
	virtual void showSelectedEntities() = 0;
	virtual void hideEntities(const Entity* entities, int count) = 0;
	virtual void hideSelectedEntities() = 0;
	virtual void copyEntities(const Entity* entities, int count, OutputBlob& blob) = 0;
	virtual void copyEntities() = 0;
	virtual bool canPasteEntities() const = 0;
	virtual void pasteEntities() = 0;
	virtual void addComponent(ComponentType type) = 0;
	virtual void cloneComponent(const ComponentUID& src, Entity entity) = 0;
	virtual void destroyComponent(const Entity* entities, int count, ComponentType cmp_type) = 0;
	virtual void createEntityGUID(Entity entity) = 0;
	virtual void destroyEntityGUID(Entity entity) = 0;
	virtual EntityGUID getEntityGUID(Entity entity) = 0;
	virtual Entity addEntity() = 0;
	virtual void destroyEntities(const Entity* entities, int count) = 0;
	virtual void selectEntities(const Entity* entities, int count) = 0;
	virtual Entity addEntityAt(int camera_x, int camera_y) = 0;
	virtual void setEntitiesPositions(const Entity* entities, const Vec3* positions, int count) = 0;
	virtual void setEntitiesCoordinate(const Entity* entities, int count, float value, Coordinate coord) = 0;
	virtual void setEntitiesLocalCoordinate(const Entity* entities, int count, float value, Coordinate coord) = 0;
	virtual void setEntitiesScale(const Entity* entities, int count, float scale) = 0;
	virtual void setEntitiesRotations(const Entity* entity, const Quat* rotations, int count) = 0;
	virtual void setEntitiesPositionsAndRotations(const Entity* entity,
		const Vec3* position,
		const Quat* rotation,
		int count) = 0;
	virtual void setEntityName(Entity entity, const char* name) = 0;
	virtual void snapDown() = 0;
	virtual void toggleGameMode() = 0;
	virtual void navigate(float forward, float right, float up, float speed) = 0;
	virtual void setProperty(ComponentType component,
		int index,
		const PropertyDescriptorBase& property,
		const Entity* entities,
		int count,
		const void* data,
		int size) = 0;
	virtual void setSnapMode(bool enable) = 0;
	virtual void setAdditiveSelection(bool additive) = 0;
	virtual void addArrayPropertyItem(const ComponentUID& cmp, ArrayDescriptorBase& property) = 0;
	virtual void removeArrayPropertyItem(const ComponentUID& cmp, int index, ArrayDescriptorBase& property) = 0;
	virtual bool isMouseDown(MouseButton::Value button) const = 0;
	virtual bool isMouseClick(MouseButton::Value button) const = 0;
	virtual void inputFrame() = 0;
	virtual void onMouseDown(int x, int y, MouseButton::Value button) = 0;
	virtual void onMouseMove(int x, int y, int relx, int rely) = 0;
	virtual void onMouseUp(int x, int y, MouseButton::Value button) = 0;
	virtual float getMouseX() const = 0;
	virtual float getMouseY() const = 0;
	virtual float getMouseRelX() const = 0;
	virtual float getMouseRelY() const = 0;
	virtual void lookAtSelected() = 0;
	virtual bool isOrbitCamera() const = 0;
	virtual void setOrbitCamera(bool enable) = 0;
	virtual const Array<Entity>& getSelectedEntities() const = 0;
	virtual bool isEntitySelected(Entity entity) const = 0;
	virtual void makeParent(Entity parent, Entity child) = 0;

	virtual DelegateList<void(const Array<Entity>&)>& entitySelected() = 0;
	virtual DelegateList<void()>& universeCreated() = 0;
	virtual DelegateList<void()>& universeDestroyed() = 0;

	virtual void addPlugin(Plugin& plugin) = 0;
	virtual void removePlugin(Plugin& plugin) = 0;
	virtual PrefabSystem& getPrefabSystem() = 0;
	virtual Vec3 getCameraRaycastHit() = 0;
	virtual bool isMeasureToolActive() const = 0;
	virtual float getMeasuredDistance() const = 0;
	virtual void toggleMeasure() = 0;
	virtual void setTopView() = 0;
	virtual void setFrontView() = 0;
	virtual void setSideView() = 0;
	virtual class MeasureTool* getMeasureTool() const = 0;
	virtual void makeRelative(char* relative, int max_size, const char* absolute) const = 0;

	virtual void saveUndoStack(const Path& path) = 0;
	virtual bool executeUndoStack(const Path& path) = 0;
	virtual bool runTest(const char* dir, const char* name) = 0;
	virtual void registerEditorCommandCreator(const char* command_type, EditorCommandCreator) = 0;
	virtual bool isGameMode() const = 0;
	virtual void setMouseSensitivity(float x, float y) = 0;
	virtual Vec2 getMouseSensitivity() = 0;
	virtual bool isUniverseChanged() const = 0;

protected:
	virtual ~WorldEditor() {}
};
}