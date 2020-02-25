#include "world_editor.h"

#include "editor/editor_icon.h"
#include "editor/gizmo.h"
#include "editor/measure_tool.h"
#include "editor/prefab_system.h"
#include "engine/array.h"
#include "engine/associative_array.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/crt.h"
#include "engine/delegate_list.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/geometry.h"
#include "engine/plugin.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "engine/universe.h"
#include "render_interface.h"


namespace Lumix
{


static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("model_instance");
static const ComponentType CAMERA_TYPE = Reflection::getComponentType("camera");

struct UniverseViewImpl final : UniverseView {
	enum class MouseMode
	{
		NONE,
		SELECT,
		NAVIGATE,
		PAN,

		CUSTOM
	};

	enum class SnapMode
	{
		NONE,
		FREE,
		VERTEX
	};

	UniverseViewImpl(WorldEditor& editor) 
		: m_editor(editor) 
		, m_orbit_delta(0)
	{}

	void setSnapMode(bool enable, bool vertex_snap) override
	{
		m_snap_mode = enable ? (vertex_snap ? SnapMode::VERTEX : SnapMode::FREE) : SnapMode::NONE;
	}

	void previewSnapVertex()
	{
		if (m_snap_mode != SnapMode::VERTEX) return;

		DVec3 origin;
		Vec3 dir;
		m_viewport.getRay(m_mouse_pos, origin, dir);
		const WorldEditor::RayHit hit = m_editor.getRenderInterface()->castRay(origin, dir, INVALID_ENTITY);
		if (!hit.is_hit) return;

		const DVec3 snap_pos = getClosestVertex(hit);
		m_editor.getRenderInterface()->addDebugCross(snap_pos, 1, 0xfff00fff);
	}

	Vec2 getMouseSensitivity() override
	{
		return m_mouse_sensitivity;
	}

	void setMouseSensitivity(float x, float y) override
	{
		m_mouse_sensitivity.x = 10000 / x;
		m_mouse_sensitivity.y = 10000 / y;
	}

	void rectSelect()
	{
		Array<EntityRef> entities(m_editor.getAllocator());

		Vec2 min = m_rect_selection_start;
		Vec2 max = m_mouse_pos;
		if (min.x > max.x) swap(min.x, max.x);
		if (min.y > max.y) swap(min.y, max.y);
		const ShiftedFrustum frustum = m_viewport.getFrustum(min, max);
		m_editor.getRenderInterface()->getRenderables(entities, frustum);
		m_editor.selectEntities(entities.empty() ? nullptr : &entities[0], entities.size(), false);
	}

	void onMouseUp(int x, int y, OS::MouseButton button) override
	{
		m_mouse_pos = {(float)x, (float)y};
		if (m_mouse_mode == MouseMode::SELECT)
		{
			if (m_rect_selection_start.x != m_mouse_pos.x || m_rect_selection_start.y != m_mouse_pos.y)
			{
				rectSelect();
			}
			else
			{
				DVec3 origin;
				Vec3 dir;
				m_viewport.getRay(m_mouse_pos, origin, dir);
				auto hit = m_editor.getRenderInterface()->castRay(origin, dir, INVALID_ENTITY);

				const Array<EntityRef>& selected_entities = m_editor.getSelectedEntities();
				if (m_snap_mode != SnapMode::NONE && !selected_entities.empty() && hit.is_hit)
				{
					DVec3 snap_pos = origin + dir * hit.t;
					if (m_snap_mode == SnapMode::VERTEX) snap_pos = getClosestVertex(hit);
					const Quat rot = m_editor.getUniverse()->getRotation(selected_entities[0]);
					const Vec3 offset = rot.rotate(m_editor.getGizmo().getOffset());
					m_editor.snapEntities(snap_pos - offset);
				}
				else
				{
					auto icon_hit = m_editor.getIcons().raycast(origin, dir);
					if (icon_hit.entity != INVALID_ENTITY)
					{
						if(icon_hit.entity.isValid()) {
							EntityRef e = (EntityRef)icon_hit.entity;
							m_editor.selectEntities(&e, 1, true);
						}
					}
					else if (hit.is_hit)
					{
						if(hit.entity.isValid()) {
							EntityRef entity = (EntityRef)hit.entity;
							m_editor.selectEntities(&entity, 1, true);
						}
					}
				}
			}
		}

		m_is_mouse_down[(int)button] = false;
		if (m_mouse_handling_plugin)
		{
			m_mouse_handling_plugin->onMouseUp(x, y, button);
			m_mouse_handling_plugin = nullptr;
		}
		m_mouse_mode = MouseMode::NONE;
	}

	Vec2 getMousePos() const override { return m_mouse_pos; }
	void setCustomPivot() override
	{
		const Array<EntityRef>& selected_entities = m_editor.getSelectedEntities();
		if (selected_entities.empty()) return;

		DVec3 origin;
		Vec3 dir;		
		m_viewport.getRay(m_mouse_pos, origin, dir);
		auto hit = m_editor.getRenderInterface()->castRay(origin, dir, INVALID_ENTITY);
		if (!hit.is_hit || hit.entity != selected_entities[0]) return;

		const DVec3 snap_pos = getClosestVertex(hit);

		const Transform tr = m_editor.getUniverse()->getTransform(selected_entities[0]);
		m_editor.getGizmo().setOffset(tr.rot.conjugated() * (snap_pos - tr.pos).toFloat());
	}

	DVec3 getClosestVertex(const WorldEditor::RayHit& hit)
	{
		ASSERT(hit.is_hit);
		return m_editor.getRenderInterface()->getClosestVertex(m_editor.getUniverse(), (EntityRef)hit.entity, hit.pos);
	}

	void inputFrame() override
	{
		for (auto& i : m_is_mouse_click) i = false;
	}

	bool isMouseClick(OS::MouseButton button) const override
	{
		return m_is_mouse_click[(int)button];
	}

	bool isMouseDown(OS::MouseButton button) const override
	{
		return m_is_mouse_down[(int)button];
	}

	void onMouseMove(int x, int y, int relx, int rely) override
	{
		PROFILE_FUNCTION();
		m_mouse_pos.set((float)x, (float)y);
		
		static const float MOUSE_MULTIPLIER = 1 / 200.0f;

		switch (m_mouse_mode)
		{
			case MouseMode::CUSTOM:
			{
				if (m_mouse_handling_plugin)
				{
					m_mouse_handling_plugin->onMouseMove(x, y, relx, rely);
				}
			}
			break;
			case MouseMode::NAVIGATE: {
				const float yaw = -signum(relx) * (powf(fabsf((float)relx / m_mouse_sensitivity.x), 1.2f));
				const float pitch = -signum(rely) * (powf(fabsf((float)rely / m_mouse_sensitivity.y), 1.2f));
				rotateCamera(yaw, pitch);
				break;
			}
			case MouseMode::PAN: panCamera(relx * MOUSE_MULTIPLIER, rely * MOUSE_MULTIPLIER); break;
			case MouseMode::NONE:
			case MouseMode::SELECT:
				break;
		}
	}

	void onMouseDown(int x, int y, OS::MouseButton button) override
	{
		m_is_mouse_click[(int)button] = true;
		m_is_mouse_down[(int)button] = true;
		if(button == OS::MouseButton::MIDDLE)
		{
			m_mouse_mode = MouseMode::PAN;
		}
		else if (button == OS::MouseButton::RIGHT)
		{
			m_mouse_mode = MouseMode::NAVIGATE;
		}
		else if (button == OS::MouseButton::LEFT)
		{
			DVec3 origin;
			Vec3 dir;
			m_viewport.getRay({(float)x, (float)y}, origin, dir);
			const WorldEditor::RayHit hit = m_editor.getRenderInterface()->castRay(origin, dir, INVALID_ENTITY);
			if (m_editor.getGizmo().isActive()) return;

			for (WorldEditor::Plugin* plugin : m_editor.getPlugins())
			{
				if (plugin->onMouseDown(hit, x, y))
				{
					m_mouse_handling_plugin = plugin;
					m_mouse_mode = MouseMode::CUSTOM;
					return;
				}
			}
			m_mouse_mode = MouseMode::SELECT;
			m_rect_selection_start = {(float)x, (float)y};
		}
	}

	const Viewport& getViewport() override { return m_viewport; }
	void setViewport(const Viewport& vp) override { m_viewport = vp; }

	void copyTransform() override {
		if (m_editor.getSelectedEntities().empty()) return;

		m_editor.setEntitiesPositionsAndRotations(m_editor.getSelectedEntities().begin(), &m_viewport.pos, &m_viewport.rot, 1);
	}

	void lookAtSelected() override {
		const Universe* universe = m_editor.getUniverse();
		if (m_editor.getSelectedEntities().empty()) return;

		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		m_go_to_parameters.m_from = m_viewport.pos;
		const Vec3 dir = m_viewport.rot.rotate(Vec3(0, 0, 1));
		m_go_to_parameters.m_to = universe->getPosition(m_editor.getSelectedEntities()[0]) + dir * 10;
		const double len = (m_go_to_parameters.m_to - m_go_to_parameters.m_from).length();
		m_go_to_parameters.m_speed = maximum(100.0f / (len > 0 ? float(len) : 1), 2.0f);
		m_go_to_parameters.m_from_rot = m_go_to_parameters.m_to_rot = m_viewport.rot;

	}
	
	void setTopView() override {
		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		m_go_to_parameters.m_from = m_viewport.pos;
		m_go_to_parameters.m_to = m_go_to_parameters.m_from;
		const Array<EntityRef>& selected_entities = m_editor.getSelectedEntities();
		if (m_is_orbit && !selected_entities.empty()) {
			auto* universe = m_editor.getUniverse();
			m_go_to_parameters.m_to = universe->getPosition(selected_entities[0]) + Vec3(0, 10, 0);
		}
		m_go_to_parameters.m_speed = 2.0f;
		m_go_to_parameters.m_from_rot = m_viewport.rot;
		m_go_to_parameters.m_to_rot = Quat(Vec3(1, 0, 0), -PI * 0.5f);
	}

	void setFrontView() override {
		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		m_go_to_parameters.m_from = m_viewport.pos;
		m_go_to_parameters.m_to = m_go_to_parameters.m_from;
		const Array<EntityRef>& selected_entities = m_editor.getSelectedEntities();
		if (m_is_orbit && !selected_entities.empty()) {
			auto* universe = m_editor.getUniverse();
			m_go_to_parameters.m_to = universe->getPosition(selected_entities[0]) + Vec3(0, 0, -10);
		}
		m_go_to_parameters.m_speed = 2.0f;
		m_go_to_parameters.m_from_rot = m_viewport.rot;
		m_go_to_parameters.m_to_rot = Quat(Vec3(0, 1, 0), PI);
	}


	void setSideView() override {
		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		m_go_to_parameters.m_from = m_viewport.pos;
		m_go_to_parameters.m_to = m_go_to_parameters.m_from;
		const Array<EntityRef>& selected_entities = m_editor.getSelectedEntities();
		if (m_is_orbit && !selected_entities.empty()) {
			auto* universe = m_editor.getUniverse();
			m_go_to_parameters.m_to = universe->getPosition(selected_entities[0]) + Vec3(-10, 0, 0);
		}
		m_go_to_parameters.m_speed = 2.0f;
		m_go_to_parameters.m_from_rot = m_viewport.rot;
		m_go_to_parameters.m_to_rot = Quat(Vec3(0, 1, 0), -PI * 0.5f);
	}
	
	bool isOrbitCamera() const override { return m_is_orbit; }

	void setOrbitCamera(bool enable) override
	{
		m_orbit_delta = Vec2(0, 0);
		m_is_orbit = enable;
	}

	void moveCamera(float forward, float right, float up, float speed) override
	{
		const Quat rot = m_viewport.rot;

		right = m_is_orbit ? 0 : right;

		m_viewport.pos += rot.rotate(Vec3(0, 0, -1)) * forward * speed;
		m_viewport.pos += rot.rotate(Vec3(1, 0, 0)) * right * speed;
		m_viewport.pos += rot.rotate(Vec3(0, 1, 0)) * up * speed;
	}

	void rotateCamera(float yaw, float pitch) {
		const Universe* universe = m_editor.getUniverse();
		DVec3 pos = m_viewport.pos;
		Quat rot = m_viewport.rot;
		const Quat old_rot = rot;

		Quat yaw_rot(Vec3(0, 1, 0), yaw);
		rot = yaw_rot * rot;
		rot.normalize();

		Vec3 pitch_axis = rot.rotate(Vec3(1, 0, 0));
		const Quat pitch_rot(pitch_axis, pitch);
		rot = pitch_rot * rot;
		rot.normalize();

		if (m_is_orbit && !m_editor.getSelectedEntities().empty())
		{
			const Vec3 dir = rot.rotate(Vec3(0, 0, 1));
			const DVec3 entity_pos = universe->getPosition(m_editor.getSelectedEntities()[0]);
			DVec3 nondelta_pos = pos;

			nondelta_pos -= old_rot.rotate(Vec3(0, -1, 0)) * m_orbit_delta.y;
			nondelta_pos -= old_rot.rotate(Vec3(1, 0, 0)) * m_orbit_delta.x;

			const float dist = float((entity_pos - nondelta_pos).length());
			pos = entity_pos + dir * dist;
			pos += rot.rotate(Vec3(1, 0, 0)) * m_orbit_delta.x;
			pos += rot.rotate(Vec3(0, -1, 0)) * m_orbit_delta.y;
		}

		m_viewport.pos = pos;
		m_viewport.rot = rot;
	}

	void panCamera(float x, float y) {
		const Quat rot = m_viewport.rot;

		if (m_is_orbit) {
			m_orbit_delta.x += x;
			m_orbit_delta.y += y;
		}

		m_viewport.pos += rot.rotate(Vec3(x, 0, 0));
		m_viewport.pos += rot.rotate(Vec3(0, -y, 0));
	}

	void update() {
		previewSnapVertex();
		
		if (m_is_mouse_down[(int)OS::MouseButton::LEFT] && m_mouse_mode == MouseMode::SELECT) {
			m_editor.getRenderInterface()->addRect2D(m_rect_selection_start, m_mouse_pos, 0xfffffFFF);
			m_editor.getRenderInterface()->addRect2D(m_rect_selection_start - Vec2(1, 1), m_mouse_pos + Vec2(1, 1), 0xff000000);
		}

		if (!m_go_to_parameters.m_is_active) return;

		float t = easeInOut(m_go_to_parameters.m_t);
		m_go_to_parameters.m_t += m_editor.getEngine().getLastTimeDelta() * m_go_to_parameters.m_speed;
		DVec3 pos = m_go_to_parameters.m_from * (1 - t) + m_go_to_parameters.m_to * t;
		Quat rot;
		rot = nlerp(m_go_to_parameters.m_from_rot, m_go_to_parameters.m_to_rot, t);
		if (m_go_to_parameters.m_t >= 1)
		{
			pos = m_go_to_parameters.m_to;
			m_go_to_parameters.m_is_active = false;
		}
		m_viewport.pos = pos;
		m_viewport.rot = rot;
	}

	struct {
		bool m_is_active = false;
		DVec3 m_from;
		DVec3 m_to;
		Quat m_from_rot;
		Quat m_to_rot;
		float m_t;
		float m_speed;
	} m_go_to_parameters;

	bool m_is_orbit = false;
	Vec2 m_orbit_delta;
	WorldEditor& m_editor;
	Viewport m_viewport;

	MouseMode m_mouse_mode = MouseMode::NONE;
	SnapMode m_snap_mode = SnapMode::NONE;
	Vec2 m_mouse_pos;
	Vec2 m_mouse_sensitivity{200, 200};
	bool m_is_mouse_down[(int)OS::MouseButton::EXTENDED] = {};
	bool m_is_mouse_click[(int)OS::MouseButton::EXTENDED] = {};
	WorldEditor::Plugin* m_mouse_handling_plugin = nullptr;
	Vec2 m_rect_selection_start;
};

struct BeginGroupCommand final : IEditorCommand
{
	BeginGroupCommand() = default;
	explicit BeginGroupCommand(WorldEditor&) {}

	bool execute() override { return true; }
	void undo() override { ASSERT(false); }
	bool merge(IEditorCommand& command) override { ASSERT(false); return false; }
	const char* getType() override { return "begin_group"; }
};


struct EndGroupCommand final : IEditorCommand
{
	EndGroupCommand() = default;
	EndGroupCommand(WorldEditor&) {}

	bool execute() override { return true; }
	void undo() override { ASSERT(false); }
	bool merge(IEditorCommand& command) override { ASSERT(false); return false; }
	const char* getType() override { return "end_group"; }

	u32 group_type;
};


struct SetEntityNameCommand final : IEditorCommand
{
public:
	SetEntityNameCommand(WorldEditor& editor, EntityRef entity, const char* name)
		: m_entity(entity)
		, m_new_name(name, editor.getAllocator())
		, m_old_name(editor.getUniverse()->getEntityName(entity),
					 editor.getAllocator())
		, m_editor(editor)
	{
	}


	bool execute() override
	{
		m_editor.getUniverse()->setEntityName((EntityRef)m_entity, m_new_name.c_str());
		return true;
	}


	void undo() override
	{
		m_editor.getUniverse()->setEntityName((EntityRef)m_entity, m_old_name.c_str());
	}


	const char* getType() override { return "set_entity_name"; }


	bool merge(IEditorCommand& command) override
	{
		ASSERT(command.getType() == getType());
		if (static_cast<SetEntityNameCommand&>(command).m_entity == m_entity)
		{
			static_cast<SetEntityNameCommand&>(command).m_new_name = m_new_name;
			return true;
		}
		else
		{
			return false;
		}
	}

private:
	WorldEditor& m_editor;
	EntityRef m_entity;
	String m_new_name;
	String m_old_name;
};


struct MoveEntityCommand final : IEditorCommand
{
public:
	explicit MoveEntityCommand(WorldEditor& editor)
		: m_new_positions(editor.getAllocator())
		, m_new_rotations(editor.getAllocator())
		, m_old_positions(editor.getAllocator())
		, m_old_rotations(editor.getAllocator())
		, m_entities(editor.getAllocator())
		, m_editor(editor)
	{
	}


	MoveEntityCommand(WorldEditor& editor,
		const EntityRef* entities,
		const DVec3* new_positions,
		const Quat* new_rotations,
		int count,
		IAllocator& allocator)
		: m_new_positions(allocator)
		, m_new_rotations(allocator)
		, m_old_positions(allocator)
		, m_old_rotations(allocator)
		, m_entities(allocator)
		, m_editor(editor)
	{
		ASSERT(count > 0);
		Universe* universe = m_editor.getUniverse();
		PrefabSystem& prefab_system = m_editor.getPrefabSystem();
		m_entities.reserve(count);
		m_new_positions.reserve(count);
		m_new_rotations.reserve(count);
		m_old_positions.reserve(count);
		m_old_rotations.reserve(count);
		for (int i = count - 1; i >= 0; --i)
		{
			EntityPtr parent = universe->getParent(entities[i]);
			m_entities.push(entities[i]);
			m_new_positions.push(new_positions[i]);
			m_new_rotations.push(new_rotations[i]);
			m_old_positions.push(universe->getPosition(entities[i]));
			m_old_rotations.push(universe->getRotation(entities[i]));
		}
	}


	bool execute() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if(m_entities[i].isValid()) {
				const EntityRef entity = (EntityRef)m_entities[i];
				universe->setPosition(entity, m_new_positions[i]);
				universe->setRotation(entity, m_new_rotations[i]);
			}
		}
		return true;
	}


	void undo() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if(m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				universe->setPosition(entity, m_old_positions[i]);
				universe->setRotation(entity, m_old_rotations[i]);
			}
		}
	}


	const char* getType() override { return "move_entity"; }


	bool merge(IEditorCommand& command) override
	{
		ASSERT(command.getType() == getType());
		MoveEntityCommand& my_command = static_cast<MoveEntityCommand&>(command);
		if (my_command.m_entities.size() == m_entities.size())
		{
			for (int i = 0, c = m_entities.size(); i < c; ++i)
			{
				if (m_entities[i] != my_command.m_entities[i])
				{
					return false;
				}
			}
			for (int i = 0, c = m_entities.size(); i < c; ++i)
			{
				my_command.m_new_positions[i] = m_new_positions[i];
				my_command.m_new_rotations[i] = m_new_rotations[i];
			}
			return true;
		}
		else
		{
			return false;
		}
	}

private:
	WorldEditor& m_editor;
	Array<EntityPtr> m_entities;
	Array<DVec3> m_new_positions;
	Array<Quat> m_new_rotations;
	Array<DVec3> m_old_positions;
	Array<Quat> m_old_rotations;
};


struct LocalMoveEntityCommand final : IEditorCommand
{
public:
	explicit LocalMoveEntityCommand(WorldEditor& editor)
		: m_new_positions(editor.getAllocator())
		, m_old_positions(editor.getAllocator())
		, m_entities(editor.getAllocator())
		, m_editor(editor)
	{
	}


	LocalMoveEntityCommand(WorldEditor& editor,
		const EntityRef* entities,
		const DVec3* new_positions,
		int count,
		IAllocator& allocator)
		: m_new_positions(allocator)
		, m_old_positions(allocator)
		, m_entities(allocator)
		, m_editor(editor)
	{
		ASSERT(count > 0);
		Universe* universe = m_editor.getUniverse();
		PrefabSystem& prefab_system = m_editor.getPrefabSystem();
		m_entities.reserve(count);
		m_new_positions.reserve(count);
		m_old_positions.reserve(count);
		for (int i = count - 1; i >= 0; --i)
		{
			EntityPtr parent = universe->getParent(entities[i]);
			m_entities.push(entities[i]);
			m_new_positions.push(new_positions[i]);
			m_old_positions.push(universe->getPosition(entities[i]));
		}
	}


	bool execute() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if (m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				universe->setLocalPosition(entity, m_new_positions[i]);
			}
		}
		return true;
	}


	void undo() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if (m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				universe->setLocalPosition(entity, m_old_positions[i]);
			}
		}
	}


	const char* getType() override { return "local_move_entity"; }


	bool merge(IEditorCommand& command) override
	{
		ASSERT(command.getType() == getType());
		LocalMoveEntityCommand& my_command = static_cast<LocalMoveEntityCommand&>(command);
		if (my_command.m_entities.size() == m_entities.size())
		{
			for (int i = 0, c = m_entities.size(); i < c; ++i)
			{
				if (m_entities[i] != my_command.m_entities[i])
				{
					return false;
				}
			}
			for (int i = 0, c = m_entities.size(); i < c; ++i)
			{
				my_command.m_new_positions[i] = m_new_positions[i];
			}
			return true;
		}
		else
		{
			return false;
		}
	}

private:
	WorldEditor& m_editor;
	Array<EntityPtr> m_entities;
	Array<DVec3> m_new_positions;
	Array<DVec3> m_old_positions;
};


struct ScaleEntityCommand final : IEditorCommand
{
public:
	explicit ScaleEntityCommand(WorldEditor& editor)
		: m_old_scales(editor.getAllocator())
		, m_new_scales(editor.getAllocator())
		, m_entities(editor.getAllocator())
		, m_editor(editor)
	{
	}


	ScaleEntityCommand(WorldEditor& editor,
		const EntityRef* entities,
		int count,
		float scale,
		IAllocator& allocator)
		: m_old_scales(allocator)
		, m_new_scales(allocator)
		, m_entities(allocator)
		, m_editor(editor)
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = count - 1; i >= 0; --i)
		{
			m_entities.push(entities[i]);
			m_old_scales.push(universe->getScale(entities[i]));
			m_new_scales.push(scale);
		}
	}


	ScaleEntityCommand(WorldEditor& editor,
		const EntityRef* entities,
		const float* scales,
		int count,
		IAllocator& allocator)
		: m_old_scales(allocator)
		, m_new_scales(allocator)
		, m_entities(allocator)
		, m_editor(editor)
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = count - 1; i >= 0; --i)
		{
			m_entities.push(entities[i]);
			m_old_scales.push(universe->getScale(entities[i]));
			m_new_scales.push(scales[i]);
		}
	}


	bool execute() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if (m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				universe->setScale(entity, m_new_scales[i]);
			}
		}
		return true;
	}


	void undo() override
	{
		Universe* universe = m_editor.getUniverse();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if (m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				universe->setScale(entity, m_old_scales[i]);
			}
		}
	}


	const char* getType() override { return "scale_entity"; }


	bool merge(IEditorCommand& command) override
	{
		ASSERT(command.getType() == getType());
		auto& my_command = static_cast<ScaleEntityCommand&>(command);
		if (my_command.m_entities.size() == m_entities.size())
		{
			for (int i = 0, c = m_entities.size(); i < c; ++i)
			{
				if (m_entities[i] != my_command.m_entities[i])
				{
					return false;
				}
				if (m_new_scales[i] != my_command.m_new_scales[i])
				{
					return false;
				}
			}
			return true;
		}
		else
		{
			return false;
		}
	}

private:
	WorldEditor& m_editor;
	Array<EntityPtr> m_entities;
	Array<float> m_new_scales;
	Array<float> m_old_scales;
};


struct GatherResourcesVisitor : IComponentVisitor
{
	void visit(const Prop<float>& prop) override {}
	void visit(const Prop<bool>& prop) override {}
	void visit(const Prop<i32>& prop) override {}
	void visit(const Prop<u32>& prop) override {}
	void visit(const Prop<Vec2>& prop) override {}
	void visit(const Prop<Vec3>& prop) override {}
	void visit(const Prop<IVec3>& prop) override {}
	void visit(const Prop<Vec4>& prop) override {}
	void visit(const Prop<EntityPtr>& prop) override {}
	void visit(const Prop<const char*>& prop) override {}
	bool beginArray(const char* name, const ArrayProp& prop) override { return true; }

	void visit(const Prop<Path>& prop) override {
		auto* attr = (Reflection::ResourceAttribute*)prop.getAttribute(Reflection::IAttribute::RESOURCE);
		if (!attr) return;

		const Path path = prop.get();
		Resource* resource = resource_manager->load(attr->type, path);
		if (resource) resources->push(resource);
	}

	ResourceManagerHub* resource_manager;
	Array<Resource*>* resources;
};


static void addArrayItem(WorldEditor& editor, Span<const EntityRef> entities, ComponentType cmp_type, const char* prop_name, u32 index)
{
	struct : IComponentVisitor {
		void visit(const Prop<float>& prop) override {}
		void visit(const Prop<bool>& prop) override {}
		void visit(const Prop<i32>& prop) override {}
		void visit(const Prop<u32>& prop) override {}
		void visit(const Prop<Vec2>& prop) override {}
		void visit(const Prop<Vec3>& prop) override {}
		void visit(const Prop<IVec3>& prop) override {}
		void visit(const Prop<Vec4>& prop) override {}
		void visit(const Prop<Path>& prop) override {}
		void visit(const Prop<EntityPtr>& prop) override {}
		void visit(const Prop<const char*>& prop) override {}
	
		bool beginArray(const char* name, const ArrayProp& prop) override {
			if (equalStrings(name, prop_name)) {
				prop.add(index);
			}
			return false;
		}
		const char* prop_name;
		u32 index;
	} v; 

	v.prop_name = prop_name;
	v.index = index;

	for (const EntityRef& e : entities) {
		editor.getUniverse()->getScene(cmp_type)->visit(e, cmp_type, v);
	}
}


static void removeArrayItem(WorldEditor& editor, Span<const EntityRef> entities, ComponentType cmp_type, const char* prop_name, u32 index)
{
	struct : IComponentVisitor {
		void visit(const Prop<float>& prop) override {}
		void visit(const Prop<bool>& prop) override {}
		void visit(const Prop<i32>& prop) override {}
		void visit(const Prop<u32>& prop) override {}
		void visit(const Prop<Vec2>& prop) override {}
		void visit(const Prop<Vec3>& prop) override {}
		void visit(const Prop<IVec3>& prop) override {}
		void visit(const Prop<Vec4>& prop) override {}
		void visit(const Prop<Path>& prop) override {}
		void visit(const Prop<EntityPtr>& prop) override {}
		void visit(const Prop<const char*>& prop) override {}
	
		bool beginArray(const char* name, const ArrayProp& prop) override {
			if (equalStrings(name, prop_name)) {
				prop.remove(index);
			}
			return false;
		}
		const char* prop_name;
		u32 index;
	} v; 

	v.index = index;
	v.prop_name = prop_name;
	for (const EntityRef& e : entities) {
		editor.getUniverse()->getScene(cmp_type)->visit(e, cmp_type, v);
	}
}

static void saveArrayItem(WorldEditor& editor, EntityRef e, ComponentType cmp_type, const char* name, u32 idx, Ref<OutputMemoryStream> out) {
	struct : IComponentVisitor {
		void visit(const Prop<float>& prop) override { if(save) (*out)->write(prop.get()); }
		void visit(const Prop<bool>& prop) override { if(save) (*out)->write(prop.get()); }
		void visit(const Prop<i32>& prop) override { if(save) (*out)->write(prop.get()); }
		void visit(const Prop<u32>& prop) override { if(save) (*out)->write(prop.get()); }
		void visit(const Prop<Vec2>& prop) override { if(save) (*out)->write(prop.get()); }
		void visit(const Prop<Vec3>& prop) override { if(save) (*out)->write(prop.get()); }
		void visit(const Prop<IVec3>& prop) override { if(save) (*out)->write(prop.get()); }
		void visit(const Prop<Vec4>& prop) override { if(save) (*out)->write(prop.get()); }
		void visit(const Prop<EntityPtr>& prop) override { if(save) (*out)->write(prop.get()); }
		void visit(const Prop<Path>& prop) override { if(save) (*out)->writeString(prop.get().c_str()); }
		void visit(const Prop<const char*>& prop) override { if(save) (*out)->writeString(prop.get()); }
	
		bool beginArray(const char* name, const ArrayProp& prop) override { return true; }
		bool beginArrayItem(const char* name, u32 idx, const ArrayProp& prop) { 
			if (idx == index && equalStrings(name, prop_name)) {
				save = true;
				return true;
			}
			return false; 
		}
		void endArrayItem() { save = false; };
		void endArray() {}

		bool save = false;
		u32 index;
		const char* prop_name;
		Ref<OutputMemoryStream>* out;
	} v;

	v.out = &out;
	v.index = idx;
	v.prop_name = name;

	editor.getUniverse()->getScene(cmp_type)->visit(e, cmp_type, v);
}

struct LoadArrayItemVisitor : IComponentVisitor {
	template <typename T>
	void read(const Prop<T>& prop) {
		if (!load) return;

		T val;
		in->read(val);
		prop.set(val);
	}

	void visit(const Prop<float>& prop) override { read(prop); }
	void visit(const Prop<bool>& prop) override { read(prop); }
	void visit(const Prop<i32>& prop) override { read(prop); }
	void visit(const Prop<u32>& prop) override { read(prop); }
	void visit(const Prop<Vec2>& prop) override { read(prop); }
	void visit(const Prop<Vec3>& prop) override { read(prop); }
	void visit(const Prop<IVec3>& prop) override { read(prop); }
	void visit(const Prop<Vec4>& prop) override { read(prop); }
	void visit(const Prop<EntityPtr>& prop) override { read(prop); }
	void visit(const Prop<Path>& prop) override {
		if (!load) return;

		char val[MAX_PATH_LENGTH];
		in->readString(Span(val));
		prop.set(Path(val));
	}

	void visit(const Prop<const char*>& prop) override { 
		if (!load) return;

		// TODO longer strings
		char val[1024];
		in->readString(Span(val));
		prop.set(val);
	}
	
	bool beginArray(const char* name, const ArrayProp& prop) override { return true; }
	bool beginArrayItem(const char* name, u32 idx, const ArrayProp& prop) { 
		if (idx == index && equalStrings(name, prop_name)) {
			load = true;
			return true;
		}
		return false; 
	}
	void endArrayItem() { load = false; };
	void endArray() {}

	bool load = false;
	u32 index;
	const char* prop_name;
	InputMemoryStream* in;
};

static void loadArrayItem(WorldEditor& editor, EntityRef e, ComponentType cmp_type, const char* name, u32 idx, InputMemoryStream& in) {
	LoadArrayItemVisitor v;

	v.in = &in;
	v.prop_name = name;
	v.index = idx;

	editor.getUniverse()->getScene(cmp_type)->visit(e, cmp_type, v);
}
struct RemoveArrayPropertyItemCommand final : IEditorCommand
{
public:
	RemoveArrayPropertyItemCommand(WorldEditor& editor,
		EntityRef entity, 
		ComponentType cmp_type, 
		const char* prop_name,
		u32 index)
		: m_cmp_type(cmp_type)
		, m_prop_name(prop_name)
		, m_entity(entity)
		, m_editor(editor)
		, m_index(index)
		, m_old_values(editor.getAllocator())
	{
		saveArrayItem(editor, entity, cmp_type, prop_name, index, Ref(m_old_values));
	}

	bool execute() override {
		removeArrayItem(m_editor, Span(&m_entity, 1), m_cmp_type, m_prop_name, m_index);
		return true;
	}

	void undo() override {
		addArrayItem(m_editor, Span(&m_entity, 1), m_cmp_type, m_prop_name, m_index);
		InputMemoryStream old_values(m_old_values);
		loadArrayItem(m_editor, m_entity, m_cmp_type, m_prop_name, m_index, old_values);
	}

	const char* getType() override { return "remove_array_property_item"; }

	bool merge(IEditorCommand&) override { return false; }

private:
	WorldEditor& m_editor;
	ComponentType m_cmp_type;
	EntityRef m_entity;
	u32 m_index;
	OutputMemoryStream m_old_values;
	const char* m_prop_name;
};


struct AddArrayPropertyItemCommand final : IEditorCommand
{
public:
	AddArrayPropertyItemCommand(WorldEditor& editor,
		EntityRef entity, 
		ComponentType cmp_type, 
		const char* prop_name,
		u32 index)
		: m_cmp_type(cmp_type)
		, m_prop_name(prop_name)
		, m_entity(entity)
		, m_editor(editor)
		, m_index(index)
	{
	}

	bool execute() override {
		addArrayItem(m_editor, Span(&m_entity, 1), m_cmp_type, m_prop_name, m_index);
		return true;
	}


	void undo() override {
		removeArrayItem(m_editor, Span(&m_entity, 1), m_cmp_type, m_prop_name, m_index);
	}


	const char* getType() override { return "add_array_property_item"; }


	bool merge(IEditorCommand&) override { return false; }

private:
	WorldEditor& m_editor;
	ComponentType m_cmp_type;
	EntityRef m_entity;
	u32 m_index;
	const char* m_prop_name;
};


struct SetPropertyCommand final : IEditorCommand
{
public:
	struct GetterVisitor : IComponentVisitor {
		template <typename T>
		bool isSrc(const Prop<T>& prop) {
			const char* src = cmd->m_property.c_str();
			if (current_array) {
				if (!startsWith(src, current_array)) return false;
				StaticString<256> prop_path(current_array, "[", current_array_idx, "].", prop.name);
				return prop_path == src;
			}
			
			if (!equalStrings(src, prop.name)) return false;
			const char c = src[stringLength(prop.name)];
			return c == '\0';
		}

		template <typename T>
		void get(const Prop<T>& prop) {
			if (!isSrc(prop)) return;
			const T val = prop.get();
			buf->write(&val, sizeof(val));
		}

		void visit(const Prop<float>& prop) override { get(prop); }
		void visit(const Prop<bool>& prop) override { get(prop); }
		void visit(const Prop<i32>& prop) override { get(prop); }
		void visit(const Prop<u32>& prop) override { get(prop); }
		void visit(const Prop<Vec2>& prop) override { get(prop); }
		void visit(const Prop<Vec3>& prop) override { get(prop); }
		void visit(const Prop<IVec3>& prop) override { get(prop); }
		void visit(const Prop<Vec4>& prop) override { get(prop); }
		void visit(const Prop<EntityPtr>& prop) override { get(prop); }
		
		void visit(const Prop<Path>& prop) override { 
			if (!isSrc(prop)) return;
			const Path val = prop.get();
			buf->write(val.c_str(), val.length() + 1);
		}
		void visit(const Prop<const char*>& prop) override { 
			if (!isSrc(prop)) return;
			const char* val = prop.get();
			buf->write(val, stringLength(val) + 1);
		}
	
		bool beginArray(const char* name, const ArrayProp& prop) override { return true; }
		bool beginArrayItem(const char* name, u32 idx, const ArrayProp& prop) {
			ASSERT(!current_array);
			current_array = name;
			current_array_idx = idx;
			return true; 
		}
		void endArrayItem() { current_array = nullptr; }

		OutputMemoryStream* buf;
		SetPropertyCommand* cmd;
		const char* current_array = nullptr;
		u32 current_array_idx;
	};

	struct SetterVisitor : IComponentVisitor {
		template <typename T>
		bool isSrc(const Prop<T>& prop) {
			const char* src = cmd->m_property.c_str();
			if (current_array) {
				if (!startsWith(src, current_array)) return false;
				StaticString<256> prop_path(current_array, "[", current_array_idx, "].", prop.name);
				return prop_path == src;
			}
			
			if (!equalStrings(src, prop.name)) return false;
			const char c = src[stringLength(prop.name)];
			return c == '\0';
		}


		template <typename T>
		void set(const Prop<T>& prop) {
			if (!isSrc(prop)) return;
			T val;
			buf->read(Ref(val));
			prop.set(val);
		}

		void visit(const Prop<float>& prop) override { set(prop); }
		void visit(const Prop<bool>& prop) override { set(prop); }
		void visit(const Prop<i32>& prop) override { set(prop); }
		void visit(const Prop<u32>& prop) override { set(prop); }
		void visit(const Prop<Vec2>& prop) override { set(prop); }
		void visit(const Prop<Vec3>& prop) override { set(prop); }
		void visit(const Prop<IVec3>& prop) override { set(prop); }
		void visit(const Prop<Vec4>& prop) override { set(prop); }
		void visit(const Prop<EntityPtr>& prop) override { set(prop); }
		
		void visit(const Prop<Path>& prop) override {
			if (!isSrc(prop)) return;
			char val[MAX_PATH_LENGTH];
			ASSERT(buf->size() <= sizeof(val));
			buf->read(val, buf->size());
			prop.set(Path(val));
		}
		void visit(const Prop<const char*>& prop) override { 
			if (!isSrc(prop)) return;
			// TODO any string size
			char val[4096];
			ASSERT(buf->size() <= sizeof(val));
			buf->read(val, buf->size());
			prop.set(val);
		}
	
		bool beginArray(const char* name, const ArrayProp& prop) override { return true; }
		bool beginArrayItem(const char* name, u32 idx, const ArrayProp& prop) {
			ASSERT(!current_array);
			current_array = name;
			current_array_idx = idx;
			return true; 
		}
		void endArrayItem() { current_array = nullptr; }
			
		const char* current_array = nullptr;
		u32 current_array_idx;
		InputMemoryStream* buf;
		SetPropertyCommand* cmd;
	};

	SetPropertyCommand(WorldEditor& editor,
		Span<const EntityRef> entities,
		ComponentType component_type,
		const char* property,
		Span<const u8> data)
		: m_component_type(component_type)
		, m_entities(editor.getAllocator())
		, m_property(property, editor.getAllocator())
		, m_editor(editor)
		, m_new_value(editor.getAllocator())
		, m_old_value(editor.getAllocator())
	{
		auto& prefab_system = editor.getPrefabSystem();
		m_entities.reserve(entities.length());

		for (u32 i = 0; i < entities.length(); ++i)
		{
			ComponentUID component = m_editor.getUniverse()->getComponent(entities[i], m_component_type);
			if (!component.isValid()) continue;
			
			GetterVisitor v;
			v.cmd = this;
			v.buf = &m_old_value;
			editor.getUniverse()->getScene(component_type)->visit(entities[i], component_type, v);

			m_entities.push(entities[i]);
		}

		m_new_value.write(data.begin(), data.length());
	}


	bool execute() override
	{
		InputMemoryStream blob(m_new_value);
		for (EntityPtr entity : m_entities) {
			blob.rewind();

			SetterVisitor v;
			v.cmd = this;
			v.buf = &blob;
			m_editor.getUniverse()->getScene(m_component_type)->visit((EntityRef)entity, m_component_type, v);
		}
		return true;
	}


	void undo() override
	{
		InputMemoryStream blob(m_old_value);
		for (EntityPtr entity : m_entities) {
			SetterVisitor v;
			v.cmd = this;
			v.buf = &blob;
			m_editor.getUniverse()->getScene(m_component_type)->visit((EntityRef)entity, m_component_type, v);
		}
	}


	const char* getType() override { return "set_property_values"; }


	bool merge(IEditorCommand& command) override
	{
		ASSERT(command.getType() == getType());
		SetPropertyCommand& src = static_cast<SetPropertyCommand&>(command);
		if (m_component_type == src.m_component_type &&
			m_entities.size() == src.m_entities.size() &&
			src.m_property == m_property)
		{
			for (int i = 0, c = m_entities.size(); i < c; ++i)
			{
				if (m_entities[i] != src.m_entities[i]) return false;
			}

			src.m_new_value = m_new_value;
			return true;
		}
		return false;
	}

private:
	WorldEditor& m_editor;
	ComponentType m_component_type;
	Array<EntityPtr> m_entities;
	OutputMemoryStream m_new_value;
	OutputMemoryStream m_old_value;
	String m_property;
};

struct PasteEntityCommand;


bool WorldEditor::Plugin::showGizmo(ComponentUID) { return false; }



struct WorldEditorImpl final : WorldEditor
{
	friend struct PasteEntityCommand;
private:
	struct AddComponentCommand final : IEditorCommand
	{
		AddComponentCommand(WorldEditorImpl& editor,
							Span<const EntityRef> entities,
							ComponentType type)
			: m_editor(editor)
			, m_entities(editor.getAllocator())
		{
			m_type = type;
			m_entities.reserve(entities.length());
			Universe* universe = m_editor.getUniverse();
			for (EntityRef e : entities) {
				if (!universe->getComponent(e, type).isValid()) {
					m_entities.push(e);
				}
			}
		}


		bool merge(IEditorCommand&) override { return false; }


		const char* getType() override { return "add_component"; }


		bool execute() override
		{
			bool ret = false;
			Universe* universe = m_editor.getUniverse();

			for (EntityRef e : m_entities) {
				ASSERT(!universe->hasComponent(e, m_type));
				universe->createComponent(m_type, e);
				if (universe->hasComponent(e, m_type)) {
					ret = true;
				}
				else {
					logError("Editor") << "Failed to create component on entity " << e.index;
				}
			}
			return ret;
		}


		void undo() override
		{
			Universe* universe = m_editor.getUniverse();
			for (EntityRef e : m_entities) {
				if (universe->hasComponent(e, m_type)) {
					universe->destroyComponent(e, m_type);
					ASSERT(!universe->hasComponent(e, m_type));
				}
			}
		}


	private:
		ComponentType m_type;
		Array<EntityRef> m_entities;
		WorldEditorImpl& m_editor;
	};


	struct MakeParentCommand final : IEditorCommand
	{
	public:
		explicit MakeParentCommand(WorldEditor& editor)
			: m_editor(static_cast<WorldEditorImpl&>(editor))
		{
		}


		MakeParentCommand(WorldEditorImpl& editor, EntityPtr parent, EntityRef child)
			: m_editor(static_cast<WorldEditorImpl&>(editor))
			, m_parent(parent)
			, m_child(child)
		{
		}


		bool merge(IEditorCommand& cmd) override { 
			
			auto& c = (MakeParentCommand&)cmd;
			if (c.m_child != m_child) return false;
			c.m_parent = m_parent;
			return true;
		}


		const char* getType() override { return "make_parent"; }


		bool execute() override
		{
			if(m_child.isValid()) {
				const EntityRef e = (EntityRef)m_child;
				m_old_parent = m_editor.getUniverse()->getParent(e);
				m_editor.getUniverse()->setParent(m_parent, e);
			}
			return true;
		}


		void undo() override
		{
			if(m_child.isValid()) {
				m_editor.getUniverse()->setParent(m_old_parent, (EntityRef)m_child);
			}
		}

	private:
		WorldEditor& m_editor;
		EntityPtr m_parent;
		EntityPtr m_old_parent;
		EntityPtr m_child;
	};


	struct DestroyEntitiesCommand final : IEditorCommand
	{
	public:
		explicit DestroyEntitiesCommand(WorldEditor& editor)
			: m_editor(static_cast<WorldEditorImpl&>(editor))
			, m_entities(editor.getAllocator())
			, m_transformations(editor.getAllocator())
			, m_old_values(editor.getAllocator())
			, m_resources(editor.getAllocator())
		{
		}


		DestroyEntitiesCommand(WorldEditorImpl& editor, const EntityRef* entities, int count)
			: m_editor(editor)
			, m_entities(editor.getAllocator())
			, m_transformations(editor.getAllocator())
			, m_old_values(editor.getAllocator())
			, m_resources(editor.getAllocator())
		{
			m_entities.reserve(count);
			for (int i = 0; i < count; ++i)
			{
				m_entities.push(entities[i]);
				pushChildren(entities[i]);
			}
			m_entities.removeDuplicates();
			m_transformations.reserve(m_entities.size());
		}


		~DestroyEntitiesCommand()
		{
			for (Resource* resource : m_resources)
			{
				resource->getResourceManager().unload(*resource);
			}
		}


		void pushChildren(EntityRef entity)
		{
			Universe* universe = m_editor.getUniverse();
			for (EntityPtr e = universe->getFirstChild(entity); e.isValid(); e = universe->getNextSibling((EntityRef)e))
			{
				m_entities.push((EntityRef)e);
				pushChildren((EntityRef)e);
			}
		}


		bool execute() override
		{
			m_editor.selectEntities(nullptr, 0, false);
			Universe* universe = m_editor.getUniverse();
			m_transformations.clear();
			m_old_values.clear();
			ResourceManagerHub& resource_manager = m_editor.getEngine().getResourceManager();
			for (int i = 0; i < m_entities.size(); ++i)
			{
				m_transformations.emplace(universe->getTransform(m_entities[i]));
				int count = 0;
				for (ComponentUID cmp = universe->getFirstComponent(m_entities[i]);
					cmp.isValid();
					cmp = universe->getNextComponent(cmp))
				{
					++count;
				}
				m_old_values.writeString(universe->getEntityName(m_entities[i]));
				EntityPtr parent = universe->getParent(m_entities[i]);
				m_old_values.write(parent);
				if (parent.isValid())
				{
					Transform local_tr = universe->getLocalTransform(m_entities[i]);
					m_old_values.write(local_tr);
				}
				for (EntityPtr child = universe->getFirstChild(m_entities[i]); child.isValid(); child = universe->getNextSibling((EntityRef)child))
				{
					m_old_values.write(child);
					Transform local_tr = universe->getLocalTransform((EntityRef)child);
					m_old_values.write(local_tr);
				}
				m_old_values.write(INVALID_ENTITY);

				m_old_values.write(count);
				for (ComponentUID cmp = universe->getFirstComponent(m_entities[i]);
					cmp.isValid();
					cmp = universe->getNextComponent(cmp))
				{
					m_old_values.write(cmp.type);

					GatherResourcesVisitor gather;
					gather.resources = &m_resources;
					gather.resource_manager = &resource_manager;
					cmp.scene->visit((EntityRef)cmp.entity, cmp.type, gather);

					Reflection::serializeComponent(*cmp.scene, (EntityRef)cmp.entity, cmp.type, Ref(m_old_values));
				}
				const PrefabHandle prefab = m_editor.getPrefabSystem().getPrefab(m_entities[i]);
				m_old_values.write(prefab);
			}
			for (EntityRef e : m_entities)
			{
				universe->destroyEntity(e);
			}
			return true;
		}


		bool merge(IEditorCommand&) override { return false; }


		void undo() override
		{
			Universe* universe = m_editor.getUniverse();
			InputMemoryStream blob(m_old_values);
			for (int i = 0; i < m_entities.size(); ++i)
			{
				universe->emplaceEntity(m_entities[i]);
			}
			for (int i = 0; i < m_entities.size(); ++i)
			{
				EntityRef new_entity = m_entities[i];
				universe->setTransform(new_entity, m_transformations[i]);
				int cmps_count;
				char name[Universe::ENTITY_NAME_MAX_LENGTH];
				blob.readString(Span(name));
				universe->setEntityName(new_entity, name);
				EntityPtr parent;
				blob.read(parent);
				if (parent.isValid())
				{
					Transform local_tr;
					blob.read(local_tr);
					universe->setParent(parent, new_entity);
					universe->setLocalTransform(new_entity, local_tr);
				}
				EntityPtr child;
				for(blob.read(child); child.isValid(); blob.read(child))
				{
					Transform local_tr;
					blob.read(local_tr);
					universe->setParent(new_entity, (EntityRef)child);
					universe->setLocalTransform((EntityRef)child, local_tr);
				}

				blob.read(cmps_count);
				for (int j = 0; j < cmps_count; ++j)
				{
					ComponentType cmp_type;
					blob.read(cmp_type);
					universe->createComponent(cmp_type, new_entity);
					
					HashMap<EntityPtr, u32> map(m_editor.getAllocator());
					map.insert(new_entity, 0);
					Reflection::deserializeComponent(*universe->getScene(cmp_type), new_entity, cmp_type, map, Span(&new_entity, 1), Ref(blob));
				}
				PrefabHandle tpl;
				blob.read(tpl);
				if (tpl) m_editor.getPrefabSystem().setPrefab(new_entity, tpl);
			}
		}


		const char* getType() override { return "destroy_entities"; }


	private:
		WorldEditorImpl& m_editor;
		Array<EntityRef> m_entities;
		Array<Transform> m_transformations;
		OutputMemoryStream m_old_values;
		Array<Resource*> m_resources;
	};


	struct DestroyComponentCommand final : IEditorCommand
	{
	public:
		explicit DestroyComponentCommand(WorldEditor& editor)
			: m_editor(static_cast<WorldEditorImpl&>(editor))
			, m_old_values(editor.getAllocator())
			, m_entities(editor.getAllocator())
			, m_cmp_type(INVALID_COMPONENT_TYPE)
			, m_resources(editor.getAllocator())
		{
		}


		DestroyComponentCommand(WorldEditorImpl& editor, Span<const EntityRef> entities, ComponentType cmp_type)
			: m_cmp_type(cmp_type)
			, m_editor(editor)
			, m_old_values(editor.getAllocator())
			, m_entities(editor.getAllocator())
			, m_resources(editor.getAllocator())
		{
			m_entities.reserve(entities.length());
			PrefabSystem& prefab_system = editor.getPrefabSystem();
			for (EntityRef e : entities) {
				if (!m_editor.getUniverse()->getComponent(e, m_cmp_type).isValid()) continue;
				m_entities.push(e);
			}
		}


		~DestroyComponentCommand()
		{
			for (Resource* resource : m_resources) {
				resource->getResourceManager().unload(*resource);
			}
		}


		void undo() override
		{
			ComponentUID cmp;
			Universe* universe = m_editor.getUniverse();
			cmp.scene = universe->getScene(m_cmp_type);
			cmp.type = m_cmp_type;
			ASSERT(cmp.scene);
			InputMemoryStream blob(m_old_values);
			for (EntityRef entity : m_entities) {
				cmp.entity = entity;
				universe->createComponent(cmp.type, entity);
				HashMap<EntityPtr, u32> map(m_editor.getAllocator());
				map.insert(entity, 0);
				Reflection::deserializeComponent(*cmp.scene, entity, cmp.type, map, Span(&entity, 1), Ref(blob));
			}
		}


		bool merge(IEditorCommand&) override { return false; }


		const char* getType() override { return "destroy_components"; }


		bool execute() override
		{
			ASSERT(!m_entities.empty());
			ComponentUID cmp;
			cmp.type = m_cmp_type;
			cmp.scene = m_editor.getUniverse()->getScene(m_cmp_type);
			ASSERT(cmp.scene);

			ResourceManagerHub& resource_manager = m_editor.getEngine().getResourceManager();

			for (EntityRef entity : m_entities) {
				cmp.entity = entity;
				Reflection::serializeComponent(*cmp.scene, entity, cmp.type, Ref(m_old_values));

				GatherResourcesVisitor gather;
				gather.resources = &m_resources;
				gather.resource_manager = &resource_manager;
				cmp.scene->visit((EntityRef)cmp.entity, cmp.type, gather);

				m_editor.getUniverse()->destroyComponent(entity, m_cmp_type);
			}
			return true;
		}

	private:
		Array<EntityRef> m_entities;
		ComponentType m_cmp_type;
		WorldEditorImpl& m_editor;
		OutputMemoryStream m_old_values;
		Array<Resource*> m_resources;
	};


	struct AddEntityCommand final : IEditorCommand
	{
		AddEntityCommand(WorldEditorImpl& editor, const DVec3& position)
			: m_editor(editor)
			, m_position(position)
		{
			m_entity = INVALID_ENTITY;
		}


		bool execute() override
		{
			if (m_entity.isValid()) {
				m_editor.getUniverse()->emplaceEntity((EntityRef)m_entity);
				m_editor.getUniverse()->setPosition((EntityRef)m_entity, m_position);
			}
			else {
				m_entity = m_editor.getUniverse()->createEntity(m_position, Quat(0, 0, 0, 1));
			}
			const EntityRef e = (EntityRef)m_entity;
			m_editor.selectEntities(&e, 1, false);
			return true;
		}


		void undo() override
		{
			ASSERT(m_entity.isValid());

			const EntityRef e = (EntityRef)m_entity;
			m_editor.getUniverse()->destroyEntity(e);
		}


		bool merge(IEditorCommand&) override { return false; }
		const char* getType() override { return "add_entity"; }
		EntityPtr getEntity() const { return m_entity; }


	private:
		WorldEditorImpl& m_editor;
		EntityPtr m_entity;
		DVec3 m_position;
	};

public:
	IAllocator& getAllocator() override { return m_allocator; }

	UniverseView& getView() override { return m_view; }


	Universe* getUniverse() override
	{
		return m_universe; 
	}


	Engine& getEngine() override { return m_engine; }


	void showGizmos()
	{
		if (m_selected_entities.empty()) return;

		Universe* universe = getUniverse();

		if (m_selected_entities.size() > 1)
		{
			AABB aabb = m_render_interface->getEntityAABB(*universe, m_selected_entities[0], m_view.m_viewport.pos);
			for (int i = 1; i < m_selected_entities.size(); ++i)
			{
				AABB entity_aabb = m_render_interface->getEntityAABB(*universe, m_selected_entities[i], m_view.m_viewport.pos);
				aabb.merge(entity_aabb);
			}

			m_render_interface->addDebugCube(m_view.m_viewport.pos + aabb.min, m_view.m_viewport.pos + aabb.max, 0xffffff00);
			return;
		}

		for (ComponentUID cmp = universe->getFirstComponent(m_selected_entities[0]);
			cmp.isValid();
			cmp = universe->getNextComponent(cmp))
		{
			for (auto* plugin : m_plugins)
			{
				if (plugin->showGizmo(cmp)) break;
			}
		}
	}


	void createEditorLines()
	{
		PROFILE_FUNCTION();
		showGizmos();
		m_measure_tool->createEditorLines(*m_render_interface);
	}


	void update() override
	{
		PROFILE_FUNCTION();

		// TODO do not allow user interaction (e.g. saving universe) while queue is not empty
		while (!m_command_queue.empty()) {
			if (!m_command_queue[0]->isReady()) break;

			IEditorCommand* cmd = m_command_queue[0];
			m_command_queue.erase(0);
			doExecute(cmd);
		}

		m_view.update();
		m_prefab_system->update();

		if (!m_selected_entities.empty())
		{
			m_gizmo->add(m_selected_entities[0]);
		}


		createEditorLines();
		m_gizmo->update(m_view.m_viewport);
	}


	~WorldEditorImpl()
	{
		destroyUniverse();

		Gizmo::destroy(*m_gizmo);
		m_gizmo = nullptr;

		removePlugin(*m_measure_tool);
		LUMIX_DELETE(m_allocator, m_measure_tool);
		ASSERT(m_plugins.empty());

		EditorIcons::destroy(*m_editor_icons);
		PrefabSystem::destroy(m_prefab_system);

		LUMIX_DELETE(m_allocator, m_render_interface);
	}


	void snapEntities(const DVec3& hit_pos) override
	{
		Array<DVec3> positions(m_allocator);
		Array<Quat> rotations(m_allocator);
		if(m_gizmo->isTranslateMode())
		{
			for(auto e : m_selected_entities)
			{
				positions.push(hit_pos);
				rotations.push(m_universe->getRotation(e));
			}
		}
		else
		{
			for(auto e : m_selected_entities)
			{
				const DVec3 pos = m_universe->getPosition(e);
				Vec3 dir = (pos - hit_pos).toFloat();
				dir.normalize();
				Matrix mtx = Matrix::IDENTITY;
				Vec3 y(0, 1, 0);
				if(dotProduct(y, dir) > 0.99f)
				{
					y.set(1, 0, 0);
				}
				Vec3 x = crossProduct(y, dir);
				x.normalize();
				y = crossProduct(dir, x);
				y.normalize();
				mtx.setXVector(x);
				mtx.setYVector(y);
				mtx.setZVector(dir);

				positions.push(pos);
				rotations.emplace(mtx.getRotation());
			}
		}
		MoveEntityCommand* cmd = LUMIX_NEW(m_allocator, MoveEntityCommand)(*this,
			&m_selected_entities[0],
			&positions[0],
			&rotations[0],
			positions.size(),
			m_allocator);
		executeCommand(cmd);
	}


	DVec3 getClosestVertex(const RayHit& hit)
	{
		ASSERT(hit.is_hit);
		return m_render_interface->getClosestVertex(m_universe, (EntityRef)hit.entity, hit.pos);
	}


	void addPlugin(Plugin& plugin) override { m_plugins.push(&plugin); }


	void removePlugin(Plugin& plugin) override
	{
		m_plugins.swapAndPopItem(&plugin);
	}


	bool isUniverseChanged() const override { return m_is_universe_changed; }


	void saveUniverse(const char* basename, bool save_path) override
	{
		logInfo("Editor") << "Saving universe " << basename << "...";
		
		StaticString<MAX_PATH_LENGTH> dir(m_engine.getFileSystem().getBasePath(), "universes/", basename);
		OS::makePath(dir);
		StaticString<MAX_PATH_LENGTH> path(dir, "/entities.unv");
		OS::OutputFile file;
		if (file.open(path)) {
			save(file);
			file.close();
		}
		else {
			logError("Editor") << "Failed to save universe " << basename;
		}
		
		m_is_universe_changed = false;

		if (save_path) m_universe->setName(basename);
	}


	void save(IOutputStream& file)
	{
		while (m_engine.getFileSystem().hasWork()) m_engine.getFileSystem().processCallbacks();

		ASSERT(m_universe);

		OutputMemoryStream blob(m_allocator);
		blob.reserve(64 * 1024);

		Header header = {0xffffFFFF, (int)SerializedVersion::LATEST, 0, 0};
		blob.write(header);
		int hashed_offset = sizeof(header);

		header.engine_hash = m_engine.serialize(*m_universe, blob);
		m_prefab_system->serialize(blob);
		header.hash = crc32((const u8*)blob.getData() + hashed_offset, (int)blob.getPos() - hashed_offset);
		*(Header*)blob.getData() = header;
		file.write(blob.getData(), blob.getPos());

		logInfo("editor") << "Universe saved";
	}


	void setRenderInterface(struct RenderInterface* interface) override
	{
		m_render_interface = interface;
		m_editor_icons->setRenderInterface(m_render_interface);
		createUniverse();
	}


	RenderInterface* getRenderInterface() override
	{
		return m_render_interface;
	}


	void snapDown() override
	{
		if (m_selected_entities.empty()) return;

		Array<DVec3> new_positions(m_allocator);
		Universe* universe = getUniverse();

		for (int i = 0; i < m_selected_entities.size(); ++i)
		{
			EntityRef entity = m_selected_entities[i];

			DVec3 origin = universe->getPosition(entity);
			auto hit = m_render_interface->castRay(origin, Vec3(0, -1, 0), m_selected_entities[i]);
			if (hit.is_hit)
			{
				new_positions.push(origin + Vec3(0, -hit.t, 0));
			}
			else
			{
				hit = m_render_interface->castRay(origin, Vec3(0, 1, 0), m_selected_entities[i]);
				if (hit.is_hit)
				{
					new_positions.push(origin + Vec3(0, hit.t, 0));
				}
				else
				{
					new_positions.push(universe->getPosition(m_selected_entities[i]));
				}
			}
		}
		setEntitiesPositions(&m_selected_entities[0], &new_positions[0], new_positions.size());
	}


	void makeParent(EntityPtr parent, EntityRef child) override
	{
		MakeParentCommand* command = LUMIX_NEW(m_allocator, MakeParentCommand)(*this, parent, child);
		executeCommand(command);
	}


	void destroyEntities(const EntityRef* entities, int count) override
	{
		DestroyEntitiesCommand* command = LUMIX_NEW(m_allocator, DestroyEntitiesCommand)(*this, entities, count);
		executeCommand(command);
	}


	EntityRef addEntity() override
	{
		return addEntityAt(m_view.m_viewport.w >> 1, m_view.m_viewport.h >> 1);
	}


	EntityRef addEntityAt(int camera_x, int camera_y) override
	{
		DVec3 origin;
		Vec3 dir;

		m_view.m_viewport.getRay({(float)camera_x, (float)camera_y}, origin, dir);
		auto hit = m_render_interface->castRay(origin, dir, INVALID_ENTITY);
		DVec3 pos;
		if (hit.is_hit) {
			pos = origin + dir * hit.t;
		}
		else {
			pos = m_view.m_viewport.pos + m_view.m_viewport.rot.rotate(Vec3(0, 0, -2));
		}
		AddEntityCommand* command = LUMIX_NEW(m_allocator, AddEntityCommand)(*this, pos);
		executeCommand(command);

		return (EntityRef)command->getEntity();
	}


	DVec3 getCameraRaycastHit() override
	{
		const Vec2 center(float(m_view.m_viewport.w >> 1), float(m_view.m_viewport.h >> 1));

		DVec3 origin;
		Vec3 dir;
		m_view.m_viewport.getRay(center, origin, dir);
		auto hit = m_render_interface->castRay(origin, dir, INVALID_ENTITY);
		DVec3 pos;
		if (hit.is_hit) {
			pos = origin + dir * hit.t;
		}
		else {
			pos = m_view.m_viewport.pos + m_view.m_viewport.rot.rotate(Vec3(0, 0, -2));
		}
		return pos;
	}


	void setEntitiesScales(const EntityRef* entities, const float* scales, int count) override
	{
		if (count <= 0) return;

		IEditorCommand* command =
			LUMIX_NEW(m_allocator, ScaleEntityCommand)(*this, entities, scales, count, m_allocator);
		executeCommand(command);
	}


	void setEntitiesScale(const EntityRef* entities, int count, float scale) override
	{
		if (count <= 0) return;

		IEditorCommand* command =
			LUMIX_NEW(m_allocator, ScaleEntityCommand)(*this, entities, count, scale, m_allocator);
		executeCommand(command);
	}


	void setEntitiesRotations(const EntityRef* entities, const Quat* rotations, int count) override
	{
		ASSERT(entities && rotations);
		if (count <= 0) return;

		Universe* universe = getUniverse();
		Array<DVec3> positions(m_allocator);
		for (int i = 0; i < count; ++i)
		{
			positions.push(universe->getPosition(entities[i]));
		}
		IEditorCommand* command =
			LUMIX_NEW(m_allocator, MoveEntityCommand)(*this, entities, &positions[0], rotations, count, m_allocator);
		executeCommand(command);
	}


	void setEntitiesCoordinate(const EntityRef* entities, int count, double value, Coordinate coord) override
	{
		ASSERT(entities);
		if (count <= 0) return;

		Universe* universe = getUniverse();
		Array<Quat> rots(m_allocator);
		Array<DVec3> poss(m_allocator);
		rots.reserve(count);
		poss.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			rots.push(universe->getRotation(entities[i]));
			poss.push(universe->getPosition(entities[i]));
			(&poss[i].x)[(int)coord] = value;
		}
		IEditorCommand* command =
			LUMIX_NEW(m_allocator, MoveEntityCommand)(*this, entities, &poss[0], &rots[0], count, m_allocator);
		executeCommand(command);
	}


	void setEntitiesLocalCoordinate(const EntityRef* entities, int count, double value, Coordinate coord) override
	{
		ASSERT(entities);
		if (count <= 0) return;

		Universe* universe = getUniverse();
		Array<DVec3> poss(m_allocator);
		poss.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			poss.push(universe->getLocalTransform(entities[i]).pos);
			(&poss[i].x)[(int)coord] = value;
		}
		IEditorCommand* command =
			LUMIX_NEW(m_allocator, LocalMoveEntityCommand)(*this, entities, &poss[0], count, m_allocator);
		executeCommand(command);
	}


	void setEntitiesPositions(const EntityRef* entities, const DVec3* positions, int count) override
	{
		ASSERT(entities && positions);
		if (count <= 0) return;

		Universe* universe = getUniverse();
		Array<Quat> rots(m_allocator);
		for (int i = 0; i < count; ++i)
		{
			rots.push(universe->getRotation(entities[i]));
		}
		IEditorCommand* command =
			LUMIX_NEW(m_allocator, MoveEntityCommand)(*this, entities, positions, &rots[0], count, m_allocator);
		executeCommand(command);
	}

	void setEntitiesPositionsAndRotations(const EntityRef* entities,
		const DVec3* positions,
		const Quat* rotations,
		int count) override
	{
		if (count <= 0) return;
		IEditorCommand* command =
			LUMIX_NEW(m_allocator, MoveEntityCommand)(*this, entities, positions, rotations, count, m_allocator);
		executeCommand(command);
	}


	void setEntityName(EntityRef entity, const char* name) override
	{
		IEditorCommand* command = LUMIX_NEW(m_allocator, SetEntityNameCommand)(*this, entity, name);
		executeCommand(command);
	}


	void beginCommandGroup(u32 type) override
	{
		if(m_undo_index < m_undo_stack.size() - 1)
		{
			for(int i = m_undo_stack.size() - 1; i > m_undo_index; --i)
			{
				LUMIX_DELETE(m_allocator, m_undo_stack[i]);
			}
			m_undo_stack.resize(m_undo_index + 1);
		}

		if(m_undo_index >= 0)
		{
			static const u32 end_group_hash = crc32("end_group");
			if(crc32(m_undo_stack[m_undo_index]->getType()) == end_group_hash)
			{
				if(static_cast<EndGroupCommand*>(m_undo_stack[m_undo_index])->group_type == type)
				{
					LUMIX_DELETE(m_allocator, m_undo_stack[m_undo_index]);
					--m_undo_index;
					m_undo_stack.pop();
					return;
				}
			}
		}

		m_current_group_type = type;
		auto* cmd = LUMIX_NEW(m_allocator, BeginGroupCommand);
		m_undo_stack.push(cmd);
		++m_undo_index;
	}


	void endCommandGroup() override
	{
		if (m_undo_index < m_undo_stack.size() - 1)
		{
			for (int i = m_undo_stack.size() - 1; i > m_undo_index; --i)
			{
				LUMIX_DELETE(m_allocator, m_undo_stack[i]);
			}
			m_undo_stack.resize(m_undo_index + 1);
		}

		auto* cmd = LUMIX_NEW(m_allocator, EndGroupCommand);
		cmd->group_type = m_current_group_type;
		m_undo_stack.push(cmd);
		++m_undo_index;
	}

	void registerCommand(const char* name, CommandCreator* creator) override {
		lua_State* L = m_engine.getState();
		LuaWrapper::DebugGuard guard(L);
		lua_getfield(L, LUA_GLOBALSINDEX, "Editor");
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			lua_newtable(L);
			lua_pushvalue(L, -1);
			lua_setfield(L, LUA_GLOBALSINDEX, "Editor");
		}

		lua_getfield(L, -1, name);
		if (!lua_isnil(L, -1)) {
			lua_pop(L, 1);
			logError("Editor") << "Command " << name << " already exists.";
			return;
		}
		lua_pop(L, 1);

		auto f = [](lua_State* L) -> int {
			auto* creator = LuaWrapper::toType<CommandCreator*>(L, lua_upvalueindex(1));
			auto* editor = LuaWrapper::toType<WorldEditor*>(L, lua_upvalueindex(2));
			IEditorCommand* cmd = creator(L, *editor);
			editor->executeCommand(cmd);
			return 0;
		};

		lua_pushlightuserdata(L, reinterpret_cast<void*>(creator));
		lua_pushlightuserdata(L, this);
		lua_pushcclosure(L, f, 2);
		lua_setfield(L, -2, name);
		lua_pop(L, 1);
	}

	void executeCommand(const char* name, const char* args) override {
		lua_State* L = m_engine.getState();
		StaticString<1024> tmp("Editor.", name, "(", args, ")");
		LuaWrapper::execute(L, Span(tmp.data, stringLength(tmp.data)), "executeCommand", 0);
	}

	void executeCommand(IEditorCommand* command) override
	{
		if (!m_command_queue.empty() || !command->isReady()) {
			m_command_queue.push(command);
			return;
		}

		doExecute(command);
	}


	void doExecute(IEditorCommand* command)
	{
		ASSERT(command->isReady());
		
		m_is_universe_changed = true;
		if (m_undo_index >= 0 && command->getType() == m_undo_stack[m_undo_index]->getType())
		{
			if (command->merge(*m_undo_stack[m_undo_index]))
			{
				m_undo_stack[m_undo_index]->execute();
				LUMIX_DELETE(m_allocator, command);
				return;
			}
		}

		if (command->execute())
		{
			if (m_undo_index < m_undo_stack.size() - 1)
			{
				for (int i = m_undo_stack.size() - 1; i > m_undo_index; --i)
				{
					LUMIX_DELETE(m_allocator, m_undo_stack[i]);
				}
				m_undo_stack.resize(m_undo_index + 1);
			}
			m_undo_stack.push(command);
			if (m_is_game_mode) ++m_game_mode_commands;
			++m_undo_index;
			return;
		}
		else {
			logError("Editor") << "Editor command failed";
		}
		LUMIX_DELETE(m_allocator, command);	
	}


	bool isGameMode() const override { return m_is_game_mode; }


	void toggleGameMode() override
	{
		ASSERT(m_universe);
		if (m_is_game_mode) {
			stopGameMode(true);
			return;
		}

		m_selected_entity_on_game_mode = m_selected_entities.empty() ? INVALID_ENTITY : m_selected_entities[0];
		m_game_mode_file.clear();
		save(m_game_mode_file);
		m_is_game_mode = true;
		beginCommandGroup(0);
		endCommandGroup();
		m_game_mode_commands = 2;
		m_engine.startGame(*m_universe);
	}


	void stopGameMode(bool reload)
	{
		for (int i = 0; i < m_game_mode_commands; ++i)
		{
			LUMIX_DELETE(m_allocator, m_undo_stack.back());
			m_undo_stack.pop();
			--m_undo_index;
		}

		ASSERT(m_universe);
		m_engine.getResourceManager().enableUnload(false);
		m_engine.stopGame(*m_universe);
		selectEntities(nullptr, 0, false);
		m_gizmo->clearEntities();
		m_editor_icons->clear();
		m_is_game_mode = false;
		if (reload)
		{
			m_universe_destroyed.invoke();
			StaticString<64> name(m_universe->getName());
			m_engine.destroyUniverse(*m_universe);
			
			m_universe = &m_engine.createUniverse(true);
			m_universe_created.invoke();
			m_universe->setName(name);
			m_universe->entityDestroyed().bind<&WorldEditorImpl::onEntityDestroyed>(this);
			m_selected_entities.clear();
            InputMemoryStream file(m_game_mode_file);
			load(file);
		}
		m_game_mode_file.clear();
		if(m_selected_entity_on_game_mode.isValid()) {
			EntityRef e = (EntityRef)m_selected_entity_on_game_mode;
			selectEntities(&e, 1, false);
		}
		m_engine.getResourceManager().enableUnload(true);
	}


	PrefabSystem& getPrefabSystem() override
	{
		return *m_prefab_system;
	}

	void copyEntities(const EntityRef* entities, int count, OutputMemoryStream& serializer)
	{
		serializer.write(count);
		for (int i = 0; i < count; ++i) {
			m_copy_buffer.write(entities[i]);
		}
		for (int i = 0; i < count; ++i) {
			EntityRef entity = entities[i];
			Transform tr = m_universe->getTransform(entity);
			serializer.write(tr);
			serializer.write(m_universe->getParent(entity));

			for (ComponentUID cmp = m_universe->getFirstComponent(entity);
				cmp.isValid();
				cmp = m_universe->getNextComponent(cmp))
			{
				const u32 cmp_type = Reflection::getComponentTypeHash(cmp.type);
				serializer.write(cmp_type);
				
				Reflection::serializeComponent(*cmp.scene, (EntityRef)cmp.entity, cmp.type, Ref(serializer));
			}
			serializer.write((u32)0);
		}
	}

	void copyEntities() override
	{
		if (m_selected_entities.empty()) return;

		m_copy_buffer.clear();

		Array<EntityRef> entities(m_allocator);
		entities = m_selected_entities;
		for (EntityRef e : entities) {
			for (EntityPtr child = m_universe->getFirstChild(e); 
				child.isValid(); 
				child = m_universe->getNextSibling((EntityRef)child)) 
			{
				if(entities.indexOf((EntityRef)child) < 0) entities.push((EntityRef)child);
			}
		}
		copyEntities(&entities[0], entities.size(), m_copy_buffer);
	}


	bool canPasteEntities() const override
	{
		return m_copy_buffer.getPos() > 0;
	}


	void pasteEntities() override;
	void duplicateEntities() override;


	void destroyComponent(Span<const EntityRef> entities, ComponentType cmp_type) override
	{
		ASSERT(entities.length() > 0);
		IEditorCommand* command = LUMIX_NEW(m_allocator, DestroyComponentCommand)(*this, entities, cmp_type);
		executeCommand(command);
	}


	void addComponent(Span<const EntityRef> entities, ComponentType cmp_type) override
	{
		ASSERT(entities.length() > 0);
		IEditorCommand* command = LUMIX_NEW(m_allocator, AddComponentCommand)(*this, entities, cmp_type);
		executeCommand(command);
	}

	void loadUniverse(const char* basename) override
	{
		if (m_is_game_mode) stopGameMode(false);
		destroyUniverse();
		createUniverse();
		m_universe->setName(basename);
		logInfo("Editor") << "Loading universe " << basename << "...";
		OS::InputFile file;
		const StaticString<MAX_PATH_LENGTH> path(m_engine.getFileSystem().getBasePath(), "universes/", basename, "/entities.unv");
		if (file.open(path)) {
			if (!load(file)) {
				logError("Editor") << "Failed to parse " << path;
				newUniverse();
			}
			file.close();
		}
		else {
			logError("Editor") << "Failed to open " << path;
			newUniverse();
		}
		m_editor_icons->refresh();
	}


	void newUniverse() override
	{
		destroyUniverse();
		createUniverse();
		logInfo("Editor") << "Universe created.";
	}


	enum class SerializedVersion : int
	{
		LATEST
	};


	#pragma pack(1)
		struct Header
		{
			u32 magic;
			int version;
			u32 hash;
			u32 engine_hash;
		};
	#pragma pack()


	bool load(IInputStream& file)
	{
		m_is_loading = true;
		Header header;
		const u64 file_size = file.size();
		if (file_size < sizeof(header)) {
			logError("Editor") << "Corrupted file.";
			m_is_loading = false;
			return false;
		}
		if (file_size > 0xffFFffFF) {
			logError("Editor") << "File too big.";
			m_is_loading = false;
			return false;
		}

		OS::Timer timer;
		logInfo("Editor") << "Parsing universe...";
		Array<u8> data(m_allocator);
		if (!file.getBuffer()) {
			data.resize((u32)file_size);
			if (!file.read(data.begin(), data.byte_size())) {
				logError("Editor") << "Failed to load file.";
				m_is_loading = false;
				return false;
			}
		}
		InputMemoryStream blob(file.getBuffer() ? file.getBuffer() : data.begin(), (int)file_size);
		u32 hash = 0;
		blob.read(hash);
		header.version = -1;
		int hashed_offset = sizeof(hash);
		if (hash == 0xFFFFffff)
		{
			blob.rewind();
			blob.read(header);
			hashed_offset = sizeof(header);
			hash = header.hash;
		}
		else
		{
			u32 engine_hash = 0;
			blob.read(engine_hash);
		}
		if (crc32((const u8*)blob.getData() + hashed_offset, (int)blob.size() - hashed_offset) != hash)
		{
			logError("Editor") << "Corrupted file.";
			m_is_loading = false;
			return false;
		}

		EntityMap entity_map(m_allocator);
		if (m_engine.deserialize(*m_universe, blob, Ref(entity_map)))
		{
			m_prefab_system->deserialize(blob, entity_map);
			logInfo("Editor") << "Universe parsed in " << timer.getTimeSinceStart() << " seconds";
			m_is_loading = false;
			return true;
		}

		newUniverse();
		m_is_loading = false;
		return false;
	}


	template <typename T>
	static IEditorCommand* constructEditorCommand(WorldEditor& editor)
	{
		return LUMIX_NEW(editor.getAllocator(), T)(editor);
	}


	Gizmo& getGizmo() override { return *m_gizmo; }


	EditorIcons& getIcons() override
	{
		return *m_editor_icons;
	}


	WorldEditorImpl(const char* base_path, Engine& engine, IAllocator& allocator)
		: m_allocator(allocator)
		, m_universe_destroyed(m_allocator)
		, m_universe_created(m_allocator)
		, m_selected_entities(m_allocator)
		, m_editor_icons(nullptr)
		, m_plugins(m_allocator)
		, m_undo_stack(m_allocator)
		, m_copy_buffer(m_allocator)
		, m_is_loading(false)
		, m_universe(nullptr)
		, m_is_toggle_selection(false)
		, m_render_interface(nullptr)
		, m_selected_entity_on_game_mode(INVALID_ENTITY)
		, m_is_game_mode(false)
		, m_undo_index(-1)
		, m_engine(engine)
        , m_game_mode_file(m_allocator)
		, m_command_queue(m_allocator)
		, m_view(*this)
	{
		logInfo("Editor") << "Initializing editor...";
		m_view.m_viewport.is_ortho = false;
		m_view.m_viewport.pos = DVec3(0);
		m_view.m_viewport.rot.set(0, 0, 0, 1);
		m_view.m_viewport.w = -1;
		m_view.m_viewport.h = -1;
		m_view.m_viewport.fov = degreesToRadians(60.f);
		m_view.m_viewport.near = 0.1f;
		m_view.m_viewport.far = 100000.f;

		m_measure_tool = LUMIX_NEW(m_allocator, MeasureTool)();
		addPlugin(*m_measure_tool);

		m_prefab_system = PrefabSystem::create(*this);

		m_gizmo = Gizmo::create(*this);
		m_editor_icons = EditorIcons::create(*this);
	}


	bool isEntitySelected(EntityRef entity) const override
	{
		return m_selected_entities.indexOf(entity) >= 0;
	}


	const Array<EntityRef>& getSelectedEntities() const override
	{
		return m_selected_entities;
	}


	void setToggleSelection(bool is_toggle) override { m_is_toggle_selection = is_toggle; }


	void addArrayPropertyItem(EntityRef entity, ComponentType cmp_type, const char* prop_name, u32 index) override
	{
		IEditorCommand* command = LUMIX_NEW(m_allocator, AddArrayPropertyItemCommand)(*this, entity, cmp_type, prop_name, index);
		executeCommand(command);
	}


	void removeArrayPropertyItem(EntityRef entity, ComponentType cmp_type, const char* prop_name, u32 index) override
	{
		IEditorCommand* command = LUMIX_NEW(m_allocator, RemoveArrayPropertyItemCommand)(*this, entity, cmp_type, prop_name, index);
		executeCommand(command);
	}


	void setProperty(ComponentType component_type,
		const char* prop_name,
		Span<const EntityRef> entities,
		Span<const u8> data) override
	{
		IEditorCommand* command = LUMIX_NEW(m_allocator, SetPropertyCommand)(*this, entities, component_type, prop_name, data);
		executeCommand(command);
	}


	void selectEntities(const EntityRef* entities, int count, bool toggle) override
	{
		if (!toggle || !m_is_toggle_selection)
		{
			m_gizmo->clearEntities();
			m_selected_entities.clear();
			for (int i = 0; i < count; ++i)
			{
				m_selected_entities.push(entities[i]);
			}
		}
		else
		{
			for (int i = 0; i < count; ++i)
			{
				int idx = m_selected_entities.indexOf(entities[i]);
				if (idx < 0)
				{
					m_selected_entities.push(entities[i]);
				}
				else
				{
					m_selected_entities.swapAndPop(idx);
				}
			}
		}

		m_selected_entities.removeDuplicates();
	}


	void onEntityDestroyed(EntityRef entity)
	{
		m_selected_entities.swapAndPopItem(entity);
	}


	void destroyUniverse()
	{
		if (m_is_game_mode) stopGameMode(false);

		ASSERT(m_universe);
		destroyUndoStack();
		m_universe_destroyed.invoke();
		m_editor_icons->clear();
		m_gizmo->clearEntities();
		selectEntities(nullptr, 0, false);
		m_engine.destroyUniverse(*m_universe);
		m_universe = nullptr;
	}


	DelegateList<void()>& universeCreated() override
	{
		return m_universe_created;
	}


	DelegateList<void()>& universeDestroyed() override
	{
		return m_universe_destroyed;
	}


	void destroyUndoStack()
	{
		m_undo_index = -1;
		for (int i = 0; i < m_undo_stack.size(); ++i)
		{
			LUMIX_DELETE(m_allocator, m_undo_stack[i]);
		}
		m_undo_stack.clear();
	}


	void createUniverse()
	{
		ASSERT(!m_universe);

		m_is_universe_changed = false;
		destroyUndoStack();
		m_universe = &m_engine.createUniverse(true);
		Universe* universe = m_universe;

		universe->entityDestroyed().bind<&WorldEditorImpl::onEntityDestroyed>(this);

		m_view.m_is_orbit = false;
		m_selected_entities.clear();
		m_universe_created.invoke();
	}


	bool canUndo() const override
	{
		return !m_is_game_mode && m_undo_index < m_undo_stack.size() && m_undo_index >= 0;
	}


	bool canRedo() const override
	{
		return !m_is_game_mode && m_undo_index + 1 < m_undo_stack.size();
	}


	void undo() override
	{
		if (m_is_game_mode) return;

		static const u32 end_group_hash = crc32("end_group");
		static const u32 begin_group_hash = crc32("begin_group");

		if (m_undo_index >= m_undo_stack.size() || m_undo_index < 0) return;

		if(crc32(m_undo_stack[m_undo_index]->getType()) == end_group_hash)
		{
			--m_undo_index;
			while(crc32(m_undo_stack[m_undo_index]->getType()) != begin_group_hash)
			{
				m_undo_stack[m_undo_index]->undo();
				--m_undo_index;
			}
			--m_undo_index;
		}
		else
		{
			m_undo_stack[m_undo_index]->undo();
			--m_undo_index;
		}
	}


	void redo() override
	{
		if (m_is_game_mode) return;

		static const u32 end_group_hash = crc32("end_group");
		static const u32 begin_group_hash = crc32("begin_group");

		if (m_undo_index + 1 >= m_undo_stack.size()) return;

		++m_undo_index;
		if(crc32(m_undo_stack[m_undo_index]->getType()) == begin_group_hash)
		{
			++m_undo_index;
			while(crc32(m_undo_stack[m_undo_index]->getType()) != end_group_hash)
			{
				m_undo_stack[m_undo_index]->execute();
				++m_undo_index;
			}
		}
		else
		{
			m_undo_stack[m_undo_index]->execute();
		}
	}


	MeasureTool* getMeasureTool() const override
	{
		return m_measure_tool;
	}


	double getMeasuredDistance() const override
	{
		return m_measure_tool->getDistance();
	}


	bool isMeasureToolActive() const override
	{
		return m_measure_tool->isEnabled();
	}


	void toggleMeasure() override
	{
		m_measure_tool->enable(!m_measure_tool->isEnabled());
	}


	static int getEntitiesCount(Universe& universe)
	{
		int count = 0;
		for (EntityPtr e = universe.getFirstEntity(); e.isValid(); e = universe.getNextEntity((EntityRef)e)) ++count;
		return count;
	}

	Span<Plugin*> getPlugins() override { return m_plugins; }

private:
	IAllocator& m_allocator;
	Engine& m_engine;
	RenderInterface* m_render_interface;
	UniverseViewImpl m_view;
	PrefabSystem* m_prefab_system;
	Universe* m_universe;
	bool m_is_loading;
	bool m_is_universe_changed;
	Array<Plugin*> m_plugins;
	
	Array<IEditorCommand*> m_undo_stack;
	Array<IEditorCommand*> m_command_queue;
	int m_undo_index;
	u32 m_current_group_type;

	Array<EntityRef> m_selected_entities;
	EntityPtr m_selected_entity_on_game_mode;

	Gizmo* m_gizmo;
	EditorIcons* m_editor_icons;
	MeasureTool* m_measure_tool;

	bool m_is_toggle_selection;

	bool m_is_game_mode;
	int m_game_mode_commands;
	OutputMemoryStream m_game_mode_file;
	DelegateList<void()> m_universe_destroyed;
	DelegateList<void()> m_universe_created;

	OutputMemoryStream m_copy_buffer;
};


struct PasteEntityCommand final : IEditorCommand
{
public:
	PasteEntityCommand(WorldEditor& editor, const OutputMemoryStream& copy_buffer, bool identity = false)
		: m_copy_buffer(copy_buffer)
		, m_editor(editor)
		, m_position(editor.getCameraRaycastHit())
		, m_entities(editor.getAllocator())
		, m_map(editor.getAllocator())
		, m_identity(identity)
	{
	}


	PasteEntityCommand(WorldEditor& editor, const DVec3& pos, const OutputMemoryStream& copy_buffer, bool identity = false)
		: m_copy_buffer(copy_buffer)
		, m_editor(editor)
		, m_position(pos)
		, m_entities(editor.getAllocator())
		, m_map(editor.getAllocator())
		, m_identity(identity)
	{
	}


	bool execute() override
	{
		InputMemoryStream blob(m_copy_buffer);

		Universe& universe = *m_editor.getUniverse();
		int entity_count;
		blob.read(Ref(entity_count));
		bool is_redo = !m_entities.empty();
		if (is_redo)
		{
			for (int i = 0; i < entity_count; ++i) {
				universe.emplaceEntity(m_entities[i]);
			}
		}
		else {
			m_entities.resize(entity_count);
			for (int i = 0; i < entity_count; ++i) {
				m_entities[i] = universe.createEntity(DVec3(0), Quat(0, 0, 0, 1));
			}
		}

		Transform base_tr;
		base_tr.pos = m_position;
		base_tr.scale = 1;
		base_tr.rot = Quat(0, 0, 0, 1);
		m_map.reserve(entity_count);
		if (!is_redo) {
			for (int i = 0; i < entity_count; ++i) {
				EntityRef orig_e;
				blob.read(Ref(orig_e));
				m_map.insert(orig_e, i);
			}
		}
		for (int i = 0; i < entity_count; ++i)
		{
			Transform tr;
			blob.read(Ref(tr));
			EntityPtr parent;
			blob.read(Ref(parent));

			auto iter = m_map.find(parent);
			if (iter.isValid()) parent = m_entities[iter.value()];

			if (!m_identity)
			{
				if (i == 0)
				{
					const Transform inv = tr.inverted();
					base_tr.rot = tr.rot;
					base_tr.scale = tr.scale;
					base_tr = base_tr * inv;
					tr.pos = m_position;
				}
				else
				{
					tr = base_tr * tr;
				}
			}

			const EntityRef new_entity = m_entities[i];
			universe.setTransform(new_entity, tr);
			universe.setParent(parent, new_entity);
			for (;;) {
				u32 hash;
				blob.read(Ref(hash));
				if (hash == 0) break;

				ComponentUID cmp;
				cmp.entity = new_entity;
				cmp.type = Reflection::getComponentTypeFromHash(hash);
				cmp.scene = universe.getScene(cmp.type);

				cmp.scene->getUniverse().createComponent(cmp.type, new_entity);

				Reflection::deserializeComponent(*cmp.scene, new_entity, cmp.type, m_map, m_entities, Ref(blob));
			}
		}
		return true;
	}


	void undo() override
	{
		for (auto entity : m_entities) {
			m_editor.getUniverse()->destroyEntity(entity);
		}
	}


	const char* getType() override { return "paste_entity"; }


	bool merge(IEditorCommand& command) override
	{
		ASSERT(command.getType() == getType());
		return false;
	}


	const Array<EntityRef>& getEntities() { return m_entities; }


private:
	OutputMemoryStream m_copy_buffer;
	WorldEditor& m_editor;
	DVec3 m_position;
	Array<EntityRef> m_entities;
	HashMap<EntityPtr, u32> m_map;
	bool m_identity;
};


void WorldEditorImpl::pasteEntities()
{
	if (!canPasteEntities()) return;
	PasteEntityCommand* command = LUMIX_NEW(m_allocator, PasteEntityCommand)(*this, m_copy_buffer);
	executeCommand(command);
}


void WorldEditorImpl::duplicateEntities()
{
	copyEntities();

	PasteEntityCommand* command = LUMIX_NEW(m_allocator, PasteEntityCommand)(*this, m_copy_buffer, true);
	executeCommand(command);
}


WorldEditor* WorldEditor::create(const char* base_path, Engine& engine, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, WorldEditorImpl)(base_path, engine, allocator);
}


void WorldEditor::destroy(WorldEditor* editor, IAllocator& allocator)
{
	LUMIX_DELETE(allocator, static_cast<WorldEditorImpl*>(editor));
}


} // namespace Lumix
