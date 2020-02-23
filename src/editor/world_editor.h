#pragma once

#include "engine/lumix.h"
#include "engine/math.h"


struct lua_State;


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

template <typename T> struct DelegateList;
template <typename T> struct Array;
struct ComponentUID;
struct Engine;
struct IAllocator;
struct IPlugin;
struct Path;
struct PrefabSystem;
struct Quat;
struct RenderInterface;
struct Universe;
struct Viewport;


struct IEditorCommand
{
	virtual ~IEditorCommand() {}

	virtual bool isReady() { return true; }
	virtual bool execute() = 0;
	virtual void undo() = 0;
	virtual const char* getType() = 0;
	virtual bool merge(IEditorCommand& command) = 0;
};

struct UniverseView {
	virtual ~UniverseView() = default;
	virtual const Viewport& getViewport() = 0;
	virtual void setViewport(const Viewport& vp) = 0;
	virtual void lookAtSelected() = 0;
	virtual void setTopView() = 0;
	virtual void setFrontView() = 0;
	virtual void setSideView() = 0;
	virtual bool isOrbitCamera() const = 0;
	virtual void setOrbitCamera(bool enable) = 0;
	virtual void moveCamera(float forward, float right, float up, float speed) = 0;
	virtual void copyTransform() = 0;

	virtual bool isMouseDown(OS::MouseButton button) const = 0;
	virtual bool isMouseClick(OS::MouseButton button) const = 0;
	virtual void inputFrame() = 0;
	virtual void onMouseDown(int x, int y, OS::MouseButton button) = 0;
	virtual void onMouseMove(int x, int y, int relx, int rely) = 0;
	virtual void onMouseUp(int x, int y, OS::MouseButton button) = 0;
	virtual Vec2 getMousePos() const = 0;
	virtual void setMouseSensitivity(float x, float y) = 0;
	virtual Vec2 getMouseSensitivity() = 0;
	virtual void setCustomPivot() = 0;
	virtual void setSnapMode(bool enable, bool vertex_snap) = 0;
};

struct LUMIX_EDITOR_API WorldEditor
{
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
		virtual bool showGizmo(ComponentUID /*cmp*/);
	};

	using CommandCreator = IEditorCommand* (lua_State*, WorldEditor&);

	static WorldEditor* create(const char* base_path, Engine& engine, IAllocator& allocator);
	static void destroy(WorldEditor* editor, IAllocator& allocator);

	virtual void setRenderInterface(RenderInterface* interface) = 0;
	virtual RenderInterface* getRenderInterface() = 0;
	virtual void update() = 0;
	virtual Engine& getEngine() = 0;
	virtual Universe* getUniverse() = 0;
	virtual IAllocator& getAllocator() = 0;
	virtual UniverseView& getView() = 0;
	virtual struct EditorIcons& getIcons() = 0;
	virtual struct Gizmo& getGizmo() = 0;
	virtual Span<Plugin*> getPlugins() = 0;
	
	// commands
	virtual void beginCommandGroup(u32 type) = 0;
	virtual void endCommandGroup() = 0;
	virtual void executeCommand(IEditorCommand* command) = 0;
	virtual void executeCommand(const char* name, const char* args) = 0;
	virtual void registerCommand(const char* name, CommandCreator* creator) = 0;
	virtual bool isUniverseChanged() const = 0;
	virtual bool canUndo() const = 0;
	virtual bool canRedo() const = 0;
	virtual void undo() = 0;
	virtual void redo() = 0;
	virtual void addComponent(Span<const EntityRef> entities, ComponentType type) = 0;
	virtual void destroyComponent(Span<const EntityRef> entities, ComponentType cmp_type) = 0;
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
	virtual void setProperty(ComponentType component,
		const char* prop_name,
		Span<const EntityRef> entities,
		Span<const u8> data) = 0;
	virtual void addArrayPropertyItem(const ComponentUID& cmp, const Reflection::IArrayProperty& property) = 0;
	virtual void removeArrayPropertyItem(const ComponentUID& cmp, int index, const Reflection::IArrayProperty& property) = 0;
	virtual const Array<EntityRef>& getSelectedEntities() const = 0;
	virtual bool isEntitySelected(EntityRef entity) const = 0;
	virtual void makeParent(EntityPtr parent, EntityRef child) = 0;

	//
	virtual void copyEntities() = 0;
	virtual bool canPasteEntities() const = 0;
	virtual void pasteEntities() = 0;
    virtual void duplicateEntities() = 0;

	virtual void loadUniverse(const char* basename) = 0;
	virtual void saveUniverse(const char* basename, bool save_path) = 0;
	virtual void newUniverse() = 0;
	virtual void snapDown() = 0;
	virtual void toggleGameMode() = 0;
	virtual void setToggleSelection(bool is_toggle) = 0;
	
	virtual DelegateList<void()>& universeCreated() = 0;
	virtual DelegateList<void()>& universeDestroyed() = 0;

	virtual void addPlugin(Plugin& plugin) = 0;
	virtual void removePlugin(Plugin& plugin) = 0;
	virtual PrefabSystem& getPrefabSystem() = 0;
	virtual DVec3 getCameraRaycastHit() = 0;
	virtual bool isMeasureToolActive() const = 0;
	virtual double getMeasuredDistance() const = 0;
	virtual void toggleMeasure() = 0;
	virtual struct MeasureTool* getMeasureTool() const = 0;
	virtual void snapEntities(const DVec3& hit_pos) = 0;

	virtual bool isGameMode() const = 0;

protected:
	virtual ~WorldEditor() {}
};
}