#include "world_editor.h"

#include "editor/gizmo.h"
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
#include "engine/math.h"
#include "engine/metaprogramming.h"
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


static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
static const ComponentType CAMERA_TYPE = reflection::getComponentType("camera");

static u32 ARGBToABGR(u32 color)
{
	return ((color & 0xff) << 16) | (color & 0xff00) | ((color & 0xff0000) >> 16) | (color & 0xff000000);
}

void addCube(UniverseView& view, const DVec3& pos, const Vec3& right, const Vec3& up, const Vec3& dir, Color color) {
	UniverseView::Vertex* vertices = view.render(true, 24);
	const DVec3& cam_pos = view.getViewport().pos;

	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = (a - cam_pos).toFloat();
		vertices[1].pos = (b - cam_pos).toFloat();
		vertices[0].abgr = color.abgr();
		vertices[1].abgr = color.abgr();
		vertices += 2;
	};

	add_line(pos + dir + up + right, pos + dir + up - right);
	add_line(pos - dir + up + right, pos - dir + up - right);
	add_line(pos + dir + up + right, pos - dir + up + right);
	add_line(pos + dir + up - right, pos - dir + up - right);

	add_line(pos + dir - up + right, pos + dir - up - right);
	add_line(pos - dir - up + right, pos - dir - up - right);
	add_line(pos + dir - up + right, pos - dir - up + right);
	add_line(pos + dir - up - right, pos - dir - up - right);

	add_line(pos + dir + up + right, pos + dir - up + right);
	add_line(pos + dir + up - right, pos + dir - up - right);
	add_line(pos - dir + up + right, pos - dir - up + right);
	add_line(pos - dir + up - right, pos - dir - up - right);
}

void addCube(UniverseView& view, const DVec3& min, const DVec3& max, Color color) {
	DVec3 a = min;
	DVec3 b = min;
	b.x = max.x;
	
	UniverseView::Vertex* vertices = view.render(true, 24);
	const DVec3 cam_pos = view.getViewport().pos;

	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = (a - cam_pos).toFloat();
		vertices[1].pos = (b - cam_pos).toFloat();
		vertices[0].abgr = color.abgr();
		vertices[1].abgr = color.abgr();
		vertices += 2;
	};

	add_line(a, b);
	a = DVec3(b.x, b.y, max.z);
	add_line(a, b);
	b = DVec3(min.x, a.y, a.z);
	add_line(a, b);
	a = DVec3(b.x, b.y, min.z);
	add_line(a, b);

	a = min;
	a.y = max.y;
	b = a;
	b.x = max.x;
	add_line(a, b);
	a = DVec3(b.x, b.y, max.z);
	add_line(a, b);
	b = DVec3(min.x, a.y, a.z);
	add_line(a, b);
	a = DVec3(b.x, b.y, min.z);
	add_line(a, b);

	a = min;
	b = a;
	b.y = max.y;
	add_line(a, b);
	a.x = max.x;
	b.x = max.x;
	add_line(a, b);
	a.z = max.z;
	b.z = max.z;
	add_line(a, b);
	a.x = min.x;
	b.x = min.x;
	add_line(a, b);
}

void addCylinder(UniverseView& view, const DVec3& pos, const Vec3& up, float radius, float height, Color color) {
	Vec3 x_vec(0, up.z, -up.y);
	if (x_vec.squaredLength() < 0.01) {
		x_vec = Vec3(up.y, -up.x, 0);
	}
	x_vec.normalize();
	const Vec3 z_vec = crossProduct(x_vec, up).normalized();

	const DVec3 top = pos + up * height;
	UniverseView::Vertex* vertices = view.render(true, 32 * 6);
	const DVec3 cam_pos = view.getViewport().pos;

	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = (a - cam_pos).toFloat();
		vertices[1].pos = (b - cam_pos).toFloat();
		vertices[0].abgr = color.abgr();
		vertices[1].abgr = color.abgr();
		vertices += 2;
	};

	for (int i = 0; i < 32; ++i) {
		const float a = i / 32.0f * 2 * PI;
		const float x = cosf(a) * radius;
		const float z = sinf(a) * radius;
		add_line(pos + x_vec * x + z_vec * z, top + x_vec * x + z_vec * z);

		const float a_next = (i + 1) / 32.0f * 2 * PI;
		const float x_next = cosf(a_next) * radius;
		const float z_next = sinf(a_next) * radius;

		add_line(pos + x_vec * x + z_vec * z, pos + x_vec * x_next + z_vec * z_next);
		add_line(top + x_vec * x + z_vec * z, top + x_vec * x_next + z_vec * z_next);
	}
}

void addLine(UniverseView& view, const DVec3& a, const DVec3& b, Color color) {
	UniverseView::Vertex* vertices = view.render(true, 2);
	const DVec3 cam_pos = view.getViewport().pos;
	vertices[0].pos = (a - cam_pos).toFloat();
	vertices[1].pos = (b - cam_pos).toFloat();
	vertices[0].abgr = color.abgr();
	vertices[1].abgr = color.abgr();
}

void addCone(UniverseView& view, const DVec3& vertex, const Vec3& dir, const Vec3& axis0, const Vec3& axis1, Color color) {
	UniverseView::Vertex* vertices = view.render(true, 32 * 4);
	const DVec3 cam_pos = view.getViewport().pos;
	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = (a - cam_pos).toFloat();
		vertices[1].pos = (b - cam_pos).toFloat();
		vertices[0].abgr = color.abgr();
		vertices[1].abgr = color.abgr();
		vertices += 2;
	};

	const DVec3 base_center = vertex + dir;
	DVec3 prev_p = base_center + axis0;
	for (int i = 1; i <= 32; ++i)
	{
		float angle = i / 32.0f * 2 * PI;
		const Vec3 x = cosf(angle) * axis0;
		const Vec3 z = sinf(angle) * axis1;
		const DVec3 p = base_center + x + z;
		add_line(p, prev_p);
		add_line(vertex, p);
		prev_p = p;
	}
}

void addHalfSphere(UniverseView& view, const DVec3& center, float radius, bool top, Color color)
{
	static const int COLS = 36;
	static const int ROWS = COLS >> 1;
	static const float STEP = (PI / 180.0f) * 360.0f / COLS;
	int p2 = COLS >> 1;
	int yfrom = top ? 0 : -(ROWS >> 1);
	int yto = top ? ROWS >> 1 : 0;

	UniverseView::Vertex* vertices = view.render(true, (yto - yfrom) * 2 * p2 * 6);
	const DVec3 cam_pos = view.getViewport().pos;

	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = (a - cam_pos).toFloat();
		vertices[1].pos = (b - cam_pos).toFloat();
		vertices[0].abgr = color.abgr();
		vertices[1].abgr = color.abgr();
		vertices += 2;
	};

	for (int y = yfrom; y < yto; ++y)
	{
		float cy = cosf(y * STEP);
		float cy1 = cosf((y + 1) * STEP);
		float sy = sinf(y * STEP);
		float sy1 = sinf((y + 1) * STEP);
		float prev_ci = cosf((-p2 - 1) * STEP);
		float prev_si = sinf((-p2 - 1) * STEP);

		for (int i = -p2; i < p2; ++i)
		{
			float ci = cosf(i * STEP);
			float si = sinf(i * STEP);
			add_line(DVec3(center.x + radius * ci * cy,
				center.y + radius * sy,
				center.z + radius * si * cy),
				DVec3(center.x + radius * ci * cy1,
				center.y + radius * sy1,
				center.z + radius * si * cy1));
			add_line(DVec3(center.x + radius * ci * cy,
				center.y + radius * sy,
				center.z + radius * si * cy),
				DVec3(center.x + radius * prev_ci * cy,
				center.y + radius * sy,
				center.z + radius * prev_si * cy));
			add_line(DVec3(center.x + radius * prev_ci * cy1,
				center.y + radius * sy1,
				center.z + radius * prev_si * cy1),
				DVec3(center.x + radius * ci * cy1,
				center.y + radius * sy1,
				center.z + radius * si * cy1));
			prev_ci = ci;
			prev_si = si;
		}
	}
}

void addCapsule(UniverseView& view, const DVec3& position, float height, float radius, Color color) {
	addHalfSphere(view, position + Vec3(0, radius, 0), radius, false, color);
	addHalfSphere(view, position + Vec3(0, radius + height, 0), radius, true, color);

	Vec3 z_vec(0, 0, 1.0f);
	Vec3 x_vec(1.0f, 0, 0);
	z_vec.normalize();
	x_vec.normalize();
	const DVec3 bottom = position + Vec3(0, radius, 0);
	const DVec3 top = bottom + Vec3(0, height, 0);
	UniverseView::Vertex* vertices = view.render(true, 32 * 2);
	const DVec3 cam_pos = view.getViewport().pos;

	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = (a - cam_pos).toFloat();
		vertices[1].pos = (b - cam_pos).toFloat();
		vertices[0].abgr = color.abgr();
		vertices[1].abgr = color.abgr();
		vertices += 2;
	};

	for (int i = 1; i <= 32; ++i) {
		const float a = i / 32.0f * 2 * PI;
		const float x = cosf(a) * radius;
		const float z = sinf(a) * radius;
		add_line(bottom + x_vec * x + z_vec * z, top + x_vec * x + z_vec * z);
	}
}

void addFrustum(UniverseView& view, const struct ShiftedFrustum& frustum, Color color) {
	const DVec3 o = frustum.origin;

	UniverseView::Vertex* vertices = view.render(true, 24);
	const DVec3 cam_pos = view.getViewport().pos;

	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = (a - cam_pos).toFloat();
		vertices[1].pos = (b - cam_pos).toFloat();
		vertices[0].abgr = color.abgr();
		vertices[1].abgr = color.abgr();
		vertices += 2;
	};
	
	add_line(o + frustum.points[0], o + frustum.points[1]);
	add_line(o + frustum.points[1], o + frustum.points[2]);
	add_line(o + frustum.points[2], o + frustum.points[3]);
	add_line(o + frustum.points[3], o + frustum.points[0]);

	add_line(o + frustum.points[4], o + frustum.points[5]);
	add_line(o + frustum.points[5], o + frustum.points[6]);
	add_line(o + frustum.points[6], o + frustum.points[7]);
	add_line(o + frustum.points[7], o + frustum.points[4]);

	add_line(o + frustum.points[0], o + frustum.points[4]);
	add_line(o + frustum.points[1], o + frustum.points[5]);
	add_line(o + frustum.points[2], o + frustum.points[6]);
	add_line(o + frustum.points[3], o + frustum.points[7]);
}

void addSphere(UniverseView& view, const DVec3& center, float radius, Color color) {
	static const int COLS = 36;
	static const int ROWS = COLS >> 1;
	static const float STEP = (PI / 180.0f) * 360.0f / COLS;
	int p2 = COLS >> 1;
	int r2 = ROWS >> 1;
	float prev_ci = 1;
	float prev_si = 0;

	const u32 count = 2 * r2 * 2 * p2;
	UniverseView::Vertex* vertices = view.render(true, count * 6);
	const DVec3& cam_pos = view.getViewport().pos;
	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = (a - cam_pos).toFloat();
		vertices[1].pos = (b - cam_pos).toFloat();
		vertices[0].abgr = color.abgr();
		vertices[1].abgr = color.abgr();
		vertices += 2;
	};

	for (int y = -r2; y < r2; ++y) {
		float cy = cosf(y * STEP);
		float cy1 = cosf((y + 1) * STEP);
		float sy = sinf(y * STEP);
		float sy1 = sinf((y + 1) * STEP);

		for (int i = -p2; i < p2; ++i) {
			float ci = cosf(i * STEP);
			float si = sinf(i * STEP);
			add_line(DVec3(center.x + radius * ci * cy, center.y + radius * sy, center.z + radius * si * cy),
					DVec3(center.x + radius * ci * cy1, center.y + radius * sy1, center.z + radius * si * cy1));
			add_line(DVec3(center.x + radius * ci * cy, center.y + radius * sy, center.z + radius * si * cy),
					DVec3(center.x + radius * prev_ci * cy, center.y + radius * sy, center.z + radius * prev_si * cy));
			add_line(DVec3(center.x + radius * prev_ci * cy1, center.y + radius * sy1, center.z + radius * prev_si * cy1),
					DVec3(center.x + radius * ci * cy1, center.y + radius * sy1, center.z + radius * si * cy1));
			prev_ci = ci;
			prev_si = si;
		}
	}
}

struct PropertyDeserializeVisitor : reflection::IPropertyVisitor {
	PropertyDeserializeVisitor(Ref<InputMemoryStream> deserializer
		, ComponentUID cmp
		, const HashMap<EntityPtr, u32>& map
		, Span<const EntityRef> entities)
		: deserializer(deserializer.value) //-V1041
		, cmp(cmp)
		, map(map)
		, entities(entities)
	{}

	template <typename T>
	void set(const reflection::Property<T>& prop) {
		prop.set(cmp, idx, reflection::readFromStream<T>(deserializer));
	}

	void visit(const reflection::Property<float>& prop) override { set(prop); }
	void visit(const reflection::Property<int>& prop) override { set(prop); }
	void visit(const reflection::Property<u32>& prop) override { set(prop); }
	void visit(const reflection::Property<Vec2>& prop) override { set(prop); }
	void visit(const reflection::Property<Vec3>& prop) override { set(prop); }
	void visit(const reflection::Property<IVec3>& prop) override { set(prop); }
	void visit(const reflection::Property<Vec4>& prop) override { set(prop); }
	void visit(const reflection::Property<bool>& prop) override { set(prop); }
	void visit(const reflection::IBlobProperty& prop) override { prop.setValue(cmp, idx, deserializer); }
	void visit(const reflection::Property<const char*>& prop) override { set(prop); }
	void visit(const reflection::Property<Path>& prop) override { set(prop); }
	
	void visit(const reflection::IDynamicProperties& prop) override {
		u32 c;
		deserializer.read(c);
		for (u32 i = 0; i < c; ++i) {
			const char* name = deserializer.readString();
			reflection::IDynamicProperties::Type type;
			deserializer.read(type);
			switch(type) {
				case reflection::IDynamicProperties::RESOURCE:	
				case reflection::IDynamicProperties::STRING: {
					const char* tmp = deserializer.readString();
					reflection::IDynamicProperties::Value v;
					v.s = tmp;
					prop.set(cmp, idx, name, type, v);
					break;
				}
				default: {
					reflection::IDynamicProperties::Value v;
					deserializer.read(v);
					prop.set(cmp, idx, name, type, v);
					break;
				}
			}
		}
	}

	void visit(const reflection::Property<EntityPtr>& prop) override { 
		EntityPtr value;
		deserializer.read(Ref(value));
		auto iter = map.find(value);
		if (iter.isValid()) value = entities[iter.value()];
		
		prop.set(cmp, idx, value);
	}

	void visit(const reflection::IArrayProperty& prop) override {
		int count;
		deserializer.read(Ref(count));
		const int idx_backup = idx;
		while (prop.getCount(cmp) > count) {
			prop.removeItem(cmp, prop.getCount(cmp) - 1);
		}
		while (prop.getCount(cmp) < count) {
			prop.addItem(cmp, -1);
		}
		for (int i = 0; i < count; ++i) {
			idx = i;
			prop.visit(*this);
		}
		idx = idx_backup;
	}

	InputMemoryStream& deserializer;
	ComponentUID cmp;
	const HashMap<EntityPtr, u32>& map;
	Span<const EntityRef> entities;
	int idx;
};

struct PropertySerializeVisitor : reflection::IPropertyVisitor {
	PropertySerializeVisitor(Ref<OutputMemoryStream> serializer, ComponentUID cmp)
		: serializer(serializer.value) //-V1041
		, cmp(cmp)
	{}

	template <typename T>
	void get(const reflection::Property<T>& prop) {
		reflection::writeToStream(serializer, prop.get(cmp, idx));		
	}

	void visit(const reflection::Property<float>& prop) override { get(prop); }
	void visit(const reflection::Property<int>& prop) override { get(prop); }
	void visit(const reflection::Property<u32>& prop) override { get(prop); }
	void visit(const reflection::Property<EntityPtr>& prop) override { get(prop); }
	void visit(const reflection::Property<Vec2>& prop) override { get(prop); }
	void visit(const reflection::Property<Vec3>& prop) override { get(prop); }
	void visit(const reflection::Property<IVec3>& prop) override { get(prop); }
	void visit(const reflection::Property<Vec4>& prop) override { get(prop); }
	void visit(const reflection::Property<bool>& prop) override { get(prop); }
	void visit(const reflection::IBlobProperty& prop) override { prop.getValue(cmp, idx, serializer); }
	void visit(const reflection::Property<Path>& prop) override { get(prop); }
	void visit(const reflection::Property<const char*>& prop) override { get(prop); }

	void visit(const reflection::IDynamicProperties& prop) override {
		const u32 c = prop.getCount(cmp, idx);
		serializer.write(c);
		for (u32 i = 0; i < c; ++i) {
			const char* name = prop.getName(cmp, idx, i);
			serializer.writeString(name);
			const reflection::IDynamicProperties::Type type = prop.getType(cmp, idx, i);
			serializer.write(type);
			const reflection::IDynamicProperties::Value v = prop.getValue(cmp, idx, i);
			switch(type) {
				case reflection::IDynamicProperties::RESOURCE:	
				case reflection::IDynamicProperties::STRING: 
					serializer.writeString(v.s);
					break;
				default:
					serializer.write(v);
					break;
			}
		}
	}

	void visit(const reflection::IArrayProperty& prop) override {
		const int count = prop.getCount(cmp);
		serializer.write(count);
		const int idx_backup = idx;
		for (int i = 0; i < count; ++i) {
			idx = i;
			prop.visit(*this);
		}
		idx = idx_backup;
	}

	OutputMemoryStream& serializer;
	ComponentUID cmp;
	int idx;
};


static void save(ComponentUID cmp, Ref<OutputMemoryStream> out) {
	PropertySerializeVisitor save(out, cmp);
	save.idx = -1;
	reflection::getComponent(cmp.type)->visit(save);
}

static void load(ComponentUID cmp, Ref<InputMemoryStream> blob)
{
	struct : IAllocator {
		void* allocate(size_t size) override { ASSERT(false); return nullptr; }
		void deallocate(void* ptr) override { ASSERT(!ptr); }
		void* reallocate(void* ptr, size_t size) override { ASSERT(false); return nullptr; }
		void* allocate_aligned(size_t size, size_t align) override { ASSERT(false); return nullptr; }
		void deallocate_aligned(void* ptr) override { ASSERT(!ptr); }
		void* reallocate_aligned(void* ptr, size_t size, size_t align) override { ASSERT(false); return nullptr; }

	} alloc;
	HashMap<EntityPtr, u32> map(alloc);
	PropertyDeserializeVisitor v(blob, cmp, map, Span<EntityRef>(nullptr, nullptr));
	reflection::getComponent(cmp.type)->visit(v);
}


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


struct GatherResourcesVisitor final : reflection::IEmptyPropertyVisitor
{
	void visit(const reflection::IArrayProperty& prop) override
	{
		int count = prop.getCount(cmp);
		for (int i = 0; i < count; ++i) {
			index = i;
			prop.visit(*this);
		}
		index = -1;
	}

	void visit(const reflection::Property<Path>& prop) override
	{
		auto* attr = reflection::getAttribute(prop, reflection::IAttribute::RESOURCE);
		if (!attr) return;
		auto* resource_attr = (reflection::ResourceAttribute*)attr;

		Path path = prop.get(cmp, index);
		Resource* resource = resource_manager->load(resource_attr->resource_type, path);
		if(resource) resources->push(resource);
	}

	ResourceManagerHub* resource_manager;
	ComponentUID cmp;
	int index = -1;
	WorldEditor* editor;
	Array<Resource*>* resources;
};


struct RemoveArrayPropertyItemCommand final : IEditorCommand
{

public:
	RemoveArrayPropertyItemCommand(WorldEditor& editor,
		const ComponentUID& component,
		int index,
		const char* property)
		: m_component(component)
		, m_index(index)
		, m_property(property, editor.getAllocator())
		, m_old_values(editor.getAllocator())
	{
		save(m_component, Ref(m_old_values));
	}


	bool execute() override
	{
		struct : reflection::IEmptyPropertyVisitor {
			void visit(const reflection::IArrayProperty& prop) override {
				if (!equalStrings(prop.name, propname)) return;
				prop.removeItem(cmp, index);
			}
			ComponentUID cmp;
			const char* propname;
			int index;
		} v;
		v.propname = m_property.c_str();
		v.cmp = m_component;
		v.index = m_index;
		reflection::getComponent(m_component.type)->visit(v);
		return true;
	}


	void undo() override
	{
		InputMemoryStream old_values(m_old_values);
		load(m_component, Ref(old_values));
	}


	const char* getType() override { return "remove_array_property_item"; }


	bool merge(IEditorCommand&) override { return false; }

private:
	ComponentUID m_component;
	int m_index;
	String m_property;
	OutputMemoryStream m_old_values;
};


struct AddArrayPropertyItemCommand final : IEditorCommand
{

public:
	explicit AddArrayPropertyItemCommand(WorldEditor& editor)
	{
	}

	AddArrayPropertyItemCommand(WorldEditor& editor,
		const ComponentUID& component,
		const char* property)
		: m_component(component)
		, m_index(-1)
		, m_property(property)
	{
	}


	bool execute() override
	{
		struct : reflection::IEmptyPropertyVisitor {
			void visit(const reflection::IArrayProperty& prop) override {
				if (!equalStrings(prop.name, prop_name)) return;
				index = prop.getCount(cmp);
				prop.addItem(cmp, index);
			}
			ComponentUID cmp;
			int index;
			const char* prop_name;
		} v;
		v.cmp = m_component;
		v.prop_name = m_property;
		reflection::getComponent(m_component.type)->visit(v);
		m_index = v.index;
		return true;
	}


	void undo() override
	{
		struct : reflection::IEmptyPropertyVisitor {
			void visit(const reflection::IArrayProperty& prop) override {
				if (!equalStrings(prop.name, prop_name)) return;
				prop.removeItem(cmp, index);
			}
			ComponentUID cmp;
			int index;
			const char* prop_name;
		} v;
		v.cmp = m_component;
		v.index = m_index;
		v.prop_name = m_property;
		reflection::getComponent(m_component.type)->visit(v);
	}


	const char* getType() override { return "add_array_property_item"; }


	bool merge(IEditorCommand&) override { return false; }

private:
	ComponentUID m_component;
	int m_index;
	const char *m_property;
};


template <typename T> struct StoredType { 
	using Type = T; 
	static T construct(T value, IAllocator& allocator) { return value; }
	static T get(T value) { return value; }
};
template <> struct StoredType<const char*> {
	using Type = String;
	static String construct(const char* value, IAllocator& allocator) { return String(value, allocator); }
	static const char* get(const String& value) { return value.c_str(); }
};

namespace {
template <typename T2> const char* getSetPropertyCmdName();
template <> const char* getSetPropertyCmdName<i32>() { return "set_property_values_i32"; }
template <> const char* getSetPropertyCmdName<u32>() { return "set_property_values_u32"; }
template <> const char* getSetPropertyCmdName<float>() { return "set_property_values_float"; }
template <> const char* getSetPropertyCmdName<Vec2>() { return "set_property_values_vec2"; }
template <> const char* getSetPropertyCmdName<Vec3>() { return "set_property_values_vec3"; }
template <> const char* getSetPropertyCmdName<Vec4>() { return "set_property_values_vec4"; }
template <> const char* getSetPropertyCmdName<IVec3>() { return "set_property_values_ivec3"; }
template <> const char* getSetPropertyCmdName<Path>() { return "set_property_values_path"; }
template <> const char* getSetPropertyCmdName<const char*>() { return "set_property_values_cstr"; }
template <> const char* getSetPropertyCmdName<EntityPtr>() { return "set_property_values_entity"; }
template <> const char* getSetPropertyCmdName<bool>() { return "set_property_values_bool"; }
}

template <typename T>
struct SetPropertyCommand final : IEditorCommand
{
public:
	SetPropertyCommand(WorldEditor& editor,
		Span<const EntityRef> entities,
		ComponentType component_type,
		const char* array,
		int index,
		const char* property_name,
		T value)
		: m_component_type(component_type)
		, m_entities(editor.getAllocator())
		, m_property_name(property_name, editor.getAllocator())
		, m_editor(editor)
		, m_index(index)
		, m_array(array, editor.getAllocator())
		, m_old_values(editor.getAllocator())
		, m_new_value(StoredType<T>::construct(value, editor.getAllocator()))
	{
		m_entities.reserve(entities.length());
		Universe* universe = m_editor.getUniverse();

		const reflection::ComponentBase* cmp_desc = reflection::getComponent(component_type);

		for (u32 i = 0; i < entities.length(); ++i) {
			ComponentUID component = universe->getComponent(entities[i], component_type);
			if (!component.isValid()) continue;

			PropertySerializeVisitor v(Ref<OutputMemoryStream>(m_old_values), component);
			v.idx = -1;
			cmp_desc->visit(v);
			m_entities.push(entities[i]);
		}
	}


	template <typename T2> static void set(Ref<reflection::IDynamicProperties::Value> v, T2) { ASSERT(false); }
	static void set(Ref<reflection::IDynamicProperties::Value> v, i32 val) { reflection::set(v.value, val); }
	static void set(Ref<reflection::IDynamicProperties::Value> v, float val) { reflection::set(v.value, val); }
	static void set(Ref<reflection::IDynamicProperties::Value> v, Path val) { reflection::set(v.value, val.c_str()); }
	static void set(Ref<reflection::IDynamicProperties::Value> v, const char* val) { reflection::set(v.value, val); }
	static void set(Ref<reflection::IDynamicProperties::Value> v, EntityPtr val) { reflection::set(v.value, val); }
	static void set(Ref<reflection::IDynamicProperties::Value> v, bool val) { reflection::set(v.value, val); }
	static void set(Ref<reflection::IDynamicProperties::Value> v, Vec3 val) { reflection::set(v.value, val); }
	static void set(Ref<reflection::IDynamicProperties::Value> v, const String& val) { reflection::set(v.value, val.c_str()); }


	bool execute() override
	{
		struct : reflection::IEmptyPropertyVisitor {
			void visit(const reflection::Property<T>& prop) override { 
				if (array[0] != '\0') return;
				if (!equalIStrings(prop_name, prop.name)) return;
				found = true;
				for (EntityPtr entity : cmd->m_entities) {
					const ComponentUID cmp = cmd->m_editor.getUniverse()->getComponent((EntityRef)entity, cmd->m_component_type);
					prop.set(cmp, cmd->m_index, StoredType<T>::get(cmd->m_new_value));
				}
			}

			void visit(const reflection::IArrayProperty& prop) override { 
				if (!equalStrings(array, prop.name)) return;

				const char* tmp = array;
				array = "";
				prop.visit(*this);
				array = tmp;
			}

			void visit(const reflection::IDynamicProperties& prop) override { 
				for (EntityPtr entity : cmd->m_entities) {
					const ComponentUID cmp = cmd->m_editor.getUniverse()->getComponent((EntityRef)entity, cmd->m_component_type);
					const u32 c = prop.getCount(cmp, cmd->m_index);
					for (u32 i = 0; i < c; ++i) {
						const char* name = prop.getName(cmp, cmd->m_index, i);
						if (!equalStrings(prop_name, name)) continue;
						found = true;
						reflection::IDynamicProperties::Value v;
						set(Ref(v), cmd->m_new_value);
						prop.set(cmp, cmd->m_index, i, v);
					}
				}
			}
			SetPropertyCommand<T>* cmd;
			const char* prop_name;
			const char* array;
			bool found = false;
		} v;
		v.cmd = this;
		v.prop_name = m_property_name.c_str();
		v.array = m_array.c_str();
		reflection::getComponent(m_component_type)->visit(v);
		return v.found;
	}


	void undo() override
	{
		InputMemoryStream blob(m_old_values);
		const reflection::ComponentBase* cmp_desc = reflection::getComponent(m_component_type);
		HashMap<EntityPtr, u32> map(m_editor.getAllocator());
		Universe* universe = m_editor.getUniverse();
		Span<const EntityRef> entities(nullptr, nullptr);
		for (int i = 0; i < m_entities.size(); ++i) {
			const ComponentUID cmp = universe->getComponent(m_entities[i], m_component_type);
			PropertyDeserializeVisitor v(Ref<InputMemoryStream>(blob), cmp, map, entities);	
			cmp_desc->visit(v);
		}
	}


	const char* getType() override { return getSetPropertyCmdName<T>(); }

	bool merge(IEditorCommand& command) override
	{
		ASSERT(command.getType() == getType());
		SetPropertyCommand& src = static_cast<SetPropertyCommand&>(command);
		if (m_component_type == src.m_component_type &&
			m_entities.size() == src.m_entities.size() &&
			src.m_array == m_array &&
			src.m_property_name == m_property_name &&
			m_index == src.m_index)
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
	Array<EntityRef> m_entities;
	typename StoredType<T>::Type m_new_value;
	OutputMemoryStream m_old_values;
	String m_array;
	int m_index;
	String m_property_name;
};

struct PasteEntityCommand;


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
					logError("Failed to create component on entity ", e.index);
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
				resource->decRefCount();
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
					const reflection::ComponentBase* cmp_desc = reflection::getComponent(cmp.type);

					GatherResourcesVisitor gather;
					gather.cmp = cmp;
					gather.editor = &m_editor;
					gather.resources = &m_resources;
					gather.resource_manager = &resource_manager;
					cmp_desc->visit(gather);

					Lumix::save(cmp, Ref(m_old_values));
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
				const char* name = blob.readString();
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
					ComponentUID new_component;
					IScene* scene = universe->getScene(cmp_type);
					ASSERT(scene);
					universe->createComponent(cmp_type, new_entity);
					new_component.entity = new_entity;
					new_component.scene = scene;
					new_component.type = cmp_type;
					
					::Lumix::load(new_component, Ref(blob));
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
				resource->decRefCount();
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
			for (EntityRef entity : m_entities)
			{
				cmp.entity = entity;
				universe->createComponent(cmp.type, entity);
				::Lumix::load(cmp, Ref(blob));
			}
		}


		bool merge(IEditorCommand&) override { return false; }


		const char* getType() override { return "destroy_components"; }


		bool execute() override
		{
			ASSERT(!m_entities.empty());
			const reflection::ComponentBase* cmp_desc = reflection::getComponent(m_cmp_type);
			ComponentUID cmp;
			cmp.type = m_cmp_type;
			cmp.scene = m_editor.getUniverse()->getScene(m_cmp_type);
			ASSERT(cmp.scene);

			ResourceManagerHub& resource_manager = m_editor.getEngine().getResourceManager();

			for (EntityRef entity : m_entities) {
				cmp.entity = entity;
				Lumix::save(cmp, Ref(m_old_values));

				GatherResourcesVisitor gather;
				gather.cmp = cmp;
				gather.editor = &m_editor;
				gather.resources = &m_resources;
				gather.resource_manager = &resource_manager;
				cmp_desc->visit(gather);

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
		AddEntityCommand(WorldEditorImpl& editor, const DVec3& position, EntityRef* output)
			: m_editor(editor)
			, m_position(position)
			, m_output(output)
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
			m_editor.selectEntities(Span(&e, 1), false);
			if (m_output) {
				*m_output = e;
			}
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


	private:
		WorldEditorImpl& m_editor;
		EntityPtr m_entity;
		DVec3 m_position;
		EntityRef* m_output;
	};

public:
	IAllocator& getAllocator() override { return m_allocator; }

	UniverseView& getView() override { ASSERT(m_view); return *m_view; }

	void setView(UniverseView* view) override { 
		m_view = view; 
	}

	Universe* getUniverse() override { return m_universe; }


	Engine& getEngine() override { return m_engine; }


	void update() override
	{
		PROFILE_FUNCTION();

		Gizmo::frame();
		m_prefab_system->update();
	}


	~WorldEditorImpl()
	{
		destroyUniverse();

		m_prefab_system.reset();
	}


	void snapEntities(const DVec3& hit_pos, bool translate_mode) override
	{
		Array<DVec3> positions(m_allocator);
		Array<Quat> rotations(m_allocator);
		if(translate_mode) {
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
		UniquePtr<MoveEntityCommand> cmd = UniquePtr<MoveEntityCommand>::create(m_allocator, 
			*this,
			&m_selected_entities[0],
			&positions[0],
			&rotations[0],
			positions.size(),
			m_allocator);
		executeCommand(cmd.move());
	}


	bool isUniverseChanged() const override { return m_is_universe_changed; }

	void saveUniverse(const char* basename, bool save_path) override
	{
		saveProject();

		logInfo("Saving universe ", basename, "...");
		
		if (!os::makePath("universes")) logError("Could not create directory universes/");
		StaticString<LUMIX_MAX_PATH> path(m_engine.getFileSystem().getBasePath(), "universes/", basename, ".unv");
		StaticString<LUMIX_MAX_PATH> bkp_path(path, ".bak");
		if (os::fileExists(path)) {
			if (!os::copyFile(path, bkp_path)) {
				logError("Could not copy ", path, " to ", bkp_path);
			}
		}
		os::OutputFile file;
		if (file.open(path)) {
			save(file);
			file.close();
		}
		else {
			logError("Failed to save universe ", basename);
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
		const Viewport& vp = getView().getViewport();
		blob.write(vp.pos);
		blob.write(vp.rot);
		header.hash = crc32((const u8*)blob.data() + hashed_offset, (int)blob.size() - hashed_offset);
		memcpy(blob.getMutableData(), &header, sizeof(header));
		file.write(blob.data(), blob.size());

		logInfo("Universe saved");
	}


	void makeParent(EntityPtr parent, EntityRef child) override
	{
		UniquePtr<MakeParentCommand> command = UniquePtr<MakeParentCommand>::create(m_allocator, *this, parent, child);
		executeCommand(command.move());
	}


	void destroyEntities(const EntityRef* entities, int count) override
	{
		UniquePtr<DestroyEntitiesCommand> command = UniquePtr<DestroyEntitiesCommand>::create(m_allocator, *this, entities, count);
		executeCommand(command.move());
	}


	EntityRef addEntity() override
	{
		return addEntityAt(m_view->getViewport().w >> 1, m_view->getViewport().h >> 1);
	}


	EntityRef addEntityAt(int camera_x, int camera_y) override
	{
		DVec3 origin;
		Vec3 dir;

		const UniverseView::RayHit hit = m_view->getCameraRaycastHit(camera_x, camera_y);

		EntityRef res;
		UniquePtr<AddEntityCommand> command = UniquePtr<AddEntityCommand>::create(m_allocator, *this, hit.pos, &res);
		executeCommand(command.move());

		return res;
	}


	EntityRef addEntityAt(const DVec3& pos) override
	{
		EntityRef res;
		UniquePtr<AddEntityCommand> command = UniquePtr<AddEntityCommand>::create(m_allocator, *this, pos, &res);
		executeCommand(command.move());

		return res;
	}


	void setEntitiesScales(const EntityRef* entities, const float* scales, int count) override
	{
		if (count <= 0) return;

		UniquePtr<IEditorCommand> command =
			UniquePtr<ScaleEntityCommand>::create(m_allocator, *this, entities, scales, count, m_allocator);
		executeCommand(command.move());
	}


	void setEntitiesScale(const EntityRef* entities, int count, float scale) override
	{
		if (count <= 0) return;

		UniquePtr<IEditorCommand> command =
			UniquePtr<ScaleEntityCommand>::create(m_allocator, *this, entities, count, scale, m_allocator);
		executeCommand(command.move());
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
		UniquePtr<IEditorCommand> command =
			UniquePtr<MoveEntityCommand>::create(m_allocator, *this, entities, &positions[0], rotations, count, m_allocator);
		executeCommand(command.move());
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
		UniquePtr<IEditorCommand> command = UniquePtr<MoveEntityCommand>::create(m_allocator, *this, entities, &poss[0], &rots[0], count, m_allocator);
		executeCommand(command.move());
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
		UniquePtr<IEditorCommand> command =
			UniquePtr<LocalMoveEntityCommand>::create(m_allocator, *this, entities, &poss[0], count, m_allocator);
		executeCommand(command.move());
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
		UniquePtr<IEditorCommand> command =
			UniquePtr<MoveEntityCommand>::create(m_allocator, *this, entities, positions, &rots[0], count, m_allocator);
		executeCommand(command.move());
	}

	void setEntitiesPositionsAndRotations(const EntityRef* entities,
		const DVec3* positions,
		const Quat* rotations,
		int count) override
	{
		if (count <= 0) return;
		UniquePtr<IEditorCommand> command =
			UniquePtr<MoveEntityCommand>::create(m_allocator, *this, entities, positions, rotations, count, m_allocator);
		executeCommand(command.move());
	}


	void setEntityName(EntityRef entity, const char* name) override
	{
		UniquePtr<IEditorCommand> command = UniquePtr<SetEntityNameCommand>::create(m_allocator, *this, entity, name);
		executeCommand(command.move());
	}


	void beginCommandGroup(u32 type) override
	{
		while (m_undo_index < m_undo_stack.size() - 1)
		{
			for(int i = m_undo_stack.size() - 1; i > m_undo_index; --i)
			{
				m_undo_stack[i].reset();
			}
			m_undo_stack.resize(m_undo_index + 1);
		}

		if(m_undo_index >= 0)
		{
			static const u32 end_group_hash = crc32("end_group");
			if(crc32(m_undo_stack[m_undo_index]->getType()) == end_group_hash)
			{
				if(static_cast<EndGroupCommand*>(m_undo_stack[m_undo_index].get())->group_type == type)
				{
					m_undo_stack[m_undo_index].reset();
					--m_undo_index;
					m_undo_stack.pop();
					return;
				}
			}
		}

		m_current_group_type = type;
		UniquePtr<BeginGroupCommand> cmd = UniquePtr<BeginGroupCommand>::create(m_allocator);
		m_undo_stack.push(cmd.move());
		++m_undo_index;
	}


	void endCommandGroup() override
	{
		if (m_undo_index < m_undo_stack.size() - 1)
		{
			for (int i = m_undo_stack.size() - 1; i > m_undo_index; --i)
			{
				m_undo_stack[i].reset();
			}
			m_undo_stack.resize(m_undo_index + 1);
		}

		UniquePtr<EndGroupCommand> cmd = UniquePtr<EndGroupCommand>::create(m_allocator);
		cmd->group_type = m_current_group_type;
		m_undo_stack.push(cmd.move());
		++m_undo_index;
	}

	void executeCommand(UniquePtr<IEditorCommand>&& command) override
	{
		doExecute(command.move());
	}


	void doExecute(UniquePtr<IEditorCommand>&& command)
	{
		m_is_universe_changed = true;
		if (m_undo_index >= 0 && command->getType() == m_undo_stack[m_undo_index]->getType())
		{
			if (command->merge(*m_undo_stack[m_undo_index]))
			{
				m_undo_stack[m_undo_index]->execute();
				return;
			}
		}

		if (command->execute())
		{
			if (m_undo_index < m_undo_stack.size() - 1) {
				m_undo_stack.resize(m_undo_index + 1);
			}
			m_undo_stack.emplace(command.move());
			if (m_is_game_mode) ++m_game_mode_commands;
			++m_undo_index;
			return;
		}
		else {
			logError("Editor command failed");
		}
		command.reset();
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
			m_undo_stack.pop();
			--m_undo_index;
		}

		ASSERT(m_universe);
		m_engine.getResourceManager().enableUnload(false);
		m_engine.stopGame(*m_universe);
		selectEntities({}, false);
		m_is_game_mode = false;
		if (reload)
		{
			m_universe_destroyed.invoke();
			m_prefab_system->setUniverse(nullptr);
			StaticString<64> name(m_universe->getName());
			m_engine.destroyUniverse(*m_universe);
			
			m_universe = &m_engine.createUniverse(true);
			m_prefab_system->setUniverse(m_universe);
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
			selectEntities(Span(&e, 1), false);
		}
		m_engine.getResourceManager().enableUnload(true);
	}


	PrefabSystem& getPrefabSystem() override
	{
		return *m_prefab_system;
	}

	void copyEntities(Span<const EntityRef> entities, OutputMemoryStream& serializer)
	{
		i32 count = entities.length();
		serializer.write(count);
		for (int i = 0; i < count; ++i) {
			m_copy_buffer.write(entities[i]);
		}
		for (int i = 0; i < count; ++i) {
			EntityRef entity = entities[i];
			Transform tr = m_universe->getTransform(entity);
			serializer.write(tr);
			serializer.writeString(m_universe->getEntityName(entity));
			serializer.write(m_universe->getParent(entity));

			for (ComponentUID cmp = m_universe->getFirstComponent(entity);
				cmp.isValid();
				cmp = m_universe->getNextComponent(cmp))
			{
				const u32 cmp_type = crc32(reflection::getComponent(cmp.type)->name);
				serializer.write(cmp_type);
				const reflection::ComponentBase* cmp_desc = reflection::getComponent(cmp.type);
				
				PropertySerializeVisitor visitor(Ref<OutputMemoryStream>(serializer), cmp);
				visitor.idx = -1;
				cmp_desc->visit(visitor);
			}
			serializer.write((u32)0);
		}
	}

	void gatherHierarchy(EntityRef e, Ref<Array<EntityRef>> entities) {
		entities->push(e);
		for (EntityPtr child = m_universe->getFirstChild(e); 
			child.isValid(); 
			child = m_universe->getNextSibling((EntityRef)child)) 
		{
			const EntityRef ch = (EntityRef)child;
			if (entities->indexOf(ch) < 0) {
				gatherHierarchy(ch, entities);
			}
		}
	}


	void copyEntities() override {
		if (m_selected_entities.empty()) return;

		m_copy_buffer.clear();

		Array<EntityRef> entities(m_allocator);
		entities.reserve(m_selected_entities.size());
		for (EntityRef e : m_selected_entities) {
			gatherHierarchy(e, Ref(entities));
		}
		copyEntities(entities, m_copy_buffer);
	}


	bool canPasteEntities() const override
	{
		return m_copy_buffer.size() > 0;
	}


	void pasteEntities() override;
	void duplicateEntities() override;


	void destroyComponent(Span<const EntityRef> entities, ComponentType cmp_type) override
	{
		ASSERT(entities.length() > 0);
		UniquePtr<IEditorCommand> command = UniquePtr<DestroyComponentCommand>::create(m_allocator, *this, entities, cmp_type);
		executeCommand(command.move());
	}


	void addComponent(Span<const EntityRef> entities, ComponentType cmp_type) override
	{
		ASSERT(entities.length() > 0);
		UniquePtr<IEditorCommand> command = UniquePtr<AddComponentCommand>::create(m_allocator, *this, entities, cmp_type);
		executeCommand(command.move());
	}

	void saveProject() {
		const char* base_path = m_engine.getFileSystem().getBasePath();
		const StaticString<LUMIX_MAX_PATH> path(base_path, "lumix.prj");
		os::OutputFile file;
		if (file.open(path)) {
			OutputMemoryStream stream(m_allocator);
			m_engine.serializeProject(stream);
			bool saved = true;
			if (!file.write(stream.data(), stream.size())) {
				logError("Failed to save project ", path);
				saved = false;
			}
			file.close();
			if (!saved) os::deleteFile(path);
			return;
		}
		logError("Failed to save project ", path);
	}

	bool loadProject() override {
		OutputMemoryStream data(m_allocator);
		if (!m_engine.getFileSystem().getContentSync(Path("lumix.prj"), Ref(data))) return false;
		
		InputMemoryStream stream(data);
		return m_engine.deserializeProject(stream);
	}

	void loadUniverse(const char* basename) override
	{
		if (m_is_game_mode) stopGameMode(false);
		destroyUniverse();
		createUniverse();
		m_universe->setName(basename);
		logInfo("Loading universe ", basename, "...");
		os::InputFile file;
		const StaticString<LUMIX_MAX_PATH> path(m_engine.getFileSystem().getBasePath(), "universes/", basename, ".unv");
		if (file.open(path)) {
			if (!load(file)) {
				logError("Failed to parse ", path);
				newUniverse();
			}
			file.close();
		}
		else {
			logError("Failed to open ", path);
			newUniverse();
		}
	}


	void newUniverse() override
	{
		destroyUniverse();
		createUniverse();
		logInfo("Universe created.");
	}


	enum class SerializedVersion : i32
	{
		CAMERA,
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
			logError("Corrupted file.");
			m_is_loading = false;
			return false;
		}
		if (file_size > 0xffFFffFF) {
			logError("File too big.");
			m_is_loading = false;
			return false;
		}

		os::Timer timer;
		logInfo("Parsing universe...");
		OutputMemoryStream data(m_allocator);
		if (!file.getBuffer()) {
			data.resize((u32)file_size);
			if (!file.read(data.getMutableData(), data.size())) {
				logError("Failed to load file.");
				m_is_loading = false;
				return false;
			}
		}
		InputMemoryStream blob(file.getBuffer() ? file.getBuffer() : data.data(), (int)file_size);
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
			logError("Corrupted file.");
			m_is_loading = false;
			return false;
		}

		EntityMap entity_map(m_allocator);
		if (m_engine.deserialize(*m_universe, blob, Ref(entity_map)))
		{
			m_prefab_system->deserialize(blob, entity_map);
			if (header.version > (i32)SerializedVersion::CAMERA) {
				DVec3 pos;
				Quat rot;
				blob.read(pos);
				blob.read(rot);
				if (m_view) {
					Viewport vp = m_view->getViewport();
					vp.pos = pos;
					vp.rot = rot;
					m_view->setViewport(vp);
				}

			}
			logInfo("Universe parsed in ", timer.getTimeSinceStart(), " seconds");
			m_is_loading = false;
			return true;
		}

		newUniverse();
		m_is_loading = false;
		return false;
	}


	WorldEditorImpl(Engine& engine, IAllocator& allocator)
		: m_allocator(allocator)
		, m_universe_destroyed(m_allocator)
		, m_universe_created(m_allocator)
		, m_selected_entities(m_allocator)
		, m_undo_stack(m_allocator)
		, m_copy_buffer(m_allocator)
		, m_is_loading(false)
		, m_universe(nullptr)
		, m_selected_entity_on_game_mode(INVALID_ENTITY)
		, m_is_game_mode(false)
		, m_undo_index(-1)
		, m_engine(engine)
		, m_game_mode_file(m_allocator)
	{
		loadProject();
		logInfo("Initializing editor...");

		m_prefab_system = PrefabSystem::create(*this);
		createUniverse();
	}


	bool isEntitySelected(EntityRef entity) const override
	{
		return m_selected_entities.indexOf(entity) >= 0;
	}


	const Array<EntityRef>& getSelectedEntities() const override
	{
		return m_selected_entities;
	}


	void addArrayPropertyItem(const ComponentUID& cmp, const char* property) override
	{
		if (cmp.isValid())
		{
			UniquePtr<IEditorCommand> command = UniquePtr<AddArrayPropertyItemCommand>::create(m_allocator, *this, cmp, property);
			executeCommand(command.move());
		}
	}


	void removeArrayPropertyItem(const ComponentUID& cmp, int index, const char* property) override
	{
		if (cmp.isValid())
		{
			UniquePtr<IEditorCommand> command = UniquePtr<RemoveArrayPropertyItemCommand>::create(m_allocator, *this, cmp, index, property);
			executeCommand(command.move());
		}
	}

	template <typename T>
	void set(ComponentType cmp, const char* array, int idx, const char* prop, Span<const EntityRef> entities, T val) {
		UniquePtr<IEditorCommand> command = UniquePtr<SetPropertyCommand<T>>::create(m_allocator, *this, entities, cmp, array, idx, prop, val);
		executeCommand(command.move());
		
	}

	void setProperty(ComponentType cmp, const char* array, int idx, const char* prop, Span<const EntityRef> entities, float val) override { set(cmp, array, idx, prop, entities, val); }
	void setProperty(ComponentType cmp, const char* array, int idx, const char* prop, Span<const EntityRef> entities, i32 val) override { set(cmp, array, idx, prop, entities, val); }
	void setProperty(ComponentType cmp, const char* array, int idx, const char* prop, Span<const EntityRef> entities, u32 val) override { set(cmp, array, idx, prop, entities, val); }
	void setProperty(ComponentType cmp, const char* array, int idx, const char* prop, Span<const EntityRef> entities, EntityPtr val) override { set(cmp, array, idx, prop, entities, val); }
	void setProperty(ComponentType cmp, const char* array, int idx, const char* prop, Span<const EntityRef> entities, const char* val) override { set(cmp, array, idx, prop, entities, val); }
	void setProperty(ComponentType cmp, const char* array, int idx, const char* prop, Span<const EntityRef> entities, const Path& val) override { set(cmp, array, idx, prop, entities, val); }
	void setProperty(ComponentType cmp, const char* array, int idx, const char* prop, Span<const EntityRef> entities, bool val) override { set(cmp, array, idx, prop, entities, val); }
	void setProperty(ComponentType cmp, const char* array, int idx, const char* prop, Span<const EntityRef> entities, const Vec2& val) override { set(cmp, array, idx, prop, entities, val); }
	void setProperty(ComponentType cmp, const char* array, int idx, const char* prop, Span<const EntityRef> entities, const Vec3& val) override { set(cmp, array, idx, prop, entities, val); }
	void setProperty(ComponentType cmp, const char* array, int idx, const char* prop, Span<const EntityRef> entities, const Vec4& val) override { set(cmp, array, idx, prop, entities, val); }
	void setProperty(ComponentType cmp, const char* array, int idx, const char* prop, Span<const EntityRef> entities, const IVec3& val) override { set(cmp, array, idx, prop, entities, val); }

	void selectEntities(Span<const EntityRef> entities, bool toggle) override {
		if (!toggle) {
			m_selected_entities.clear();
			for (EntityRef e : entities) {
				m_selected_entities.push(e);
			}
		}
		else {
			for (EntityRef e : entities) { 
				const i32 idx = m_selected_entities.indexOf(e);
				if (idx < 0) {
					m_selected_entities.push(e);
				}
				else {
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
		m_prefab_system->setUniverse(nullptr);
		selectEntities({}, false);
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

		m_selected_entities.clear();
		m_universe_created.invoke();
		m_prefab_system->setUniverse(universe);
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


	static int getEntitiesCount(Universe& universe)
	{
		int count = 0;
		for (EntityPtr e = universe.getFirstEntity(); e.isValid(); e = universe.getNextEntity((EntityRef)e)) ++count;
		return count;
	}

private:
	IAllocator& m_allocator;
	Engine& m_engine;
	UniverseView* m_view = nullptr;
	UniquePtr<PrefabSystem> m_prefab_system;
	Universe* m_universe;
	bool m_is_loading;
	bool m_is_universe_changed;
	
	Array<UniquePtr<IEditorCommand>> m_undo_stack;
	int m_undo_index;
	u32 m_current_group_type;

	Array<EntityRef> m_selected_entities;
	EntityPtr m_selected_entity_on_game_mode;

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
		, m_entities(editor.getAllocator())
		, m_map(editor.getAllocator())
		, m_identity(identity)
	{
		UniverseView& view = editor.getView();
		const UniverseView::RayHit hit = view.getCameraRaycastHit(view.getViewport().w >> 1, view.getViewport().h >> 1);
		m_position = hit.pos;
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
		m_editor.selectEntities(m_entities, false);

		Transform base_tr;
		base_tr.pos = m_position;
		base_tr.scale = 1;
		base_tr.rot = Quat(0, 0, 0, 1);
		m_map.reserve(entity_count);
		for (int i = 0; i < entity_count; ++i) {
			EntityRef orig_e;
			blob.read(Ref(orig_e));
			if (!is_redo) m_map.insert(orig_e, i);
		}
		for (int i = 0; i < entity_count; ++i)
		{
			Transform tr;
			blob.read(Ref(tr));
			const char* name = blob.readString();
			if (name[0]) universe.setEntityName(m_entities[i], name);
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
				cmp.type = reflection::getComponentTypeFromHash(hash);
				cmp.scene = universe.getScene(cmp.type);

				cmp.scene->getUniverse().createComponent(cmp.type, new_entity);

				PropertyDeserializeVisitor visitor(Ref<InputMemoryStream>(blob), cmp, m_map, m_entities);
				visitor.idx = -1;
				reflection::getComponent(cmp.type)->visit(visitor);
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
	UniquePtr<PasteEntityCommand> command = UniquePtr<PasteEntityCommand>::create(m_allocator, *this, m_copy_buffer);
	executeCommand(command.move());
}


void WorldEditorImpl::duplicateEntities()
{
	copyEntities();

	UniquePtr<PasteEntityCommand> command = UniquePtr<PasteEntityCommand>::create(m_allocator, *this, m_copy_buffer, true);
	executeCommand(command.move());
}


UniquePtr<WorldEditor> WorldEditor::create(Engine& engine, IAllocator& allocator)
{
	return UniquePtr<WorldEditorImpl>::create(allocator, engine, allocator);
}


} // namespace Lumix
