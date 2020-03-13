#pragma once

#include "engine/lumix.h"
#include "engine/math.h"


struct lua_State;


namespace Lumix
{

namespace OS { enum class MouseButton; }

template <typename T> struct DelegateList;
template <typename T> struct Array;


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
	struct RayHit {
		bool is_hit;
		float t;
		EntityPtr entity;
		DVec3 pos;
	};

	struct Vertex {
		Vec3 pos;
		u32 abgr;
	};

	virtual ~UniverseView() = default;
	virtual const struct Viewport& getViewport() const = 0;
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
	virtual Vec2 getMousePos() const = 0;
	virtual void setMouseSensitivity(float x, float y) = 0;
	virtual Vec2 getMouseSensitivity() = 0;
	virtual void setCustomPivot() = 0;
	virtual void resetPivot() = 0;
	virtual void setSnapMode(bool enable, bool vertex_snap) = 0;
	virtual RayHit getCameraRaycastHit(int cam_x, int cam_y) = 0;
	virtual Vertex* render(bool lines, u32 vertex_count) = 0;
	virtual void addText2D(float x, float y, Color color, const char* text) = 0;
};

LUMIX_EDITOR_API void addSphere(UniverseView& view, const DVec3& center, float radius, Color color);
LUMIX_EDITOR_API void addCube(UniverseView& view, const DVec3& center, const Vec3& x, const Vec3& y, const Vec3& z, Color color);
LUMIX_EDITOR_API void addCube(UniverseView& view, const DVec3& min, const DVec3& max, Color color);
LUMIX_EDITOR_API void addLine(UniverseView& view, const DVec3& a, const DVec3& b, Color color);
LUMIX_EDITOR_API void addCone(UniverseView& view, const DVec3& vertex, const Vec3& dir, const Vec3& axis0, const Vec3& axis1, Color color);
LUMIX_EDITOR_API void addFrustum(UniverseView& view, const struct ShiftedFrustum& frustum, Color color);
LUMIX_EDITOR_API void addCapsule(UniverseView& view, const DVec3& position, float height, float radius, Color color);

struct LUMIX_EDITOR_API WorldEditor
{
	enum class Coordinate : i32	{
		X,
		Y,
		Z,
		NONE
	};

	using CommandCreator = IEditorCommand* (lua_State*, WorldEditor&);

	static WorldEditor* create(struct Engine& engine, struct IAllocator& allocator);
	static void destroy(WorldEditor* editor, IAllocator& allocator);

	virtual void update() = 0;
	virtual Engine& getEngine() = 0;
	virtual struct Universe* getUniverse() = 0;
	virtual IAllocator& getAllocator() = 0;
	virtual UniverseView& getView() = 0;
	virtual void setView(UniverseView* view) = 0;
	
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
	virtual EntityRef addEntityAt(const DVec3& pos) = 0;
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
	
	virtual void setProperty(ComponentType component, int index, const char* property, Span<const EntityRef> entities, float value) = 0;
	virtual void setProperty(ComponentType component, int index, const char* property, Span<const EntityRef> entities, i32 value) = 0;
	virtual void setProperty(ComponentType component, int index, const char* property, Span<const EntityRef> entities, u32 value) = 0;
	virtual void setProperty(ComponentType component, int index, const char* property, Span<const EntityRef> entities, EntityPtr value) = 0;
	virtual void setProperty(ComponentType component, int index, const char* property, Span<const EntityRef> entities, const char* value) = 0;
	virtual void setProperty(ComponentType component, int index, const char* property, Span<const EntityRef> entities, const struct Path& value) = 0;
	virtual void setProperty(ComponentType component, int index, const char* property, Span<const EntityRef> entities, bool value) = 0;
	virtual void setProperty(ComponentType component, int index, const char* property, Span<const EntityRef> entities, const Vec2& value) = 0;
	virtual void setProperty(ComponentType component, int index, const char* property, Span<const EntityRef> entities, const Vec3& value) = 0;
	virtual void setProperty(ComponentType component, int index, const char* property, Span<const EntityRef> entities, const Vec4& value) = 0;
	virtual void setProperty(ComponentType component, int index, const char* property, Span<const EntityRef> entities, const IVec3& value) = 0;
	
	virtual void addArrayPropertyItem(const struct ComponentUID& cmp, const char* property) = 0;
	virtual void removeArrayPropertyItem(const ComponentUID& cmp, int index, const char* property) = 0;
	virtual const Array<EntityRef>& getSelectedEntities() const = 0;
	virtual bool isEntitySelected(EntityRef entity) const = 0;
	virtual void makeParent(EntityPtr parent, EntityRef child) = 0;

	virtual void copyEntities() = 0;
	virtual bool canPasteEntities() const = 0;
	virtual void pasteEntities() = 0;
    virtual void duplicateEntities() = 0;

	virtual void loadUniverse(const char* basename) = 0;
	virtual void saveUniverse(const char* basename, bool save_path) = 0;
	virtual void newUniverse() = 0;
	virtual void toggleGameMode() = 0;
	virtual void setToggleSelection(bool is_toggle) = 0;
	
	virtual DelegateList<void()>& universeCreated() = 0;
	virtual DelegateList<void()>& universeDestroyed() = 0;

	virtual struct PrefabSystem& getPrefabSystem() = 0;
	virtual void snapEntities(const DVec3& hit_pos, bool translate_mode) = 0;

	virtual bool isGameMode() const = 0;

protected:
	virtual ~WorldEditor() {}
};
}