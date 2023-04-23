#pragma once

#include "engine/lumix.h"
#include "engine/hash_map.h"
#include "engine/math.h"
#include "engine/world.h"


struct lua_State;


namespace Lumix
{

namespace os { enum class MouseButton; }

template <typename T> struct Array;
template <typename T> struct DelegateList;
template <typename T> struct UniquePtr;


struct IEditorCommand
{
	virtual ~IEditorCommand() {}

	virtual bool execute() = 0;
	virtual void undo() = 0;
	virtual const char* getType() = 0;
	virtual bool merge(IEditorCommand& command) = 0;
};

struct WorldView {
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

	virtual ~WorldView() = default;
	virtual const struct Viewport& getViewport() const = 0;
	virtual void setViewport(const Viewport& vp) = 0;
	virtual void lookAtSelected() = 0;
	virtual void setTopView() = 0;
	virtual void setFrontView() = 0;
	virtual void setSideView() = 0;
	virtual void moveCamera(float forward, float right, float up, float speed) = 0;
	virtual void copyTransform() = 0;
	virtual void refreshIcons() = 0;

	virtual bool isMouseDown(os::MouseButton button) const = 0;
	virtual bool isMouseClick(os::MouseButton button) const = 0;
	virtual Vec2 getMousePos() const = 0;
	virtual void setMouseSensitivity(float x, float y) = 0;
	virtual Vec2 getMouseSensitivity() = 0;
	virtual void setCustomPivot() = 0;
	virtual void resetPivot() = 0;
	virtual void setSnapMode(bool enable, bool vertex_snap) = 0;
	virtual RayHit getCameraRaycastHit(int cam_x, int cam_y, EntityPtr ignore) = 0;
	virtual Vertex* render(bool lines, u32 vertex_count) = 0;
	virtual void addText2D(float x, float y, Color color, const char* text) = 0;
	virtual struct WorldEditor& getEditor() = 0;
};

LUMIX_EDITOR_API void addCircle(WorldView& view, const DVec3& center, float radius, const Vec3& up, Color color);
LUMIX_EDITOR_API void addSphere(WorldView& view, const DVec3& center, float radius, Color color);
LUMIX_EDITOR_API void addCube(WorldView& view, const DVec3& center, const Vec3& x, const Vec3& y, const Vec3& z, Color color);
LUMIX_EDITOR_API void addCube(WorldView& view, const DVec3& min, const DVec3& max, Color color);
LUMIX_EDITOR_API void addLine(WorldView& view, const DVec3& a, const DVec3& b, Color color);
LUMIX_EDITOR_API void addCylinder(WorldView& view, const DVec3& pos, const Vec3& up, float radius, float height, Color color);
LUMIX_EDITOR_API void addCone(WorldView& view, const DVec3& vertex, const Vec3& dir, const Vec3& axis0, const Vec3& axis1, Color color);
LUMIX_EDITOR_API void addFrustum(WorldView& view, const struct ShiftedFrustum& frustum, Color color);
LUMIX_EDITOR_API void addCapsule(WorldView& view, const DVec3& position, float height, float radius, Color color);

struct LUMIX_EDITOR_API WorldEditor
{
	enum class Coordinate : i32	{
		X,
		Y,
		Z,
		NONE
	};

	static UniquePtr<WorldEditor> create(struct Engine& engine, struct IAllocator& allocator);

	virtual void loadProject() = 0;
	virtual void update() = 0;
	virtual Engine& getEngine() = 0;
	virtual struct World* getWorld() = 0;
	virtual IAllocator& getAllocator() = 0;
	virtual WorldView& getView() = 0;
	virtual void setView(WorldView* view) = 0;
	
	virtual void beginCommandGroup(const char* type) = 0;
	virtual void endCommandGroup() = 0;
	virtual void lockGroupCommand() = 0;
	virtual void executeCommand(UniquePtr<IEditorCommand>&& command) = 0;
	virtual bool isWorldChanged() const = 0;
	virtual bool canUndo() const = 0;
	virtual bool canRedo() const = 0;
	virtual void undo() = 0;
	virtual void redo() = 0;
	virtual void addComponent(Span<const EntityRef> entities, ComponentType type) = 0;
	virtual void destroyComponent(Span<const EntityRef> entities, ComponentType cmp_type) = 0;
	virtual EntityRef addEntity() = 0;
	virtual void destroyEntities(const EntityRef* entities, int count) = 0;
	virtual void selectEntities(Span<const EntityRef> entities, bool toggle) = 0;
	virtual EntityRef addEntityAt(const DVec3& pos) = 0;
	virtual void setEntitiesPositions(const EntityRef* entities, const DVec3* positions, int count) = 0;
	virtual void setEntitiesCoordinate(const EntityRef* entities, int count, double value, Coordinate coord) = 0;
	virtual void setEntitiesLocalCoordinate(const EntityRef* entities, int count, double value, Coordinate coord) = 0;
	virtual void setEntitiesScale(const EntityRef* entities, int count, const Vec3& scale) = 0;
	virtual void setEntitiesScales(const EntityRef* entities, const Vec3* scales, int count) = 0;
	virtual void setEntitiesRotations(const EntityRef* entity, const Quat* rotations, int count) = 0;
	virtual void setEntitiesPositionsAndRotations(const EntityRef* entity,
		const DVec3* position,
		const Quat* rotation,
		int count) = 0;
	virtual void setEntityName(EntityRef entity, const char* name) = 0;
	
	virtual void setProperty(ComponentType component, const char* array, int index, const char* property, Span<const EntityRef> entities, float value) = 0;
	virtual void setProperty(ComponentType component, const char* array, int index, const char* property, Span<const EntityRef> entities, i32 value) = 0;
	virtual void setProperty(ComponentType component, const char* array, int index, const char* property, Span<const EntityRef> entities, u32 value) = 0;
	virtual void setProperty(ComponentType component, const char* array, int index, const char* property, Span<const EntityRef> entities, EntityPtr value) = 0;
	virtual void setProperty(ComponentType component, const char* array, int index, const char* property, Span<const EntityRef> entities, const char* value) = 0;
	virtual void setProperty(ComponentType component, const char* array, int index, const char* property, Span<const EntityRef> entities, const struct Path& value) = 0;
	virtual void setProperty(ComponentType component, const char* array, int index, const char* property, Span<const EntityRef> entities, bool value) = 0;
	virtual void setProperty(ComponentType component, const char* array, int index, const char* property, Span<const EntityRef> entities, const Vec2& value) = 0;
	virtual void setProperty(ComponentType component, const char* array, int index, const char* property, Span<const EntityRef> entities, const Vec3& value) = 0;
	virtual void setProperty(ComponentType component, const char* array, int index, const char* property, Span<const EntityRef> entities, const Vec4& value) = 0;
	virtual void setProperty(ComponentType component, const char* array, int index, const char* property, Span<const EntityRef> entities, const IVec3& value) = 0;
	
	virtual void addArrayPropertyItem(const struct ComponentUID& cmp, const char* property) = 0;
	virtual void removeArrayPropertyItem(const ComponentUID& cmp, int index, const char* property) = 0;
	virtual const Array<EntityRef>& getSelectedEntities() const = 0;
	virtual bool isEntitySelected(EntityRef entity) const = 0;
	virtual void makeParent(EntityPtr parent, EntityRef child) = 0;

	virtual void copyEntities() = 0;
	virtual bool canPasteEntities() const = 0;
	virtual void pasteEntities() = 0;
    virtual void duplicateEntities() = 0;

	virtual void loadWorld(const char* basename, bool additive) = 0;
	virtual void loadWorld(InputMemoryStream& blob, const char* basename, bool additive) = 0;
	virtual void saveWorld(const char* basename, bool save_path) = 0;
	virtual bool isLoading() const = 0;
	virtual void newWorld() = 0;
	virtual void toggleGameMode() = 0;
	virtual void destroyWorldPartition(World::PartitionHandle partition) = 0;
	virtual void serializeWorldPartition(World::PartitionHandle partition, OutputMemoryStream& blob) = 0;
	virtual EntityRef cloneEntity(World& src_u, EntityRef src_e, World& dst_u, EntityPtr dst_parent, Array<EntityRef>& entities, const HashMap<EntityPtr, EntityPtr>& map) const = 0;
	
	virtual DelegateList<void()>& worldCreated() = 0;
	virtual DelegateList<void()>& worldDestroyed() = 0;
	virtual DelegateList<void()>& entitySelectionChanged() = 0;

	virtual u16 createEntityFolder(u16 parent) = 0;
	virtual void destroyEntityFolder(u16 folder) = 0;
	virtual void renameEntityFolder(u16 folder, const char* new_name) = 0;
	virtual void moveEntityToFolder(EntityRef entity, u16 folder) = 0;

	virtual struct PrefabSystem& getPrefabSystem() = 0;
	virtual struct EntityFolders& getEntityFolders() = 0;
	virtual void snapEntities(const DVec3& hit_pos, bool translate_mode) = 0;

	virtual bool isGameMode() const = 0;
	virtual ~WorldEditor() {}
};

}