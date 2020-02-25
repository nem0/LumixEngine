#include "engine/reflection.h"
#include "engine/crc32.h"
#include "engine/allocator.h"
#include "engine/hash_map.h"
#include "engine/log.h"


namespace Lumix
{


namespace Reflection
{


namespace detail
{


template <> Path readFromStream<Path>(InputMemoryStream& stream)
{
	const char* c_str = (const char*)stream.getData() + stream.getPosition();
	Path path(c_str);
	stream.skip(stringLength(c_str) + 1);
	return path;
}


template <> void writeToStream<const Path&>(OutputMemoryStream& stream, const Path& path)
{
	const char* str = path.c_str();
	stream.write(str, stringLength(str) + 1);
}


template <> void writeToStream<Path>(OutputMemoryStream& stream, Path path)
{
	const char* str = path.c_str();
	stream.write(str, stringLength(str) + 1);
}


template <> const char* readFromStream<const char*>(InputMemoryStream& stream)
{
	const char* c_str = (const char*)stream.getData() + stream.getPosition();
	stream.skip(stringLength(c_str) + 1);
	return c_str;
}


template <> void writeToStream<const char*>(OutputMemoryStream& stream, const char* value)
{
	stream.write(value, stringLength(value) + 1);
}


}

struct ComponentTypeData
{
	char id[50];
	u32 id_hash;
};


static IAllocator* g_allocator = nullptr;
static Array<const EnumBase*>* g_enums = nullptr;


struct ComponentLink
{
	const ComponentBase* desc;
	ComponentLink* next;
};


static ComponentLink* g_first_component = nullptr;


struct ComponentBase
{
	virtual ~ComponentBase() {}

	virtual int getPropertyCount() const = 0;
	virtual int getFunctionCount() const = 0;

	const char* name;
	ComponentType component_type;
};


const char* getComponentName(ComponentType cmp_type)
{
	ComponentLink* link = g_first_component;
	while (link)
	{
		if (link->desc->component_type == cmp_type) return link->desc->name;
		link = link->next;
	}

	return "Unknown component";
}

struct GetPropertyVisitor : IComponentVisitor {
	template <typename T>
	void get(const Prop<T>& prop) {
		if (equalStrings(prop.name, prop_name)) {
			T val = prop.get();
			memcpy(value.begin(), &val, minimum(sizeof(val), value.length()));
			found = true;
		}
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
		if (equalStrings(prop.name, prop_name)) {
			Path p = prop.get();
			copyString(Span((char*)value.begin(), value.length()), p.c_str());
			found = true;
		}
	}

	void visit(const Prop<const char*>& prop) override { 
		if (equalStrings(prop.name, prop_name)) {
			copyString(Span((char*)value.begin(), value.length()), prop.get());
			found = true;
		}
	}

	bool beginArray(const char* name, const ArrayProp& prop) override { return false; }

	const char* prop_name;
	Span<u8> value;
	bool found = false;
};

struct PropertyDeserializeVisitor : IComponentVisitor {
	PropertyDeserializeVisitor(const HashMap<EntityPtr, u32>& map, Span<const EntityRef> entities)
		: map(map)
		, entities(entities)
	{}

	template <typename T> 
	void set(const Prop<T>& prop) {
		prop.set(blob->read<T>());
	}

	void visit(const Prop<float>& prop) override { set(prop); }
	void visit(const Prop<i32>& prop) override { set(prop); }
	void visit(const Prop<u32>& prop) override { set(prop); }
	void visit(const Prop<Vec2>& prop) override { set(prop); }
	void visit(const Prop<Vec3>& prop) override { set(prop); }
	void visit(const Prop<IVec3>& prop) override { set(prop); }
	void visit(const Prop<Vec4>& prop) override { set(prop); }
	void visit(const Prop<bool>& prop) override { set(prop); }
	
	void visit(const Prop<EntityPtr>& prop) override { 
		EntityPtr value;
		blob->read(Ref(value));
		auto iter = map.find(value);
		if (iter.isValid()) value = entities[iter.value()];
		prop.set(value);
	}
	
	void visit(const Prop<const char*>& prop) override 
	{
		// TODO support bigger strings
		char tmp[1024];
		blob->readString(Span(tmp));
		prop.set(tmp);
	}
	
	void visit(const Prop<Path>& prop) override {
		char tmp[MAX_PATH_LENGTH];
		blob->readString(Span(tmp));
		Path path(tmp);
		prop.set(path);
	}

	bool beginArray(const char* name, const ArrayProp& prop) override {
		const u32 wanted = blob->read<u32>();
		while (prop.count() > wanted) {
			prop.remove(prop.count() - 1);
		}
		while (prop.count() < wanted) {
			prop.add(prop.count());
		}
		return true;
	}

	const HashMap<EntityPtr, u32>& map;
	Span<const EntityRef> entities;
	IInputStream* blob;
};

struct PropertySerializeVisitor : IComponentVisitor {
	void visit(const Prop<float>& prop) override { serializer->write(prop.get()); }
	void visit(const Prop<i32>& prop) override { serializer->write(prop.get()); }
	void visit(const Prop<u32>& prop) override { serializer->write(prop.get()); }
	void visit(const Prop<Vec2>& prop) override { serializer->write(prop.get()); }
	void visit(const Prop<Vec3>& prop) override { serializer->write(prop.get()); }
	void visit(const Prop<IVec3>& prop) override { serializer->write(prop.get()); }
	void visit(const Prop<Vec4>& prop) override { serializer->write(prop.get()); }
	void visit(const Prop<bool>& prop) override { serializer->write(prop.get()); }
	void visit(const Prop<const char*>& prop) override { serializer->writeString(prop.get()); }
	
	void visit(const Prop<EntityPtr>& prop) override { serializer->write(prop.get().index); }


	void visit(const Prop<Path>& prop) override { 
		serializer->writeString(prop.get().c_str()); 
	}

	bool beginArray(const char* name, const ArrayProp& prop) override {
		const u32 c = prop.count();
		serializer->write(c);
		return true;
	}

	IOutputStream* serializer;
};

struct SetPropertyVisitor : IComponentVisitor {
	template <typename T>
	void get(const Prop<T>& prop) {
		if (equalStrings(prop.name, prop_name)) {
			T val;
			memcpy(&val, value.begin(), minimum(sizeof(val), value.length()));
			prop.set(val);
		}
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
		if (equalStrings(prop.name, prop_name)) {
			char tmp[MAX_PATH_LENGTH];
			memcpy(tmp, value.begin(), minimum(value.length(), sizeof(tmp)));
			prop.set(Path(tmp));
		}
	}

	void visit(const Prop<const char*>& prop) override { 
		if (equalStrings(prop.name, prop_name)) {
			// TODO bigger strings
			char tmp[4096];
			memcpy(tmp, value.begin(), minimum(value.length(), sizeof(tmp)));
			tmp[4095] = '\0';
			prop.set(tmp);
		}
	}

	bool beginArray(const char* name, const ArrayProp& prop) override { return false; }

	const char* prop_name;
	Span<const u8> value;
};

void setProperty(IScene& scene, EntityRef e, ComponentType cmp_type, const char* property, Span<const u8> in) {
	SetPropertyVisitor v;
	v.value = in;
	v.prop_name = property;
	scene.visit(e, cmp_type, v);
}

void serializeComponent(IScene& scene, EntityRef e, ComponentType cmp_type, Ref<IOutputStream> out) {
	PropertySerializeVisitor v;
	v.serializer = &out.value;
	scene.visit(e, cmp_type, v);
}

void deserializeComponent(IScene& scene
	, EntityRef e
	, ComponentType cmp_type
	, const HashMap<EntityPtr, u32>& map
	, Span<const EntityRef> entities
	, Ref<IInputStream> in)
{
	PropertyDeserializeVisitor v(map, entities);
	v.blob = &in.value;
	scene.visit(e, cmp_type, v);
}

bool getProperty(IScene& scene, EntityRef e, ComponentType cmp_type, const char* property, Span<u8> out) {
	GetPropertyVisitor v;
	v.value = out;
	v.prop_name = property;
	scene.visit(e, cmp_type, v);
	return v.found;
}


static Array<ComponentTypeData>& getComponentTypes()
{
	static DefaultAllocator allocator;
	static Array<ComponentTypeData> types(allocator);
	return types;
}


void init(IAllocator& allocator)
{
	g_allocator = &allocator;
	g_enums = LUMIX_NEW(allocator, Array<const EnumBase*>)(allocator);
}


static void destroy(ComponentLink* link)
{
	if (!link) return;
	destroy(link->next);
	LUMIX_DELETE(*g_allocator, link);
}


void shutdown()
{
	destroy(g_first_component);
	LUMIX_DELETE(*g_allocator, g_enums);
	g_allocator = nullptr;
}


ComponentType getComponentTypeFromHash(u32 hash)
{
	auto& types = getComponentTypes();
	for (int i = 0, c = types.size(); i < c; ++i)
	{
		if (types[i].id_hash == hash)
		{
			return {i};
		}
	}
	ASSERT(false);
	return {-1};
}


u32 getComponentTypeHash(ComponentType type)
{
	return getComponentTypes()[type.index].id_hash;
}


ComponentType getComponentType(const char* id)
{
	u32 id_hash = crc32(id);
	auto& types = getComponentTypes();
	for (int i = 0, c = types.size(); i < c; ++i)
	{
		if (types[i].id_hash == id_hash)
		{
			return {i};
		}
	}

	auto& cmp_types = getComponentTypes();
	if (types.size() == ComponentType::MAX_TYPES_COUNT)
	{
		logError("Engine") << "Too many component types";
		return INVALID_COMPONENT_TYPE;
	}

	ComponentTypeData& type = cmp_types.emplace();
	copyString(type.id, id);
	type.id_hash = id_hash;
	return {getComponentTypes().size() - 1};
}


int getComponentTypesCount()
{
	return getComponentTypes().size();
}


const char* getComponentTypeID(int index)
{
	return getComponentTypes()[index].id;
}


int getEnumsCount()
{
	return g_enums->size();
}


const EnumBase& getEnum(int index)
{
	return *(*g_enums)[index];
}


} // namespace Reflection


} // namespace Lumix
