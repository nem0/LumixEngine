#include "world_editor.h"

#include "editor/entity_folders.h"
#include "editor/gizmo.h"
#include "editor/prefab_system.h"
#include "core/array.h"
#include "core/associative_array.h"
#include "core/command_line_parser.h"
#include "core/crt.h"
#include "core/delegate_list.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "core/geometry.h"
#include "core/hash.h"
#include "engine/plugin.h"
#include "core/log.h"
#include "core/math.h"
#include "core/metaprogramming.h"
#include "core/os.h"
#include "core/path.h"
#include "core/profiler.h"
#include "engine/reflection.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "core/stream.h"
#include "core/string.h"
#include "engine/world.h"
#include "render_interface.h"


namespace Lumix
{


void addCube(WorldView& view, const DVec3& pos, const Vec3& right, const Vec3& up, const Vec3& dir, Color color) {
	WorldView::Vertex* vertices = view.render(true, 24);
	const DVec3& cam_pos = view.getViewport().pos;

	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = Vec3(a - cam_pos);
		vertices[1].pos = Vec3(b - cam_pos);
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

void addCube(WorldView& view, const DVec3& min, const DVec3& max, Color color) {
	DVec3 a = min;
	DVec3 b = min;
	b.x = max.x;
	
	WorldView::Vertex* vertices = view.render(true, 24);
	const DVec3 cam_pos = view.getViewport().pos;

	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = Vec3(a - cam_pos);
		vertices[1].pos = Vec3(b - cam_pos);
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

void addCylinder(WorldView& view, const DVec3& pos, const Vec3& up, float radius, float height, Color color) {
	Vec3 x_vec(0, up.z, -up.y);
	if (squaredLength(x_vec) < 0.01) {
		x_vec = Vec3(up.y, -up.x, 0);
	}
	x_vec = normalize(x_vec);
	const Vec3 z_vec = normalize(cross(x_vec, up));

	const DVec3 top = pos + up * height;
	WorldView::Vertex* vertices = view.render(true, 32 * 6);
	const DVec3 cam_pos = view.getViewport().pos;

	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = Vec3(a - cam_pos);
		vertices[1].pos = Vec3(b - cam_pos);
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

void addLine(WorldView& view, const DVec3& a, const DVec3& b, Color color) {
	WorldView::Vertex* vertices = view.render(true, 2);
	const DVec3 cam_pos = view.getViewport().pos;
	vertices[0].pos = Vec3(a - cam_pos);
	vertices[1].pos = Vec3(b - cam_pos);
	vertices[0].abgr = color.abgr();
	vertices[1].abgr = color.abgr();
}

void addCone(WorldView& view, const DVec3& vertex, const Vec3& dir, const Vec3& axis0, const Vec3& axis1, Color color) {
	WorldView::Vertex* vertices = view.render(true, 32 * 4);
	const DVec3 cam_pos = view.getViewport().pos;
	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = Vec3(a - cam_pos);
		vertices[1].pos = Vec3(b - cam_pos);
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

void addHalfSphere(WorldView& view, const DVec3& center, float radius, bool top, Color color)
{
	static const int COLS = 36;
	static const int ROWS = COLS >> 1;
	static const float STEP = (PI / 180.0f) * 360.0f / COLS;
	int p2 = COLS >> 1;
	int yfrom = top ? 0 : -(ROWS >> 1);
	int yto = top ? ROWS >> 1 : 0;

	WorldView::Vertex* vertices = view.render(true, (yto - yfrom) * 2 * p2 * 6);
	const DVec3 cam_pos = view.getViewport().pos;

	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = Vec3(a - cam_pos);
		vertices[1].pos = Vec3(b - cam_pos);
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

void addCapsule(WorldView& view, const DVec3& position, float height, float radius, Color color) {
	addHalfSphere(view, position + Vec3(0, radius, 0), radius, false, color);
	addHalfSphere(view, position + Vec3(0, radius + height, 0), radius, true, color);

	Vec3 z_vec(0, 0, 1.0f);
	Vec3 x_vec(1.0f, 0, 0);
	const DVec3 bottom = position + Vec3(0, radius, 0);
	const DVec3 top = bottom + Vec3(0, height, 0);
	WorldView::Vertex* vertices = view.render(true, 32 * 2);
	const DVec3 cam_pos = view.getViewport().pos;

	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = Vec3(a - cam_pos);
		vertices[1].pos = Vec3(b - cam_pos);
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

void addFrustum(WorldView& view, const struct ShiftedFrustum& frustum, Color color) {
	const DVec3 o = frustum.origin;

	WorldView::Vertex* vertices = view.render(true, 24);
	const DVec3 cam_pos = view.getViewport().pos;

	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = Vec3(a - cam_pos);
		vertices[1].pos = Vec3(b - cam_pos);
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

void addCircle(WorldView& view, const DVec3& center, float radius, const Vec3& up, Color color) {
	WorldView::Vertex* vertices = view.render(true, 64);
	const Vec3 offset = Vec3(center - view.getViewport().pos);
	const Vec3 x = normalize(fabsf(up.x) > 0.001f ? Vec3(up.y, -up.x, 0) : Vec3(0, up.z, -up.y));
	const Vec3 y = cross(x, up);
	for (u32 i = 0; i < 32; ++i) {
		const float a = i / 32.f * PI * 2;
		const float b = a + PI * 2 / 32.f;
		const float sa = sinf(a) * radius;
		const float ca = cosf(a) * radius;
		const float sb = sinf(b) * radius;
		const float cb = cosf(b) * radius;
		vertices[i * 2].pos = offset + x * ca + y * sa;
		vertices[i * 2].abgr = color.abgr();
		vertices[i * 2 + 1].pos = offset + x * cb + y * sb;
		vertices[i * 2 + 1].abgr = color.abgr();
	}
}

void addSphere(WorldView& view, const DVec3& center, float radius, Color color) {
	static const int COLS = 36;
	static const int ROWS = COLS >> 1;
	static const float STEP = (PI / 180.0f) * 360.0f / COLS;
	int p2 = COLS >> 1;
	int r2 = ROWS >> 1;
	float prev_ci = 1;
	float prev_si = 0;

	const u32 count = 2 * r2 * 2 * p2;
	WorldView::Vertex* vertices = view.render(true, count * 6);
	const DVec3& cam_pos = view.getViewport().pos;
	auto add_line = [&](const DVec3& a, const DVec3& b){
		vertices[0].pos = Vec3(a - cam_pos);
		vertices[1].pos = Vec3(b - cam_pos);
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

template <typename T> void writeToStream(OutputMemoryStream& stream, T value) {	stream.write(value); }
template <typename T> T readFromStream(InputMemoryStream& stream) { return stream.read<T>(); }
template <> LUMIX_EDITOR_API void writeToStream<const Path&>(OutputMemoryStream& stream, const Path& path);
	
template <> LUMIX_EDITOR_API Path readFromStream<Path>(InputMemoryStream& stream)
{
	const char* c_str = (const char*)stream.getData() + stream.getPosition();
	Path path(c_str);
	stream.skip(stringLength(c_str) + 1);
	return path;
}


template <> LUMIX_EDITOR_API void writeToStream<const Path&>(OutputMemoryStream& stream, const Path& path) {
	StringView str = path;
	stream.write(str.begin, str.size());
	stream.write((char)0);
}


template <> LUMIX_EDITOR_API void writeToStream<Path>(OutputMemoryStream& stream, Path path) {
	StringView str = path;
	stream.write(str.begin, str.size());
	stream.write((char)0);
}

template <> LUMIX_EDITOR_API const char* readFromStream<const char*>(InputMemoryStream& stream)
{
	const char* c_str = (const char*)stream.getData() + stream.getPosition();
	stream.skip(stringLength(c_str) + 1);
	return c_str;
}


template <> LUMIX_EDITOR_API void writeToStream<const char*>(OutputMemoryStream& stream, const char* value)
{
	stream.write(value, stringLength(value) + 1);
}


struct PropertyDeserializeVisitor : reflection::IPropertyVisitor {
	PropertyDeserializeVisitor(InputMemoryStream& deserializer
		, ComponentUID cmp
		, const HashMap<EntityPtr, u32>& map
		, Span<const EntityRef> entities)
		: deserializer(deserializer)
		, cmp(cmp)
		, map(map)
		, entities(entities)
	{}

	template <typename T>
	void set(const reflection::Property<T>& prop) {
		if (prop.setter) prop.set(cmp, idx, readFromStream<T>(deserializer));
	}

	void visit(const reflection::Property<float>& prop) override { set(prop); }
	void visit(const reflection::Property<int>& prop) override { set(prop); }
	void visit(const reflection::Property<u32>& prop) override { set(prop); }
	void visit(const reflection::Property<Vec2>& prop) override { set(prop); }
	void visit(const reflection::Property<Vec3>& prop) override { set(prop); }
	void visit(const reflection::Property<IVec3>& prop) override { set(prop); }
	void visit(const reflection::Property<Vec4>& prop) override { set(prop); }
	void visit(const reflection::Property<bool>& prop) override { set(prop); }
	void visit(const reflection::Property<const char*>& prop) override { set(prop); }
	void visit(const reflection::Property<Path>& prop) override { set(prop); }
	void visit(const reflection::BlobProperty& prop) override { prop.setValue(cmp, idx, deserializer); }
	
	void visit(const reflection::DynamicProperties& prop) override {
		u32 c;
		deserializer.read(c);
		for (u32 i = 0; i < c; ++i) {
			const char* name = deserializer.readString();
			reflection::DynamicProperties::Type type;
			deserializer.read(type);
			switch(type) {
				case reflection::DynamicProperties::RESOURCE:	
				case reflection::DynamicProperties::STRING: {
					const char* tmp = deserializer.readString();
					reflection::DynamicProperties::Value v;
					v.s = tmp;
					prop.set(cmp, idx, name, type, v);
					break;
				}
				default: {
					reflection::DynamicProperties::Value v;
					deserializer.read(v);
					prop.set(cmp, idx, name, type, v);
					break;
				}
			}
		}
	}
	
	void visit(const reflection::Property<EntityPtr>& prop) override { 
		if (!prop.setter) return;

		EntityPtr value;
		deserializer.read(value);
		auto iter = map.find(value);
		if (iter.isValid()) value = entities[iter.value()];
		
		prop.set(cmp, idx, value);
	}

	void visit(const reflection::ArrayProperty& prop) override {
		u32 count;
		deserializer.read(count);
		const int idx_backup = idx;
		while (prop.getCount(cmp) > count) {
			prop.removeItem(cmp, prop.getCount(cmp) - 1);
		}
		while (prop.getCount(cmp) < count) {
			prop.addItem(cmp, -1);
		}
		for (u32 i = 0; i < count; ++i) {
			idx = i;
			prop.visitChildren(*this);
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
	PropertySerializeVisitor(OutputMemoryStream& serializer, ComponentUID cmp)
		: serializer(serializer)
		, cmp(cmp)
	{}

	template <typename T>
	void get(const reflection::Property<T>& prop) {
		if (prop.isReadonly()) return;
		writeToStream(serializer, prop.get(cmp, idx));		
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
	void visit(const reflection::BlobProperty& prop) override { prop.getValue(cmp, idx, serializer); }
	void visit(const reflection::Property<Path>& prop) override { get(prop); }
	void visit(const reflection::Property<const char*>& prop) override { get(prop); }
	
	void visit(const reflection::DynamicProperties& prop) override {
		const u32 c = prop.getCount(cmp, idx);
		serializer.write(c);
		for (u32 i = 0; i < c; ++i) {
			const char* name = prop.getName(cmp, idx, i);
			serializer.writeString(name);
			const reflection::DynamicProperties::Type type = prop.getType(cmp, idx, i);
			serializer.write(type);
			const reflection::DynamicProperties::Value v = prop.getValue(cmp, idx, i);
			switch(type) {
				case reflection::DynamicProperties::RESOURCE:	
				case reflection::DynamicProperties::STRING: 
					serializer.writeString(v.s);
					break;
				default:
					serializer.write(v);
					break;
			}
		}
	}

	void visit(const reflection::ArrayProperty& prop) override {
		const int count = prop.getCount(cmp);
		serializer.write(count);
		const int idx_backup = idx;
		for (int i = 0; i < count; ++i) {
			idx = i;
			prop.visitChildren(*this);
		}
		idx = idx_backup;
	}

	OutputMemoryStream& serializer;
	ComponentUID cmp;
	int idx = -1;
};


static void save(ComponentUID cmp, OutputMemoryStream& out) {
	PropertySerializeVisitor save(out, cmp);
	save.idx = -1;
	reflection::getComponent(cmp.type)->visit(save);
}

static void load(ComponentUID cmp, InputMemoryStream& blob)
{
	struct : IAllocator {
		void* allocate(size_t size, size_t align) override { ASSERT(false); return nullptr; }
		void deallocate(void* ptr) override { ASSERT(!ptr); }
		void* reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) override { ASSERT(false); return nullptr; }

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

	RuntimeHash group_type;
	bool locked = false;
};


struct SetEntityNameCommand final : IEditorCommand
{
public:
	SetEntityNameCommand(WorldEditor& editor, EntityRef entity, const char* name)
		: m_entity(entity)
		, m_new_name(name, editor.getAllocator())
		, m_old_name(editor.getWorld()->getEntityName(entity),
					 editor.getAllocator())
		, m_editor(editor)
	{
	}


	bool execute() override
	{
		m_editor.getWorld()->setEntityName((EntityRef)m_entity, m_new_name);
		return true;
	}


	void undo() override
	{
		m_editor.getWorld()->setEntityName((EntityRef)m_entity, m_old_name);
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
		World* world = m_editor.getWorld();
		m_entities.reserve(count);
		m_new_positions.reserve(count);
		m_new_rotations.reserve(count);
		m_old_positions.reserve(count);
		m_old_rotations.reserve(count);
		for (int i = count - 1; i >= 0; --i)
		{
			m_entities.push(entities[i]);
			m_new_positions.push(new_positions[i]);
			m_new_rotations.push(new_rotations[i]);
			m_old_positions.push(world->getPosition(entities[i]));
			m_old_rotations.push(world->getRotation(entities[i]));
		}
	}


	bool execute() override
	{
		World* world = m_editor.getWorld();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if(m_entities[i].isValid()) {
				const EntityRef entity = (EntityRef)m_entities[i];
				world->setPosition(entity, m_new_positions[i]);
				world->setRotation(entity, m_new_rotations[i]);
			}
		}
		return true;
	}


	void undo() override
	{
		World* world = m_editor.getWorld();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if(m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				world->setPosition(entity, m_old_positions[i]);
				world->setRotation(entity, m_old_rotations[i]);
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

struct LocalRotateEntityCommand final : IEditorCommand {
	explicit LocalRotateEntityCommand(WorldEditor& editor)
		: m_new_rotations(editor.getAllocator())
		, m_old_rotations(editor.getAllocator())
		, m_entities(editor.getAllocator())
		, m_editor(editor)
	{}

	LocalRotateEntityCommand(WorldEditor& editor,
		const EntityRef* entities,
		const Quat* new_rotations,
		int count,
		IAllocator& allocator)
		: m_new_rotations(allocator)
		, m_old_rotations(allocator)
		, m_entities(allocator)
		, m_editor(editor)
	{
		ASSERT(count > 0);
		World* world = m_editor.getWorld();
		m_entities.reserve(count);
		m_new_rotations.reserve(count);
		m_old_rotations.reserve(count);
		for (int i = count - 1; i >= 0; --i)
		{
			m_entities.push(entities[i]);
			m_new_rotations.push(new_rotations[i]);
			m_old_rotations.push(world->getLocalTransform(entities[i]).rot);
		}
	}

	bool execute() override {
		World* world = m_editor.getWorld();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if (m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				world->setLocalRotation(entity, m_new_rotations[i]);
			}
		}
		return true;
	}

	void undo() override {
		World* world = m_editor.getWorld();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if (m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				world->setLocalRotation(entity, m_old_rotations[i]);
			}
		}
	}

	const char* getType() override { return "local_rotate_entity"; }

	bool merge(IEditorCommand& command) override {
		ASSERT(command.getType() == getType());
		LocalRotateEntityCommand& my_command = static_cast<LocalRotateEntityCommand&>(command);
		if (my_command.m_entities.size() != m_entities.size()) return false;

		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if (m_entities[i] != my_command.m_entities[i]) {
				return false;
			}
		}
		
		my_command.m_new_rotations = m_new_rotations.move();
		return true;
	}

private:
	WorldEditor& m_editor;
	Array<EntityPtr> m_entities;
	Array<Quat> m_new_rotations;
	Array<Quat> m_old_rotations;
};

struct LocalMoveEntityCommand final : IEditorCommand {
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
		World* world = m_editor.getWorld();
		m_entities.reserve(count);
		m_new_positions.reserve(count);
		m_old_positions.reserve(count);
		for (int i = count - 1; i >= 0; --i)
		{
			m_entities.push(entities[i]);
			m_new_positions.push(new_positions[i]);
			m_old_positions.push(world->getLocalTransform(entities[i]).pos);
		}
	}


	bool execute() override
	{
		World* world = m_editor.getWorld();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if (m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				world->setLocalPosition(entity, m_new_positions[i]);
			}
		}
		return true;
	}


	void undo() override
	{
		World* world = m_editor.getWorld();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if (m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				world->setLocalPosition(entity, m_old_positions[i]);
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
		const Vec3& scale,
		IAllocator& allocator)
		: m_old_scales(allocator)
		, m_new_scales(allocator)
		, m_entities(allocator)
		, m_editor(editor)
	{
		World* world = m_editor.getWorld();
		for (int i = count - 1; i >= 0; --i)
		{
			m_entities.push(entities[i]);
			m_old_scales.push(world->getScale(entities[i]));
			m_new_scales.push(scale);
		}
	}


	ScaleEntityCommand(WorldEditor& editor,
		const EntityRef* entities,
		const Vec3* scales,
		int count,
		IAllocator& allocator)
		: m_old_scales(allocator)
		, m_new_scales(allocator)
		, m_entities(allocator)
		, m_editor(editor)
	{
		World* world = m_editor.getWorld();
		for (int i = count - 1; i >= 0; --i)
		{
			m_entities.push(entities[i]);
			m_old_scales.push(world->getScale(entities[i]));
			m_new_scales.push(scales[i]);
		}
	}


	bool execute() override
	{
		World* world = m_editor.getWorld();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if (m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				world->setScale(entity, m_new_scales[i]);
			}
		}
		return true;
	}


	void undo() override
	{
		World* world = m_editor.getWorld();
		for (int i = 0, c = m_entities.size(); i < c; ++i) {
			if (m_entities[i].isValid()) {
				EntityRef entity = (EntityRef)m_entities[i];
				world->setScale(entity, m_old_scales[i]);
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
	Array<Vec3> m_new_scales;
	Array<Vec3> m_old_scales;
};


struct GatherResourcesVisitor final : reflection::IEmptyPropertyVisitor
{
	void visit(const reflection::ArrayProperty& prop) override
	{
		int count = prop.getCount(cmp);
		for (int i = 0; i < count; ++i) {
			index = i;
			prop.visitChildren(*this);
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
		save(m_component, m_old_values);
	}


	bool execute() override
	{
		struct : reflection::IEmptyPropertyVisitor {
			void visit(const reflection::ArrayProperty& prop) override {
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
		load(m_component, old_values);
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
		reflection::ArrayProperty* prop = (reflection::ArrayProperty*)reflection::getProperty(m_component.type, m_property);
		m_index = prop->getCount(m_component);
		prop->addItem(m_component, m_index);
		return true;
	}


	void undo() override
	{
		struct : reflection::IEmptyPropertyVisitor {
			void visit(const reflection::ArrayProperty& prop) override {
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
		World* world = m_editor.getWorld();

		const reflection::ComponentBase* cmp_desc = reflection::getComponent(component_type);

		for (u32 i = 0; i < entities.length(); ++i) {
			ComponentUID component = world->getComponent(entities[i], component_type);
			if (!component.isValid()) continue;

			PropertySerializeVisitor v(m_old_values, component);
			v.idx = -1;
			cmp_desc->visit(v);
			m_entities.push(entities[i]);
		}
	}


	template <typename T2> static void set(reflection::DynamicProperties::Value& v, T2) { ASSERT(false); }
	static void set(reflection::DynamicProperties::Value& v, i32 val) { reflection::set(v, val); }
	static void set(reflection::DynamicProperties::Value& v, float val) { reflection::set(v, val); }
	static void set(reflection::DynamicProperties::Value& v, const Path& val) { reflection::set(v, val.c_str()); }
	static void set(reflection::DynamicProperties::Value& v, const char* val) { reflection::set(v, val); }
	static void set(reflection::DynamicProperties::Value& v, EntityPtr val) { reflection::set(v, val); }
	static void set(reflection::DynamicProperties::Value& v, bool val) { reflection::set(v, val); }
	static void set(reflection::DynamicProperties::Value& v, Vec3 val) { reflection::set(v, val); }
	static void set(reflection::DynamicProperties::Value& v, const String& val) { reflection::set(v, val.c_str()); }


	bool execute() override
	{
		struct : reflection::IEmptyPropertyVisitor {
			void visit(const reflection::Property<T>& prop) override { 
				if (array[0] != '\0') return;
				if (!equalIStrings(prop_name, prop.name)) return;
				found = true;
				for (EntityPtr entity : cmd->m_entities) {
					const ComponentUID cmp = cmd->m_editor.getWorld()->getComponent((EntityRef)entity, cmd->m_component_type);
					ASSERT(prop.setter);
					prop.set(cmp, cmd->m_index, StoredType<T>::get(cmd->m_new_value));
				}
			}

			void visit(const reflection::ArrayProperty& prop) override { 
				if (!equalStrings(array, prop.name)) return;

				const char* tmp = array;
				array = "";
				prop.visitChildren(*this);
				array = tmp;
			}

			void visit(const reflection::DynamicProperties& prop) override { 
				for (EntityPtr entity : cmd->m_entities) {
					const ComponentUID cmp = cmd->m_editor.getWorld()->getComponent((EntityRef)entity, cmd->m_component_type);
					const u32 c = prop.getCount(cmp, cmd->m_index);
					for (u32 i = 0; i < c; ++i) {
						const char* name = prop.getName(cmp, cmd->m_index, i);
						if (!equalStrings(prop_name, name)) continue;
						found = true;
						reflection::DynamicProperties::Value v;
						set(v, cmd->m_new_value);
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
		World* world = m_editor.getWorld();
		Span<const EntityRef> entities(nullptr, nullptr);
		for (int i = 0; i < m_entities.size(); ++i) {
			const ComponentUID cmp = world->getComponent(m_entities[i], m_component_type);
			PropertyDeserializeVisitor v(blob, cmp, map, entities);	
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


struct WorldEditorImpl final : WorldEditor
{
	friend struct PasteEntityCommand;
private:
	struct DestroyEntityFolderCommand final : IEditorCommand {
		DestroyEntityFolderCommand(WorldEditorImpl& editor, EntityFolders::FolderHandle folder)
			: m_editor(editor)
			, m_folder(folder)
			, m_folder_name(editor.getAllocator())
		{
			const EntityFolders::Folder& f = editor.m_entity_folders->getFolder(folder);
			m_parent = f.parent;
			m_folder_name = f.name;
		}

		bool execute() override {
			m_editor.m_entity_folders->destroyFolder(m_folder);
			return true;
		}

		void undo() override {
			m_editor.m_entity_folders->emplaceFolder(m_folder, m_parent);
			EntityFolders::Folder& f = m_editor.m_entity_folders->getFolder(m_folder);
			copyString(f.name, m_folder_name);
		}

		const char* getType() override { return "destroy_entity_folder"; }
		bool merge(IEditorCommand& command) override { return false; }

		WorldEditorImpl& m_editor;
		EntityFolders::FolderHandle m_parent;
		EntityFolders::FolderHandle m_folder;
		String m_folder_name;
	};

	struct MoveEntityFolderCommand final : IEditorCommand {
		MoveEntityFolderCommand(WorldEditorImpl& editor, EntityFolders::FolderHandle folder, EntityFolders::FolderHandle new_parent)
			: m_editor(editor)
			, m_new_parent(new_parent)
			, m_folder(folder)
		{
			m_old_parent = m_editor.getEntityFolders().getFolder(folder).parent;
		}

		bool execute() override {
			m_editor.getEntityFolders().moveFolder(m_folder, m_new_parent);
			return true;
		}

		void undo() override {
			m_editor.getEntityFolders().moveFolder(m_folder, m_old_parent);
		}

		const char* getType() override { return "move_entity_folder"; }
		bool merge(IEditorCommand& command) override { return false; }

		WorldEditorImpl& m_editor;
		EntityFolders::FolderHandle m_folder;
		EntityFolders::FolderHandle m_new_parent;
		EntityFolders::FolderHandle m_old_parent;
	};

	struct CreateEntityFolderCommand final : IEditorCommand {
		CreateEntityFolderCommand(WorldEditorImpl& editor, EntityFolders::FolderHandle parent, EntityFolders::FolderHandle* out)
			: m_editor(editor)
			, m_parent(parent)
			, m_out(out)
		{}

		bool execute() override {
			m_folder = m_editor.m_entity_folders->emplaceFolder(m_folder, m_parent);
			if (m_out) *m_out = m_folder;
			m_out = nullptr;
			return true;
		}

		void undo() override {
			m_editor.m_entity_folders->destroyFolder(m_folder);
		}

		const char* getType() override { return "create_entity_folder"; }
		bool merge(IEditorCommand& command) override { return false; }
	
		WorldEditorImpl& m_editor;
		EntityFolders::FolderHandle m_parent;
		EntityFolders::FolderHandle m_folder = EntityFolders::INVALID_FOLDER;
		EntityFolders::FolderHandle* m_out;
	};

	struct RenameEntityFolderCommand final : IEditorCommand {
		RenameEntityFolderCommand(WorldEditorImpl& editor, EntityFolders::FolderHandle folder, const char* new_name)
			: m_editor(editor)
			, m_folder(folder)
			, m_new_name(new_name, editor.getAllocator())
			, m_old_name(editor.getAllocator())
		{
			m_old_name = m_editor.m_entity_folders->getFolder(m_folder).name;
		}

		bool execute() override {
			m_editor.m_entity_folders->renameFolder(m_folder, m_new_name);
			return true;
		}

		void undo() override {
			m_editor.m_entity_folders->renameFolder(m_folder, m_old_name);
		}

		const char* getType() override { return "rename_entity_folder"; }
		bool merge(IEditorCommand& command) override { 
			RenameEntityFolderCommand& cmd = (RenameEntityFolderCommand&)command;
			if (cmd.m_folder != m_folder) return false;
			cmd.m_new_name = m_new_name;
			return true;
		}
	
		WorldEditorImpl& m_editor;
		EntityFolders::FolderHandle m_folder;
		String m_new_name;
		String m_old_name;
	};
	
	struct MoveEntityToFolderCommand final : IEditorCommand {
		MoveEntityToFolderCommand(WorldEditorImpl& editor, EntityRef entity, EntityFolders::FolderHandle folder)
			: m_editor(editor)
			, m_new_folder(folder)
			, m_entity(entity)
		{
			m_old_folder = editor.m_entity_folders->getFolder(entity);
		}

		bool execute() override {
			m_editor.m_entity_folders->moveToFolder(m_entity, m_new_folder);
			return true;
		}

		void undo() override {
			m_editor.m_entity_folders->moveToFolder(m_entity, m_old_folder);
		}

		const char* getType() override { return "move_entity_to_folder"; }

		bool merge(IEditorCommand& command) override { return false; }
	
		WorldEditorImpl& m_editor;
		EntityFolders::FolderHandle m_new_folder;
		EntityFolders::FolderHandle m_old_folder;
		EntityRef m_entity;
	};


	struct AddComponentCommand final : IEditorCommand {
		AddComponentCommand(WorldEditorImpl& editor,
							Span<const EntityRef> entities,
							ComponentType type)
			: m_editor(editor)
			, m_entities(editor.getAllocator())
		{
			m_type = type;
			m_entities.reserve(entities.length());
			World* world = m_editor.getWorld();
			for (EntityRef e : entities) {
				if (!world->hasComponent(e, type)) {
					m_entities.push(e);
				}
			}
		}


		bool merge(IEditorCommand&) override { return false; }


		const char* getType() override { return "add_component"; }


		bool execute() override
		{
			bool ret = false;
			World* world = m_editor.getWorld();

			for (EntityRef e : m_entities) {
				ASSERT(!world->hasComponent(e, m_type));
				world->createComponent(m_type, e);
				if (world->hasComponent(e, m_type)) {
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
			World* world = m_editor.getWorld();
			for (EntityRef e : m_entities) {
				if (world->hasComponent(e, m_type)) {
					world->destroyComponent(e, m_type);
					ASSERT(!world->hasComponent(e, m_type));
				}
			}
		}


	private:
		ComponentType m_type;
		Array<EntityRef> m_entities;
		WorldEditorImpl& m_editor;
	};


	struct MakeParentCommand final : IEditorCommand {
		explicit MakeParentCommand(WorldEditorImpl& editor)
			: m_editor(editor)
		{
		}


		MakeParentCommand(WorldEditorImpl& editor, EntityPtr parent, EntityRef child)
			: m_editor(editor)
			, m_parent(parent)
			, m_child(child)
		{
			m_old_folder = editor.m_entity_folders->getFolder(child);
			m_old_parent = m_editor.getWorld()->getParent(m_child);
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
			m_editor.getWorld()->setParent(m_parent, m_child);
			if (m_parent.isValid()) {
				EntityFolders::FolderHandle f = m_editor.m_entity_folders->getFolder((EntityRef)m_parent);
				m_editor.m_entity_folders->moveToFolder(m_child, f);
			}
			return true;
		}


		void undo() override
		{
			m_editor.getWorld()->setParent(m_old_parent, m_child);
			m_editor.m_entity_folders->moveToFolder(m_child, m_old_folder);
		}

	private:
		WorldEditorImpl& m_editor;
		EntityFolders::FolderHandle m_old_folder;
		EntityPtr m_parent;
		EntityPtr m_old_parent;
		EntityRef m_child;
	};


	struct DestroyEntitiesCommand final : IEditorCommand {
		explicit DestroyEntitiesCommand(WorldEditorImpl& editor)
			: m_editor(editor)
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
			if (!m_entities.empty()) {
				fastRemoveDuplicates(m_entities);
			}
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
			World* world = m_editor.getWorld();
			for (EntityRef e : world->childrenOf(entity))
			{
				m_entities.push(e);
				pushChildren(e);
			}
		}


		bool execute() override
		{
			World* world = m_editor.getWorld();
			m_transformations.clear();
			m_old_values.clear();
			ResourceManagerHub& resource_manager = m_editor.getEngine().getResourceManager();
			for (int i = 0; i < m_entities.size(); ++i)
			{
				m_transformations.emplace(world->getTransform(m_entities[i]));
				int count = 0;
				for (ComponentUID cmp = world->getFirstComponent(m_entities[i]);
					cmp.isValid();
					cmp = world->getNextComponent(cmp))
				{
					++count;
				}
				m_old_values.writeString(world->getEntityName(m_entities[i]));
				EntityPtr parent = world->getParent(m_entities[i]);
				const EntityFolders::FolderHandle folder = m_editor.m_entity_folders->getFolder(m_entities[i]);
				m_old_values.write(folder);
				m_old_values.write(parent);
				if (parent.isValid())
				{
					Transform local_tr = world->getLocalTransform(m_entities[i]);
					m_old_values.write(local_tr);
				}
				for (EntityRef child : world->childrenOf(m_entities[i]))
				{
					m_old_values.write(child);
					Transform local_tr = world->getLocalTransform(child);
					m_old_values.write(local_tr);
				}
				m_old_values.write(INVALID_ENTITY);

				m_old_values.write(count);
				for (ComponentUID cmp = world->getFirstComponent(m_entities[i]);
					cmp.isValid();
					cmp = world->getNextComponent(cmp))
				{
					m_old_values.write(cmp.type);
					const reflection::ComponentBase* cmp_desc = reflection::getComponent(cmp.type);

					GatherResourcesVisitor gather;
					gather.cmp = cmp;
					gather.editor = &m_editor;
					gather.resources = &m_resources;
					gather.resource_manager = &resource_manager;
					cmp_desc->visit(gather);

					Lumix::save(cmp, m_old_values);
				}
				const PrefabHandle prefab = m_editor.getPrefabSystem().getPrefab(m_entities[i]);
				m_old_values.write(prefab);
			}
			for (EntityRef e : m_entities)
			{
				world->destroyEntity(e);
			}
			return true;
		}


		bool merge(IEditorCommand&) override { return false; }


		void undo() override
		{
			World* world = m_editor.getWorld();
			InputMemoryStream blob(m_old_values);
			for (int i = 0; i < m_entities.size(); ++i)
			{
				world->emplaceEntity(m_entities[i]);
			}
			for (int i = 0; i < m_entities.size(); ++i)
			{
				EntityRef new_entity = m_entities[i];
				world->setTransform(new_entity, m_transformations[i]);
				int cmps_count;
				const char* name = blob.readString();
				world->setEntityName(new_entity, name);
				EntityPtr parent;
				EntityFolders::FolderHandle folder;
				blob.read(folder);
				m_editor.m_entity_folders->moveToFolder(new_entity, folder);
				blob.read(parent);
				if (parent.isValid())
				{
					Transform local_tr;
					blob.read(local_tr);
					world->setParent(parent, new_entity);
					world->setLocalTransform(new_entity, local_tr);
				}
				EntityPtr child;
				for(blob.read(child); child.isValid(); blob.read(child))
				{
					Transform local_tr;
					blob.read(local_tr);
					world->setParent(new_entity, (EntityRef)child);
					world->setLocalTransform((EntityRef)child, local_tr);
				}

				blob.read(cmps_count);
				for (int j = 0; j < cmps_count; ++j)
				{
					ComponentType cmp_type;
					blob.read(cmp_type);
					ComponentUID new_component;
					IModule* module = world->getModule(cmp_type);
					ASSERT(module);
					world->createComponent(cmp_type, new_entity);
					new_component.entity = new_entity;
					new_component.module = module;
					new_component.type = cmp_type;
					
					::Lumix::load(new_component, blob);
				}
				PrefabHandle tpl;
				blob.read(tpl);
				if (tpl.getHashValue()) m_editor.getPrefabSystem().setPrefab(new_entity, tpl);
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


	struct DestroyComponentCommand final : IEditorCommand {
		explicit DestroyComponentCommand(WorldEditorImpl& editor)
			: m_editor(editor)
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
			for (EntityRef e : entities) {
				if (!m_editor.getWorld()->hasComponent(e, m_cmp_type)) continue;
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
			World* world = m_editor.getWorld();
			cmp.module = world->getModule(m_cmp_type);
			cmp.type = m_cmp_type;
			ASSERT(cmp.module);
			InputMemoryStream blob(m_old_values);
			for (EntityRef entity : m_entities)
			{
				cmp.entity = entity;
				world->createComponent(cmp.type, entity);
				::Lumix::load(cmp, blob);
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
			cmp.module = m_editor.getWorld()->getModule(m_cmp_type);
			ASSERT(cmp.module);

			ResourceManagerHub& resource_manager = m_editor.getEngine().getResourceManager();

			for (EntityRef entity : m_entities) {
				cmp.entity = entity;
				Lumix::save(cmp, m_old_values);

				GatherResourcesVisitor gather;
				gather.cmp = cmp;
				gather.editor = &m_editor;
				gather.resources = &m_resources;
				gather.resource_manager = &resource_manager;
				cmp_desc->visit(gather);

				m_editor.getWorld()->destroyComponent(entity, m_cmp_type);
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


	struct AddEntityCommand final : IEditorCommand {
		AddEntityCommand(WorldEditorImpl& editor, const DVec3& position, EntityRef* output)
			: m_editor(editor)
			, m_position(position)
			, m_output(output)
		{
			m_entity = INVALID_ENTITY;
			m_folder = m_editor.getEntityFolders().getSelectedFolder();
		}


		bool execute() override
		{
			EntityFolders& folders = m_editor.getEntityFolders();
			EntityFolders::FolderHandle selected_folder = folders.getSelectedFolder();
			folders.selectFolder(m_folder);
			if (m_entity.isValid()) {
				m_editor.getWorld()->emplaceEntity((EntityRef)m_entity);
				m_editor.getWorld()->setPosition((EntityRef)m_entity, m_position);
			}
			else {
				m_entity = m_editor.getWorld()->createEntity(m_position, Quat(0, 0, 0, 1));
			}
			const EntityRef e = (EntityRef)m_entity;
			if (m_output) {
				*m_output = e;
			}
			folders.selectFolder(selected_folder);
			return true;
		}


		void undo() override
		{
			ASSERT(m_entity.isValid());

			const EntityRef e = (EntityRef)m_entity;
			m_editor.getWorld()->destroyEntity(e);
		}


		bool merge(IEditorCommand&) override { return false; }
		const char* getType() override { return "add_entity"; }


	private:
		WorldEditorImpl& m_editor;
		EntityPtr m_entity;
		DVec3 m_position;
		EntityRef* m_output;
		EntityFolders::FolderHandle m_folder;
	};

public:
	IAllocator& getAllocator() override { return m_allocator; }
	WorldView& getView() override { ASSERT(m_view); return *m_view; }
	void setView(WorldView* view) override { m_view = view; }
	World* getWorld() override { return m_world; }
	Engine& getEngine() override { return m_engine; }

	void update() override {
		PROFILE_FUNCTION();
		Gizmo::frame();
		m_prefab_system->update();
	}

	~WorldEditorImpl() {
		destroyWorld();
		m_prefab_system.reset();
	}

	void snapEntities(const DVec3& hit_pos, bool translate_mode) override {
		Array<DVec3> positions(m_allocator);
		Array<Quat> rotations(m_allocator);
		if(translate_mode) {
			for(auto e : m_selected_entities)
			{
				positions.push(hit_pos);
				rotations.push(m_world->getRotation(e));
			}
		}
		else
		{
			for(auto e : m_selected_entities)
			{
				const DVec3 pos = m_world->getPosition(e);
				Vec3 dir = normalize(Vec3(pos - hit_pos));
				Matrix mtx = Matrix::IDENTITY;
				Vec3 y(0, 1, 0);
				if(dot(y, dir) > 0.99f)
				{
					y = Vec3(1, 0, 0);
				}
				Vec3 x = normalize(cross(y, dir));
				y = normalize(cross(dir, x));
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

	bool isWorldChanged() const override { return m_is_world_changed; }

	void savePartition(World::PartitionHandle partition) override {
		ASSERT(!isGameMode());
		const Array<World::Partition>& partitions = m_world->getPartitions();
		if (partitions.size() == 1) {
			ASSERT(partition == partitions[0].handle);
			save();
			return;
		}

		UniquePtr<WorldEditorImpl> partition_clone = createPartitionWorld(partition);
		World* cloned_world = partition_clone->getWorld();
		World::Partition& cloned_partition = cloned_world->getPartitions()[0];
		World::Partition& src_partition = m_world->getPartition(partition);

		copyString(cloned_partition.name, src_partition.name);
		partition_clone->save();
	}

	void save() {
		ASSERT(m_world->getPartitions().size() == 1);
		const char* path = m_world->getPartitions()[0].name;
		ASSERT(path[0]);
		saveProject();

		logInfo("Saving world ", path, "...");
		
		FileSystem& fs = m_engine.getFileSystem();
		Path bkp_path(path, ".bak");
		if (fs.fileExists(path)) {
			if (!fs.copyFile(path, bkp_path)) {
				logError("Could not copy ", path, " to ", bkp_path);
			}
		}
		os::OutputFile file;
		if (fs.open(path, file)) {
			save(file, false);
			file.close();
		}
		else {
			logError("Failed to save world ", path);
		}
		
		m_is_world_changed = false;
	}


	void save(IOutputStream& file, bool is_game_mode_save)
	{
		while (m_engine.getFileSystem().hasWork()) m_engine.getFileSystem().processCallbacks();

		ASSERT(m_world);

		OutputMemoryStream blob(m_allocator);
		blob.reserve(64 * 1024);

		m_world->serialize(blob, is_game_mode_save ? WorldSerializeFlags::HAS_PARTITIONS : WorldSerializeFlags::NONE);
		m_prefab_system->serialize(blob);
		m_entity_folders->serialize(blob);
		if (m_view) {
			const Viewport& vp = m_view->getViewport();
			blob.write(vp.pos);
			blob.write(vp.rot);
		}
		else {
			DVec3 pos(0);
			blob.write(pos);
			blob.write(Quat::IDENTITY);
		}
		file.write(blob.data(), blob.size());
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
		const Viewport& vp = m_view->getViewport();
		return addEntityAt(vp.w >> 1, vp.h >> 1);
	}


	EntityRef addEntityAt(i32 camera_x, i32 camera_y)
	{
		DVec3 origin;
		Vec3 dir;

		const WorldView::RayHit hit = m_view->getCameraRaycastHit(camera_x, camera_y, INVALID_ENTITY);

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


	void setEntitiesScales(const EntityRef* entities, const Vec3* scales, int count) override
	{
		if (count <= 0) return;

		UniquePtr<IEditorCommand> command =
			UniquePtr<ScaleEntityCommand>::create(m_allocator, *this, entities, scales, count, m_allocator);
		executeCommand(command.move());
	}


	void setEntitiesScale(const EntityRef* entities, int count, const Vec3& scale) override
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

		World* world = getWorld();
		Array<DVec3> positions(m_allocator);
		for (int i = 0; i < count; ++i)
		{
			positions.push(world->getPosition(entities[i]));
		}
		UniquePtr<IEditorCommand> command =
			UniquePtr<MoveEntityCommand>::create(m_allocator, *this, entities, &positions[0], rotations, count, m_allocator);
		executeCommand(command.move());
	}


	void setEntitiesCoordinate(const EntityRef* entities, int count, double value, Coordinate coord) override
	{
		ASSERT(entities);
		if (count <= 0) return;

		World* world = getWorld();
		Array<Quat> rots(m_allocator);
		Array<DVec3> poss(m_allocator);
		rots.reserve(count);
		poss.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			rots.push(world->getRotation(entities[i]));
			poss.push(world->getPosition(entities[i]));
			(&poss[i].x)[(int)coord] = value;
		}
		UniquePtr<IEditorCommand> command = UniquePtr<MoveEntityCommand>::create(m_allocator, *this, entities, &poss[0], &rots[0], count, m_allocator);
		executeCommand(command.move());
	}
	
	void setEntitiesLocalRotation(const EntityRef* entities, const Quat* local_rotations, u32 count) override
	{
		ASSERT(entities);
		if (count <= 0) return;

		UniquePtr<IEditorCommand> command = UniquePtr<LocalRotateEntityCommand>::create(m_allocator, *this, entities, local_rotations, count, m_allocator);
		executeCommand(command.move());
	}

	void setEntitiesLocalCoordinate(const EntityRef* entities, int count, double value, Coordinate coord) override
	{
		ASSERT(entities);
		if (count <= 0) return;

		World* world = getWorld();
		Array<DVec3> poss(m_allocator);
		poss.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			poss.push(world->getLocalTransform(entities[i]).pos);
			(&poss[i].x)[(int)coord] = value;
		}
		UniquePtr<IEditorCommand> command = UniquePtr<LocalMoveEntityCommand>::create(m_allocator, *this, entities, &poss[0], count, m_allocator);
		executeCommand(command.move());
	}


	void setEntitiesPositions(const EntityRef* entities, const DVec3* positions, int count) override
	{
		ASSERT(entities && positions);
		if (count <= 0) return;

		World* world = getWorld();
		Array<Quat> rots(m_allocator);
		for (int i = 0; i < count; ++i)
		{
			rots.push(world->getRotation(entities[i]));
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

	void beginCommandGroup(const char* type_str) override
	{
		const RuntimeHash type(type_str);
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
			if (equalStrings(m_undo_stack[m_undo_index]->getType(), "end_group"))
			{
				auto* end_group = static_cast<EndGroupCommand*>(m_undo_stack[m_undo_index].get());
				if (end_group->group_type == type && !end_group->locked)
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


	void lockGroupCommand() override {
		ASSERT(m_undo_index >= 0);
		ASSERT(m_undo_index < m_undo_stack.size());
		ASSERT(equalStrings(m_undo_stack.last()->getType(), "end_group"));
		((EndGroupCommand*)m_undo_stack.last().get())->locked = true;
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
		m_is_world_changed = true;
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
		ASSERT(m_world);
		if (m_is_game_mode) {
			stopGameMode(true);
			return;
		}

		m_selected_entity_on_game_mode = m_selected_entities.empty() ? INVALID_ENTITY : m_selected_entities[0];
		m_selected_folder_on_game_mode = m_entity_folders->getSelectedFolder();
		m_game_mode_file.clear();
		save(m_game_mode_file, true);
		m_is_game_mode = true;
		beginCommandGroup("");
		endCommandGroup();
		m_game_mode_commands = 2;
		m_engine.startGame(*m_world);
	}


	void stopGameMode(bool reload)
	{
		for (int i = 0; i < m_game_mode_commands; ++i)
		{
			m_undo_stack.pop();
			--m_undo_index;
		}

		ASSERT(m_world);
		m_engine.getResourceManager().enableUnload(false);
		m_engine.stopGame(*m_world);
		selectEntities({}, false);
		m_is_game_mode = false;
		if (reload)
		{
			m_world_destroyed.invoke();
			m_prefab_system->setWorld(nullptr);
			m_entity_folders.destroy();
			m_engine.destroyWorld(*m_world);
			
			m_world = &m_engine.createWorld(true);
			m_entity_folders.create(*m_world, m_allocator);
			m_prefab_system->setWorld(m_world);
			m_world_created.invoke();
			m_world->entityDestroyed().bind<&WorldEditorImpl::onEntityDestroyed>(this);
			m_selected_entities.clear();
			
			InputMemoryStream blob(m_game_mode_file);
			loadWorld(blob, "game mode", false, true);
		}
		m_game_mode_file.clear();
		if(m_selected_entity_on_game_mode.isValid()) {
			EntityRef e = (EntityRef)m_selected_entity_on_game_mode;
			selectEntities(Span(&e, 1), false);
		}
		m_entity_folders->selectFolder(m_selected_folder_on_game_mode);
		m_engine.getResourceManager().enableUnload(true);
	}
	
	void moveEntityToFolder(EntityRef entity, EntityFolders::FolderHandle folder) override {
		UniquePtr<IEditorCommand> command = UniquePtr<MoveEntityToFolderCommand>::create(m_allocator, *this, entity, folder);
		executeCommand(command.move());
	}

	void renameEntityFolder(EntityFolders::FolderHandle folder, const char* new_name) override {
		UniquePtr<IEditorCommand> command = UniquePtr<RenameEntityFolderCommand>::create(m_allocator, *this, folder, new_name);
		executeCommand(command.move());
	}
	
	EntityFolders::FolderHandle createEntityFolder(EntityFolders::FolderHandle parent) override {
		EntityFolders::FolderHandle res;
		UniquePtr<IEditorCommand> command = UniquePtr<CreateEntityFolderCommand>::create(m_allocator, *this, parent, &res);
		executeCommand(command.move());
		return res;
	}

	void moveEntityFolder(EntityFolders::FolderHandle folder, EntityFolders::FolderHandle new_parent) {
		UniquePtr<IEditorCommand> command = UniquePtr<MoveEntityFolderCommand>::create(m_allocator, *this, folder, new_parent);
		executeCommand(command.move());
	}

	void destroyEntityFolder(EntityFolders::FolderHandle folder) override {
		beginCommandGroup("destroy_entity_folder");
		
		for (;;) {
			const EntityFolders::Folder& f = m_entity_folders->getFolder(folder);
			ASSERT(f.parent != EntityFolders::INVALID_FOLDER);
			if (f.first_child == EntityFolders::INVALID_FOLDER) break;

			moveEntityFolder(f.first_child, f.parent);
		}

		const EntityFolders::Folder& f = m_entity_folders->getFolder(folder);
		while (f.first_entity.isValid()) {
			moveEntityToFolder(*f.first_entity, f.parent);
		}

		UniquePtr<IEditorCommand> command = UniquePtr<DestroyEntityFolderCommand>::create(m_allocator, *this, folder);
		executeCommand(command.move());

		endCommandGroup();
	}

	EntityFolders& getEntityFolders() override {
		return *m_entity_folders.get();
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
			Transform tr = m_world->getTransform(entity);
			serializer.write(tr);
			serializer.writeString(m_world->getEntityName(entity));
			serializer.write(m_world->getParent(entity));

			for (ComponentUID cmp = m_world->getFirstComponent(entity);
				cmp.isValid();
				cmp = m_world->getNextComponent(cmp))
			{
				const RuntimeHash cmp_type(reflection::getComponent(cmp.type)->name);
				serializer.write(cmp_type);
				const reflection::ComponentBase* cmp_desc = reflection::getComponent(cmp.type);
				
				PropertySerializeVisitor visitor(serializer, cmp);
				visitor.idx = -1;
				cmp_desc->visit(visitor);
			}
			serializer.write(RuntimeHash::fromU64(0));
		}
	}

	void gatherHierarchy(EntityRef e, Array<EntityRef>& entities) {
		entities.push(e);
		for (EntityRef child : m_world->childrenOf(e)) {
			if (entities.indexOf(child) < 0) {
				gatherHierarchy(child, entities);
			}
		}
	}


	void copyEntities() override {
		if (m_selected_entities.empty()) return;

		m_copy_buffer.clear();

		Array<EntityRef> entities(m_allocator);
		entities.reserve(m_selected_entities.size());
		for (EntityRef e : m_selected_entities) {
			gatherHierarchy(e, entities);
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
		FileSystem& fs = m_engine.getFileSystem();
		OutputMemoryStream blob(m_allocator);
		m_engine.serializeProject(blob, Path("main.unv"));

		if (!fs.saveContentSync(Path("lumix.prj"), blob)) {
			logError("Failed to save lumix.prj");
		}
	}

	void loadProject() override {
		OutputMemoryStream data(m_allocator);
		if (!m_engine.getFileSystem().getContentSync(Path("lumix.prj"), data)) {
			logWarning("Project file not found");
			return;
		}
		
		InputMemoryStream stream(data);
		Path dummy;
		
		const DeserializeProjectResult res = m_engine.deserializeProject(stream, dummy);
		switch (res) {
			case DeserializeProjectResult::SUCCESS: break;
			case DeserializeProjectResult::PLUGIN_DESERIALIZATION_FAILED: logError("Project file: Plugin deserialization failed"); break;
			case DeserializeProjectResult::PLUGIN_NOT_FOUND: logError("Project file: Plugin not found"); break;
			case DeserializeProjectResult::VERSION_NOT_SUPPORTED: logError("Project file: version not supported"); break;
			case DeserializeProjectResult::PLUGIN_VERSION_NOT_SUPPORTED: logError("Project file: plugin version not supported"); break;
			case DeserializeProjectResult::CORRUPTED_FILE: logError("Project file: corrupted"); break;
		}
	}
	
	bool isLoading() const override { return m_is_loading; }

	void newWorld() override {
		destroyWorld();
		createWorld();
		logInfo("World created.");
	}
	
	void loadWorld(InputMemoryStream& blob, const char* basename, bool additive) override {
		ASSERT(!m_is_game_mode);
		logInfo("Loading world ", basename, "...");
		
		if (additive) {
			World::PartitionHandle partition = m_world->createPartition(basename);
			m_world->setActivePartition(partition);
		}
		else {
			destroyWorld();
			createWorld();
			copyString(m_world->getPartitions()[0].name, basename);
		}

		if (!loadWorld(blob, basename, additive, false)) {
			newWorld();
			return;
		}

		World::PartitionHandle partition = m_world->getActivePartition();
		EntityFolders::FolderHandle root_folder = m_entity_folders->getRoot(partition);
		m_entity_folders->selectFolder(root_folder);
	}

	bool loadWorld(InputMemoryStream& blob, const char* name, bool additive, bool is_game_mode_load) {
		PROFILE_FUNCTION();
		m_is_loading = true;

		const u64 file_size = blob.size();
		if (file_size > 0xffFFffFF) {
			logError("File too big.");
			m_is_loading = false;
			return false;
		}

		os::Timer timer;
		logInfo("Parsing world...");

		EntityMap entity_map(m_allocator);
		m_entity_folders->ignoreNewEntities(true);
		WorldVersion editor_header_version;
		if (m_world->deserialize(blob, entity_map, editor_header_version)) {
			m_entity_folders->ignoreNewEntities(false);
			m_prefab_system->deserialize(blob, entity_map, editor_header_version);

			if (editor_header_version > WorldVersion::ENTITY_FOLDERS) {
				Array<EntityFolders::Folder>& folders = m_entity_folders->getFolders();
				const u32 offset = folders.size();
				m_entity_folders->deserialize(blob, entity_map, additive, editor_header_version);

				if (!is_game_mode_load) {
					const World::PartitionHandle partition = m_world->getActivePartition();
					for (u32 i = offset, c = (u32)folders.size(); i < c; ++i) {
						folders[i].partition = partition;
					}
				}
			}

			if (editor_header_version > WorldVersion::EDITOR_CAMERA) {
				DVec3 pos;
				Quat rot;
				blob.read(pos);
				blob.read(rot);
				if (m_view && !additive) {
					Viewport vp = m_view->getViewport();
					vp.pos = pos;
					vp.rot = rot;
					m_view->setViewport(vp);
				}
			}
			logInfo("World parsed in ", timer.getTimeSinceStart(), " seconds");
			m_view->refreshIcons();
			m_is_loading = false;
			return true;
		}

		m_entity_folders->ignoreNewEntities(false);
		newWorld();
		m_is_loading = false;
		return false;
	}


	WorldEditorImpl(Engine& engine, IAllocator& allocator)
		: m_allocator(allocator, "world editor")
		, m_world_destroyed(m_allocator)
		, m_world_created(m_allocator)
		, m_selected_entities(m_allocator)
		, m_entity_selection_changed(m_allocator)
		, m_undo_stack(m_allocator)
		, m_copy_buffer(m_allocator)
		, m_is_loading(false)
		, m_world(nullptr)
		, m_selected_entity_on_game_mode(INVALID_ENTITY)
		, m_is_game_mode(false)
		, m_undo_index(-1)
		, m_engine(engine)
		, m_game_mode_file(m_allocator)
	{
		loadProject();
		logInfo("Initializing editor...");

		m_prefab_system = PrefabSystem::create(*this);
		createWorld();
	}


	bool isEntitySelected(EntityRef entity) const override
	{
		return m_selected_entities.indexOf(entity) >= 0;
	}


	const Array<EntityRef>& getSelectedEntities() const override
	{
		return m_selected_entities;
	}


	struct PropertyCloner : reflection::IPropertyVisitor {
		template <typename T>
		void clone(const reflection::Property<T>& prop) { 
			if (!prop.setter) return;
			prop.set(dst, index, prop.get(src, index));
		}

		void visit(const reflection::Property<float>& prop) override { clone(prop); }
		void visit(const reflection::Property<int>& prop) override { clone(prop); }
		void visit(const reflection::Property<u32>& prop) override { clone(prop); }
		void visit(const reflection::Property<EntityPtr>& prop) override { 
			if (!prop.setter) return;

			EntityPtr e = prop.get(src, index);
			auto iter = map->find(e);
			if (iter.isValid()) {
				e = iter.value();
			}
			else {
				e = INVALID_ENTITY;
			}
			prop.set(dst, index, e);
		}
		void visit(const reflection::Property<Vec2>& prop) override { clone(prop); }
		void visit(const reflection::Property<Vec3>& prop) override { clone(prop); }
		void visit(const reflection::Property<IVec3>& prop) override { clone(prop); }
		void visit(const reflection::Property<Vec4>& prop) override { clone(prop); }
		void visit(const reflection::Property<Path>& prop) override { clone(prop); }
		void visit(const reflection::Property<bool>& prop) override { clone(prop); }
		void visit(const reflection::Property<const char*>& prop) override { clone(prop); }
		
		void visit(const reflection::ArrayProperty& prop) override {
			const u32 c = prop.getCount(src);
			while (prop.getCount(dst) < c) { prop.addItem(dst, prop.getCount(dst) - 1); }
			while (prop.getCount(dst) > c) { prop.removeItem(dst, prop.getCount(dst) - 1); }
			
			ASSERT(index == -1);
			for (u32 i = 0; i < c; ++i) {
				index = i;
				prop.visitChildren(*this);
			}
			index = -1;
		}
		
		void visit(const reflection::DynamicProperties& prop) override { 
			for (u32 i = 0, c = prop.getCount(src, index); i < c; ++i) {
				const char* name = prop.getName(src, index, i);
				reflection::DynamicProperties::Type type = prop.getType(src, index, i);
				reflection::DynamicProperties::Value val = prop.getValue(src, index, i);
				if (type == reflection::DynamicProperties::ENTITY) {
					auto iter = map->find(val.e);
					if (iter.isValid()) {
						val.e = iter.value();
					}
					else {
						val.e = INVALID_ENTITY;
					}
				}
				prop.set(dst, index, name, type, val);
			}
		}
		

		void visit(const reflection::BlobProperty& prop) override { 
			OutputMemoryStream tmp(*allocator);
			prop.getValue(src, index, tmp);
			InputMemoryStream blob(tmp);
			prop.setValue(dst, index, blob);
		}
		

		const HashMap<EntityPtr, EntityPtr>* map; 
		IAllocator* allocator;
		ComponentUID src;
		ComponentUID dst;
		int index = -1;
	};


	EntityRef cloneEntity(World& src_world, EntityRef src_e, World& dst_world, EntityPtr dst_parent, Array<EntityRef>& entities, const HashMap<EntityPtr, EntityPtr>& map) override {
		entities.push(src_e);
		const EntityRef dst_e = (EntityRef)map[src_e];
		if (dst_parent.isValid()) {
			dst_world.setParent(dst_parent, dst_e);
			dst_world.setLocalTransform(dst_e, src_world.getLocalTransform(src_e));
		}
		else {
			dst_world.setTransform(dst_e, src_world.getTransform(src_e));
		}
		const char* name = src_world.getEntityName(src_e);
		if (name[0]) {
			dst_world.setEntityName(dst_e, name);
		}

		const EntityPtr c = src_world.getFirstChild(src_e);
		if (c.isValid()) {
			cloneEntity(src_world, (EntityRef)c, dst_world, dst_e, entities, map);
		}

		if (dst_parent.isValid()) {
			const EntityPtr s = src_world.getNextSibling(src_e);
			if (s.isValid()) {
				cloneEntity(src_world, (EntityRef)s, dst_world, dst_parent, entities, map);
			}
		}

		for (ComponentUID cmp = src_world.getFirstComponent(src_e); cmp.isValid(); cmp = src_world.getNextComponent(cmp)) {
			dst_world.createComponent(cmp.type, dst_e);

			const reflection::ComponentBase* cmp_tpl = reflection::getComponent(cmp.type);
	
			PropertyCloner property_cloner;
			property_cloner.allocator = &m_allocator;
			property_cloner.src = cmp;
			property_cloner.dst.type = cmp.type;
			property_cloner.dst.entity = dst_e;
			property_cloner.dst.module = dst_world.getModule(cmp.type);
			property_cloner.map = &map;
			cmp_tpl->visit(property_cloner);
		}

		return dst_e;
	}


	void cloneHierarchy(const World& src, EntityRef src_e, World& dst, bool clone_siblings, HashMap<EntityPtr, EntityPtr>& map) {
		const EntityPtr child = src.getFirstChild(src_e);
		const EntityPtr sibling = src.getNextSibling(src_e);

		const EntityRef dst_e = dst.createEntity({0, 0, 0}, Quat::IDENTITY);
		map.insert(src_e, dst_e);

		if (child.isValid()) {
			cloneHierarchy(src, (EntityRef)child, dst, true, map);
		}
		if (clone_siblings && sibling.isValid()) {
			cloneHierarchy(src, (EntityRef)sibling, dst, true, map);
		}
	}

	UniquePtr<WorldEditorImpl> createPartitionWorld(World::PartitionHandle partition) {
		UniquePtr<WorldEditorImpl> res = WorldEditor::create(m_engine, m_allocator);

		World& dst = *res->getWorld();
		World& src = *m_world;

		Array<EntityRef> entities(m_allocator);
		HashMap<EntityPtr, EntityPtr> map(m_allocator);
		map.reserve(256);
		for (EntityPtr e = m_world->getFirstEntity(); e.isValid(); e = m_world->getNextEntity(*e)) {
			if (m_world->getPartition(*e) == partition && !m_world->getParent(*e).isValid()) {
				cloneHierarchy(src, *e, dst, false, map);
			}
		}
		for (EntityPtr e = m_world->getFirstEntity(); e.isValid(); e = m_world->getNextEntity(*e)) {
			if (m_world->getPartition(*e) == partition && !m_world->getParent(*e).isValid()) {
				cloneEntity(src, *e, dst, INVALID_ENTITY, entities, map);
			}
		}

		m_entity_folders->cloneTo(*res->m_entity_folders, partition, map);
		m_prefab_system->cloneTo(*res->m_prefab_system, map);

		return res.move();
	}


	void serializeWorldPartition(World::PartitionHandle partition, OutputMemoryStream& blob) override {
		UniquePtr<WorldEditorImpl> ed = createPartitionWorld(partition);
		ed->save(blob, false);
	}

	void destroyWorldPartition(World::PartitionHandle partition) override {
		m_world->destroyPartition(partition);
		m_entity_folders->destroyPartitionFolders(partition);
		clearUndoStack();
		m_is_world_changed = true;
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

	static void fastRemoveDuplicates(Array<EntityRef>& entities) {
		qsort(entities.begin(), entities.size(), sizeof(entities[0]), [](const void* a, const void* b){
			return memcmp(a, b, sizeof(EntityRef));
		});
		for (i32 i = entities.size() - 2; i >= 0; --i) {
			if (entities[i] == entities[i + 1]) entities.swapAndPop(i);
		}
	}

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
		fastRemoveDuplicates(m_selected_entities);
		m_entity_selection_changed.invoke();
	}


	void onEntityDestroyed(EntityRef entity)
	{
		m_selected_entities.swapAndPopItem(entity);
	}


	void destroyWorld() {
		if (m_is_game_mode) stopGameMode(false);

		ASSERT(m_world);
		clearUndoStack();
		m_entity_folders.destroy();
		m_world_destroyed.invoke();
		m_prefab_system->setWorld(nullptr);
		selectEntities({}, false);
		m_engine.destroyWorld(*m_world);
		m_world = nullptr;
	}


	DelegateList<void()>& worldCreated() override
	{
		return m_world_created;
	}


	DelegateList<void()>& worldDestroyed() override
	{
		return m_world_destroyed;
	}

	DelegateList<void()>& entitySelectionChanged() override {
		return m_entity_selection_changed;
	}

	void clearUndoStack()
	{
		m_undo_index = -1;
		m_undo_stack.clear();
	}


	void createWorld()
	{
		ASSERT(!m_world);

		m_is_world_changed = false;
		clearUndoStack();
		m_world = &m_engine.createWorld(true);
		World* world = m_world;

		world->entityDestroyed().bind<&WorldEditorImpl::onEntityDestroyed>(this);
		m_entity_folders.create(*m_world, m_allocator);

		m_selected_entities.clear();
		m_world_created.invoke();
		m_prefab_system->setWorld(world);
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

		if (m_undo_index >= m_undo_stack.size() || m_undo_index < 0) return;

		if (equalStrings(m_undo_stack[m_undo_index]->getType(), "end_group"))
		{
			--m_undo_index;
			while (!equalStrings(m_undo_stack[m_undo_index]->getType(), "begin_group"))
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

		if (m_undo_index + 1 >= m_undo_stack.size()) return;

		++m_undo_index;
		if(equalStrings(m_undo_stack[m_undo_index]->getType(), "begin_group"))
		{
			++m_undo_index;
			while(!equalStrings(m_undo_stack[m_undo_index]->getType(), "end_group"))
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


	static int getEntitiesCount(World& world)
	{
		int count = 0;
		for (EntityPtr e = world.getFirstEntity(); e.isValid(); e = world.getNextEntity((EntityRef)e)) ++count;
		return count;
	}

private:
	TagAllocator m_allocator;
	Engine& m_engine;
	WorldView* m_view = nullptr;
	UniquePtr<PrefabSystem> m_prefab_system;
	Local<EntityFolders> m_entity_folders;
	World* m_world;
	bool m_is_loading;
	bool m_is_world_changed;
	
	Array<UniquePtr<IEditorCommand>> m_undo_stack;
	int m_undo_index;
	RuntimeHash m_current_group_type;

	Array<EntityRef> m_selected_entities;
	EntityPtr m_selected_entity_on_game_mode;
	EntityFolders::FolderHandle m_selected_folder_on_game_mode;

	bool m_is_game_mode;
	int m_game_mode_commands;
	OutputMemoryStream m_game_mode_file;
	DelegateList<void()> m_world_destroyed;
	DelegateList<void()> m_world_created;
	DelegateList<void()> m_entity_selection_changed;

	OutputMemoryStream m_copy_buffer;
};


struct PasteEntityCommand final : IEditorCommand
{
public:
	PasteEntityCommand(WorldEditorImpl& editor, const OutputMemoryStream& copy_buffer, bool identity = false)
		: m_copy_buffer(copy_buffer)
		, m_editor(editor)
		, m_entities(editor.getAllocator())
		, m_map(editor.getAllocator())
		, m_identity(identity)
	{
		WorldView& view = editor.getView();
		const WorldView::RayHit hit = view.getCameraRaycastHit(view.getViewport().w >> 1, view.getViewport().h >> 1, INVALID_ENTITY);
		m_position = hit.pos;
	}

	bool execute() override
	{
		InputMemoryStream blob(m_copy_buffer);

		World& world = *m_editor.getWorld();
		int entity_count;
		blob.read(entity_count);
		bool is_redo = !m_entities.empty();
		if (is_redo)
		{
			for (int i = 0; i < entity_count; ++i) {
				world.emplaceEntity(m_entities[i]);
			}
		}
		else {
			m_entities.resize(entity_count);
			for (int i = 0; i < entity_count; ++i) {
				m_entities[i] = world.createEntity(DVec3(0), Quat(0, 0, 0, 1));
			}
		}
		m_editor.selectEntities(m_entities, false);

		Transform base_tr;
		base_tr.pos = m_position;
		base_tr.scale = Vec3(1);
		base_tr.rot = Quat(0, 0, 0, 1);
		m_map.reserve(entity_count);
		for (int i = 0; i < entity_count; ++i) {
			EntityRef orig_e;
			blob.read(orig_e);
			if (!is_redo) m_map.insert(orig_e, i);
		}
		for (int i = 0; i < entity_count; ++i)
		{
			Transform tr;
			blob.read(tr);
			const char* name = blob.readString();
			if (name[0]) world.setEntityName(m_entities[i], name);
			EntityPtr parent;
			blob.read(parent);

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
			world.setTransform(new_entity, tr);
			world.setParent(parent, new_entity);
			for (;;) {
				RuntimeHash hash;
				blob.read(hash);
				if (hash.getHashValue() == 0) break;

				ComponentUID cmp;
				cmp.entity = new_entity;
				cmp.type = reflection::getComponentTypeFromHash(hash);
				cmp.module = world.getModule(cmp.type);

				world.createComponent(cmp.type, new_entity);

				PropertyDeserializeVisitor visitor(blob, cmp, m_map, m_entities);
				visitor.idx = -1;
				reflection::getComponent(cmp.type)->visit(visitor);
			}
		}
		return true;
	}


	void undo() override
	{
		for (auto entity : m_entities) {
			m_editor.getWorld()->destroyEntity(entity);
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
