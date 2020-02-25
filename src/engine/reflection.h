#pragma once


#include "engine/hash_map.h"
#include "engine/lumix.h"
#include "engine/metaprogramming.h"
#include "engine/resource.h"
#include "engine/stream.h"
#include "engine/string.h"
#include "engine/universe.h"


#define LUMIX_PROP(Scene, Property) &Scene::get##Property, &Scene::set##Property
#define LUMIX_ENUM_VALUE(value) EnumValue(#value, (int)value)
#define LUMIX_FUNC(Func)\
	&Func, #Func

namespace Lumix
{

template <typename T> struct Array;
struct IAllocator;
struct Path;
struct PropertyDescriptorBase;
struct IVec2;
struct IVec3;
struct Vec2;
struct Vec3;
struct Vec4;


namespace Reflection
{


struct IAttribute
{
	enum Type
	{
		MIN,
		CLAMP,
		RADIANS,
		COLOR,
		RESOURCE,
		ENUM
	};

	virtual ~IAttribute() {}
	virtual int getType() const = 0;
};

struct ComponentBase;
struct SceneBase;
struct EnumBase;

struct PropertyBase
{
	virtual ~PropertyBase() {}

	const char* name;
};


LUMIX_ENGINE_API void init(IAllocator& allocator);
LUMIX_ENGINE_API void shutdown();

LUMIX_ENGINE_API void registerEnum(const EnumBase& e);
LUMIX_ENGINE_API void registerScene(const SceneBase& scene);
LUMIX_ENGINE_API const char* getComponentName(ComponentType cmp_type);
LUMIX_ENGINE_API bool getProperty(IScene& scene, EntityRef e, ComponentType cmp_type, const char* property, Span<u8> out);
LUMIX_ENGINE_API void setProperty(IScene& scene, EntityRef e, ComponentType cmp_type, const char* property, Span<const u8> in);
LUMIX_ENGINE_API void serializeComponent(IScene& scene, EntityRef e, ComponentType cmp_type, Ref<IOutputStream> out);
LUMIX_ENGINE_API void deserializeComponent(IScene& scene
	, EntityRef e
	, ComponentType cmp_type
	, const HashMap<EntityPtr, u32>& map
	, Span<const EntityRef> entities
	, Ref<IInputStream> in);

LUMIX_ENGINE_API ComponentType getComponentType(const char* id);
LUMIX_ENGINE_API u32 getComponentTypeHash(ComponentType type);
LUMIX_ENGINE_API ComponentType getComponentTypeFromHash(u32 hash);
LUMIX_ENGINE_API int getComponentTypesCount();
LUMIX_ENGINE_API const char* getComponentTypeID(int index);


namespace detail 
{


template <typename T> void writeToStream(OutputMemoryStream& stream, T value)
{
	stream.write(value);
}
	
template <typename T> T readFromStream(InputMemoryStream& stream)
{
	return stream.read<T>();
}

template <> LUMIX_ENGINE_API Path readFromStream<Path>(InputMemoryStream& stream);
template <> LUMIX_ENGINE_API void writeToStream<Path>(OutputMemoryStream& stream, Path);
template <> LUMIX_ENGINE_API void writeToStream<const Path&>(OutputMemoryStream& stream, const Path& path);
template <> LUMIX_ENGINE_API const char* readFromStream<const char*>(InputMemoryStream& stream);
template <> LUMIX_ENGINE_API void writeToStream<const char*>(OutputMemoryStream& stream, const char* path);


template <typename Getter> struct GetterProxy;

template <typename R, typename C>
struct GetterProxy<R(C::*)(EntityRef, int)>
{
	using Getter = R(C::*)(EntityRef, int);
	static void invoke(OutputMemoryStream& stream, C* inst, Getter getter, EntityRef entity, int index)
	{
		R value = (inst->*getter)(entity, index);
		writeToStream(stream, value);
	}
};

template <typename R, typename C>
struct GetterProxy<R(C::*)(EntityRef)>
{
	using Getter = R(C::*)(EntityRef);
	static void invoke(OutputMemoryStream& stream, C* inst, Getter getter, EntityRef entity, int index)
	{
		R value = (inst->*getter)(entity);
		writeToStream(stream, value);
	}
};


template <typename Setter> struct SetterProxy;

template <typename C, typename A>
struct SetterProxy<void (C::*)(EntityRef, int, A)>
{
	using Setter = void (C::*)(EntityRef, int, A);
	static void invoke(InputMemoryStream& stream, C* inst, Setter setter, EntityRef entity, int index)
	{
		using Value = RemoveCR<A>;
		auto value = readFromStream<Value>(stream);
		(inst->*setter)(entity, index, value);
	}
};

template <typename C, typename A>
struct SetterProxy<void (C::*)(EntityRef, A)>
{
	using Setter = void (C::*)(EntityRef, A);
	static void invoke(InputMemoryStream& stream, C* inst, Setter setter, EntityRef entity, int index)
	{
		using Value = RemoveCR<A>;
		auto value = readFromStream<Value>(stream);
		(inst->*setter)(entity, value);
	}
};


} // namespace detail


struct ResourceAttribute : IAttribute
{
	ResourceAttribute(const char* file_type, ResourceType type) { this->file_type = file_type; this->type = type; }
	ResourceAttribute() {}

	int getType() const override { return RESOURCE; }

	const char* file_type;
	ResourceType type;
};


struct MinAttribute : IAttribute
{
	explicit MinAttribute(float min) { this->min = min; }
	MinAttribute() {}

	int getType() const override { return MIN; }

	float min;
};


struct EnumAttribute : IAttribute {
	virtual u32 count() const = 0;
	virtual const char* getName(u32 value) const = 0;

	int getType() const override { return ENUM; }
};


struct ClampAttribute : IAttribute
{
	ClampAttribute(float min, float max) { this->min = min; this->max = max; }
	ClampAttribute() {}

	int getType() const override { return CLAMP; }

	float min;
	float max;
};


struct RadiansAttribute : IAttribute
{
	int getType() const override { return RADIANS; }
};


struct ColorAttribute : IAttribute
{
	int getType() const override { return COLOR; }
};


namespace internal
{
	static const unsigned int FRONT_SIZE = sizeof("Lumix::Reflection::internal::GetTypeNameHelper<") - 1u;
	static const unsigned int BACK_SIZE = sizeof(">::GetTypeName") - 1u;

	template <typename T>
	struct GetTypeNameHelper
	{
		static const char* GetTypeName()
		{
#if defined(_MSC_VER) && !defined(__clang__)
			static const size_t size = sizeof(__FUNCTION__) - FRONT_SIZE - BACK_SIZE;
			static char typeName[size] = {};
			memcpy(typeName, __FUNCTION__ + FRONT_SIZE, size - 1u); //-V594
#else
			static const size_t size = sizeof(__PRETTY_FUNCTION__) - FRONT_SIZE - BACK_SIZE;
			static char typeName[size] = {};
			memcpy(typeName, __PRETTY_FUNCTION__ + FRONT_SIZE, size - 1u);
#endif

			return typeName;
		}
	};
}


template <typename T>
const char* getTypeName()
{
	return internal::GetTypeNameHelper<T>::GetTypeName();
}

struct FunctionBase
{
	virtual ~FunctionBase() {}

	virtual int getArgCount() const = 0;
	virtual const char* getReturnType() const = 0;
	virtual const char* getArgType(int i) const = 0;

	const char* decl_code;
};


template <typename F> struct Function;


template <typename R, typename C, typename... Args>
struct Function<R (C::*)(Args...)> : FunctionBase
{
	using F = R(C::*)(Args...);
	F function;

	int getArgCount() const override { return sizeof...(Args); }
	const char* getReturnType() const override { return getTypeName<typename ResultOf<F>::Type>(); }
	
	const char* getArgType(int i) const override
	{
		const char* expand[] = {
			getTypeName<Args>()...
		};
		return expand[i];
	}
};


} // namespace Reflection

template <typename T>
struct Prop {
	const char* name;
	Span<Reflection::IAttribute*> attributes;

	virtual T get() const = 0;
	virtual void set(T) const = 0;

	Reflection::IAttribute* getAttribute(Reflection::IAttribute::Type type) const {
		for (Reflection::IAttribute* attr : attributes) {
			if (attr->getType() == type) return attr;
		}
		return nullptr;
	}
};

struct ArrayProp {
	virtual u32 count() const = 0;
	virtual void add(u32 idx) const = 0;
	virtual void remove(u32 idx) const = 0;
	virtual bool canAddRemove() const { return true; }
};

struct IComponentVisitor {
	virtual void visit(const Prop<float>& prop) = 0;
	virtual void visit(const Prop<bool>& prop) = 0;
	virtual void visit(const Prop<i32>& prop) = 0;
	virtual void visit(const Prop<u32>& prop) = 0;
	virtual void visit(const Prop<Vec2>& prop) = 0;
	virtual void visit(const Prop<Vec3>& prop) = 0;
	virtual void visit(const Prop<IVec3>& prop) = 0;
	virtual void visit(const Prop<Vec4>& prop) = 0;
	virtual void visit(const Prop<Path>& prop) = 0;
	virtual void visit(const Prop<EntityPtr>& prop) = 0;
	virtual void visit(const Prop<const char*>& prop) = 0;
	
	virtual bool beginArray(const char* name, const ArrayProp& prop) = 0;
	virtual bool beginArrayItem(const char* name, u32 idx, const ArrayProp& prop) { return true; }
	virtual void endArrayItem() {};
	virtual void endArray() {}
};


template <typename G, typename S, typename... Attrs>
inline void visitFunctor(IComponentVisitor& visitor, const char* name, G getter, S setter, Attrs... attrs) {
	using T = decltype(getter());
	Reflection::IAttribute* attrs_array[sizeof...(attrs) + 1] = {
		&attrs...,
		nullptr
	};
	struct : Prop<T> {
		T get() const override { return (*getter)(); }
		void set(T v) const override { (*setter)(v); }
		const G* getter;
		const S* setter;
	} p;
	p.name = name;
	p.getter = &getter;
	p.setter = &setter;
	p.attributes = Span(attrs_array, attrs_array + sizeof...(attrs));
	visitor.visit(p);
}

template <typename T, typename... Attrs>
inline void visit(IComponentVisitor& visitor, const char* name, Ref<T> value, Attrs... attrs) {
	Reflection::IAttribute* attrs_array[sizeof...(attrs) + 1] = {
		&attrs...,
		nullptr
	};
	struct : Prop<T> {
		T get() const override { return value->value; }
		void set(T v) const override { *value = v; }
		Ref<T>* value;
	} p;
	p.value = &value;
	p.name = name;
	p.attributes = Span(attrs_array, attrs_array + sizeof...(attrs));
	visitor.visit(p);
}

template <typename C, typename Count, typename Add, typename Remove, typename Iter, typename... Attrs>
inline void visitArray(IComponentVisitor& v, const char* name, C* inst, EntityRef e, Count count, Add add, Remove remove, Iter iter, Attrs... attrs) {
	struct : ArrayProp {
		u32 count() const override { return (inst->*counter)(e); }
		void add(u32 idx) const override {  (inst->*adder)(e, idx);  }
		void remove(u32 idx) const override { (inst->*remover)(e, idx); }

		C* inst;
		Count counter;
		Add adder; 
		Remove remover;
		EntityRef e;
	} ar;
	ar.counter = count;
	ar.inst = inst;
	ar.adder = add;
	ar.remover = remove;
	ar.e = e;
	if(!v.beginArray(name, ar)) return;
	for (u32 i = 0; i < (u32)(inst->*count)(e); ++i) {
		if (!v.beginArrayItem(name, i, ar)) continue;
		iter(i);
		v.endArrayItem();
	}
	v.endArray();
}


template <typename C, typename G, typename S, typename... Attrs>
inline void visit(IComponentVisitor& visitor, const char* name, C* inst, EntityRef e, G getter, S setter, Attrs... attrs) {
	using T = typename ResultOf<G>::Type;
	Reflection::IAttribute* attrs_array[sizeof...(attrs) + 1] = {
		&attrs...,
		nullptr
	};
	struct : Prop<T> {
		T get() const override { return (inst->*getter)(e); }
		void set(T v) const override { (inst->*setter)(e, v); }
		C* inst;
		EntityRef e;
		G getter;
		S setter;
	} p;
	p.name = name;
	p.inst = inst;
	p.getter = getter;
	p.setter = setter;
	p.e = e;
	p.attributes = Span(attrs_array, attrs_array + sizeof...(attrs));
	visitor.visit(p);
}

template <typename C, typename G, typename S, typename... Attrs>
inline void visitEnum(IComponentVisitor& visitor, const char* name, C* inst, EntityRef e, G getter, S setter, Attrs... attrs) {
	using T = i32;
	using EnumT = typename ResultOf<G>::Type;
	Reflection::IAttribute* attrs_array[sizeof...(attrs) + 1] = {
		&attrs...,
		nullptr
	};
	struct : Prop<T> {
		T get() const override { return (i32)(inst->*getter)(e); }
		void set(T v) const override { (inst->*setter)(e, (EnumT)v); }
		C* inst;
		EntityRef e;
		G getter;
		S setter;
	} p;
	p.name = name;
	p.inst = inst;
	p.getter = getter;
	p.setter = setter;
	p.e = e;
	p.attributes = Span(attrs_array, attrs_array + sizeof...(attrs));
	visitor.visit(p);
}

template <typename C, typename G, typename S, typename... Attrs>
inline void visit(IComponentVisitor& visitor, const char* name, C* inst, EntityRef e, int idx, G getter, S setter, Attrs... attrs) {
	using T = typename ResultOf<G>::Type;
	Reflection::IAttribute* attrs_array[sizeof...(attrs) + 1] = {
		&attrs...,
		nullptr
	};
	struct : Prop<T> {
		T get() const override { return (inst->*getter)(e, idx); }
		void set(T v) const override { (inst->*setter)(e, idx, v); }
		C* inst;
		EntityRef e;
		G getter;
		S setter;
		int idx;
	} p;
	p.name = name;
	p.inst = inst;
	p.getter = getter;
	p.setter = setter;
	p.e = e;
	p.idx = idx;
	p.attributes = Span(attrs_array, attrs_array + sizeof...(attrs));
	visitor.visit(p);
}

inline void visit(IComponentVisitor& visitor, const char* name, Ref<String> value) {
	struct : Prop<const char*> {
		const char* get() const override { return value->value.c_str(); }
		void set(const char* val) const override { *value = val; }
		Ref<String>* value;
	} p;
	p.name = name;
	p.value = &value;
	visitor.visit(p);
}


} // namespace Lumix
