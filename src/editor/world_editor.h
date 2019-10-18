#pragma once

#include "engine/lumix.h"
#include "engine/universe/component.h"
#include "engine/math.h"


namespace Lumix
{


namespace Reflection
{
struct PropertyBase;
struct IArrayProperty;
}

namespace OS { 
	enum class MouseButton; 
	using WindowHandle = void*; 
}

template <typename T> class DelegateList;
template <typename T> class Array;
class Engine;
struct EntityGUID;
class Hierarchy;
struct IAllocator;
struct IEditorCommand;
class InputMemoryStream;
struct IPlugin;
struct ISerializer;
class OutputMemoryStream;
class Path;
class Pipeline;
class PrefabSystem;
class PropertyDescriptorBase;
struct Quat;
struct RayCastModelHit;
class RenderInterface;
class Universe;
struct Viewport;


class LUMIX_EDITOR_API WorldEditor
{
public:
	enum class Coordinate : int
	{
		X,
		Y,
		Z,
		NONE
	};

	struct RayHit
	{
		bool is_hit;
		float t;
		EntityPtr entity;
		DVec3 pos;
	};

	struct LUMIX_EDITOR_API Plugin
	{
		virtual ~Plugin() {}

		virtual bool onMouseDown(const RayHit& /*hit*/, int /*x*/, int /*y*/) { return false; }
		virtual void onMouseUp(int /*x*/, int /*y*/, OS::MouseButton /*button*/) {}
		virtual void onMouseMove(int /*x*/, int /*y*/, int /*rel_x*/, int /*rel_y*/) {}
		virtual bool showGizmo(ComponentUID /*cmp*/) { return false; }
	};

public:
	static WorldEditor* create(const char* base_path, Engine& engine, IAllocator& allocator);
	static void destroy(WorldEditor* editor, IAllocator& allocator);

	virtual void setRenderInterface(RenderInterface* interface) = 0;
	virtual RenderInterface* getRenderInterface() = 0;
	virtual OS::WindowHandle getWindow() = 0;
	virtual void update() = 0;
	virtual void updateEngine() = 0;
	virtual void beginCommandGroup(u32 type) = 0;
	virtual void endCommandGroup() = 0;
	virtual void executeCommand(IEditorCommand* command) = 0;
	virtual Engine& getEngine() = 0;
	virtual Universe* getUniverse() = 0;
	virtual IAllocator& getAllocator() = 0;
	virtual const Viewport& getViewport() const = 0;
	virtual void setViewport(const Viewport& viewport) = 0;
	virtual class EditorIcons& getIcons() = 0;
	virtual class Gizmo& getGizmo() = 0;
	virtual bool canUndo() const = 0;
	virtual bool canRedo() const = 0;
	virtual void undo() = 0;
	virtual void redo() = 0;
	virtual void loadUniverse(const char* basename) = 0;
	virtual void saveUniverse(const char* basename, bool save_path) = 0;
	virtual void newUniverse() = 0;
	virtual void copyEntities(const EntityRef* entities, int count, ISerializer& serializer) = 0;
	virtual void copyEntities() = 0;
	virtual bool canPasteEntities() const = 0;
	virtual void pasteEntities() = 0;
    virtual void duplicateEntities() = 0;
	virtual void addComponent(ComponentType type) = 0;
	virtual void cloneComponent(const ComponentUID& src, EntityRef entity) = 0;
	virtual void destroyComponent(const EntityRef* entities, int count, ComponentType cmp_type) = 0;
	virtual void createEntityGUID(EntityRef entity) = 0;
	virtual void destroyEntityGUID(EntityRef entity) = 0;
	virtual EntityGUID getEntityGUID(EntityRef entity) = 0;
	virtual EntityRef addEntity() = 0;
	virtual void destroyEntities(const EntityRef* entities, int count) = 0;
	virtual void selectEntities(const EntityRef* entities, int count, bool toggle) = 0;
	virtual EntityRef addEntityAt(int camera_x, int camera_y) = 0;
	virtual void setEntitiesPositions(const EntityRef* entities, const DVec3* positions, int count) = 0;
	virtual void setEntitiesCoordinate(const EntityRef* entities, int count, double value, Coordinate coord) = 0;
	virtual void setEntitiesLocalCoordinate(const EntityRef* entities, int count, double value, Coordinate coord) = 0;
	virtual void setEntitiesScale(const EntityRef* entities, int count, float scale) = 0;
	virtual void setEntitiesScales(const EntityRef* entities, const float* scales, int count) = 0;
	virtual void setEntitiesRotations(const EntityRef* entity, const Quat* rotations, int count) = 0;
	virtual void setEntitiesPositionsAndRotations(const EntityRef* entity,
		const DVec3* position,
		const Quat* rotation,
		int count) = 0;
	virtual void setEntityName(EntityRef entity, const char* name) = 0;
	virtual void snapDown() = 0;
	virtual void toggleGameMode() = 0;
	virtual void navigate(float forward, float right, float up, float speed) = 0;
	virtual void setProperty(ComponentType component,
		int index,
		const Reflection::PropertyBase& property,
		const EntityRef* entities,
		int count,
		const void* data,
		int size) = 0;
	virtual void setCustomPivot() = 0;
	virtual void setSnapMode(bool enable, bool vertex_snap) = 0;
	virtual void setToggleSelection(bool is_toggle) = 0;
	virtual void addArrayPropertyItem(const ComponentUID& cmp, const Reflection::IArrayProperty& property) = 0;
	virtual void removeArrayPropertyItem(const ComponentUID& cmp, int index, const Reflection::IArrayProperty& property) = 0;
	virtual bool isMouseDown(OS::MouseButton button) const = 0;
	virtual bool isMouseClick(OS::MouseButton button) const = 0;
	virtual void inputFrame() = 0;
	virtual void onMouseDown(int x, int y, OS::MouseButton button) = 0;
	virtual void onMouseMove(int x, int y, int relx, int rely) = 0;
	virtual void onMouseUp(int x, int y, OS::MouseButton button) = 0;
	virtual Vec2 getMousePos() const = 0;
	virtual void copyViewTransform() = 0;
	virtual void lookAtSelected() = 0;
	virtual bool isOrbitCamera() const = 0;
	virtual void setOrbitCamera(bool enable) = 0;
	virtual const Array<EntityRef>& getSelectedEntities() const = 0;
	virtual bool isEntitySelected(EntityRef entity) const = 0;
	virtual void makeParent(EntityPtr parent, EntityRef child) = 0;

	virtual DelegateList<void(const Array<EntityRef>&)>& entitySelected() = 0;
	virtual DelegateList<void()>& universeCreated() = 0;
	virtual DelegateList<void()>& universeDestroyed() = 0;

	virtual void addPlugin(Plugin& plugin) = 0;
	virtual void removePlugin(Plugin& plugin) = 0;
	virtual PrefabSystem& getPrefabSystem() = 0;
	virtual DVec3 getCameraRaycastHit() = 0;
	virtual bool isMeasureToolActive() const = 0;
	virtual double getMeasuredDistance() const = 0;
	virtual void toggleMeasure() = 0;
	virtual void setTopView() = 0;
	virtual void setFrontView() = 0;
	virtual void setSideView() = 0;
	virtual class MeasureTool* getMeasureTool() const = 0;
	virtual void makeRelative(Span<char> relative, const char* absolute) const = 0;
	virtual void makeAbsolute(Span<char> absolute, const char* relative) const = 0;

	virtual bool isGameMode() const = 0;
	virtual void setMouseSensitivity(float x, float y) = 0;
	virtual Vec2 getMouseSensitivity() = 0;
	virtual bool isUniverseChanged() const = 0;

protected:
	virtual ~WorldEditor() {}
};
}