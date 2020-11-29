#pragma once


#include "engine/lumix.h"
#include "engine/metaprogramming.h"
#include "engine/resource.h"
#include "engine/stream.h"
#include "engine/string.h"
#include "engine/universe.h"


#define LUMIX_PROP(Scene, Property) &Scene::get##Property, &Scene::set##Property
#define LUMIX_FUNC_EX(Func, name) Reflection::function(&Func, #Func, name)
#define LUMIX_FUNC(Func) Reflection::function(&Func, #Func, nullptr)

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


struct IAttribute {
	enum Type {
		MIN,
		CLAMP,
		RADIANS,
		COLOR,
		RESOURCE,
		ENUM,
		MULTILINE,
		STRING_ENUM,
		NO_UI,
	};

	virtual ~IAttribute() {}
	virtual int getType() const = 0;
};

struct ComponentBase;
struct SceneBase;


LUMIX_ENGINE_API void init(IAllocator& allocator);
LUMIX_ENGINE_API void shutdown();

LUMIX_ENGINE_API void registerScene(SceneBase& scene);
LUMIX_ENGINE_API SceneBase* getFirstScene();
LUMIX_ENGINE_API const ComponentBase* getComponent(ComponentType cmp_type);

LUMIX_ENGINE_API ComponentType getComponentType(const char* id);
LUMIX_ENGINE_API u32 getComponentTypeHash(ComponentType type);
LUMIX_ENGINE_API ComponentType getComponentTypeFromHash(u32 hash);
LUMIX_ENGINE_API int getComponentTypesCount();
LUMIX_ENGINE_API const char* getComponentTypeID(int index);

template <typename T> void writeToStream(OutputMemoryStream& stream, T value) {	stream.write(value); }
template <typename T> T readFromStream(InputMemoryStream& stream) { return stream.read<T>(); }
template <> LUMIX_ENGINE_API Path readFromStream<Path>(InputMemoryStream& stream);
template <> LUMIX_ENGINE_API void writeToStream<Path>(OutputMemoryStream& stream, Path);
template <> LUMIX_ENGINE_API void writeToStream<const Path&>(OutputMemoryStream& stream, const Path& path);
template <> LUMIX_ENGINE_API const char* readFromStream<const char*>(InputMemoryStream& stream);
template <> LUMIX_ENGINE_API void writeToStream<const char*>(OutputMemoryStream& stream, const char* path);

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


struct ClampAttribute : IAttribute
{
	ClampAttribute(float min, float max) { this->min = min; this->max = max; }
	ClampAttribute() {}

	int getType() const override { return CLAMP; }

	float min;
	float max;
};

struct EnumAttribute : IAttribute {
	virtual u32 count(ComponentUID cmp) const = 0;
	virtual const char* name(ComponentUID cmp, u32 idx) const = 0;
	
	int getType() const override { return ENUM; }
};

struct StringEnumAttribute : IAttribute {
	virtual u32 count(ComponentUID cmp) const = 0;
	virtual const char* name(ComponentUID cmp, u32 idx) const = 0;
	
	int getType() const override { return STRING_ENUM; }
};

struct RadiansAttribute : IAttribute
{
	int getType() const override { return RADIANS; }
};

struct MultilineAttribute : IAttribute
{
	int getType() const override { return MULTILINE; }
};

struct ColorAttribute : IAttribute
{
	int getType() const override { return COLOR; }
};

struct NoUIAttribute : IAttribute {
	int getType() const override { return NO_UI; }
};

template <typename T> struct Property {
	virtual Span<const IAttribute* const> getAttributes() const = 0;
	virtual T get(ComponentUID cmp, int index) const = 0;
	virtual void set(ComponentUID cmp, int index, T) const = 0;
	const char* name;
};


struct IBlobProperty {
	virtual void getValue(ComponentUID cmp, int index, OutputMemoryStream& stream) const = 0;
	virtual void setValue(ComponentUID cmp, int index, InputMemoryStream& stream) const = 0;
	const char* name;
};

struct IDynamicProperties {
	enum Type {
		I32,
		FLOAT,
		STRING,
		ENTITY,
		RESOURCE,
		BOOLEAN,
		COLOR,

		NONE
	};
	union Value {
		Value(){}
		EntityPtr e;
		i32 i;
		float f;
		const char* s;
		bool b;
		Vec3 v3;
	};
	virtual u32 getCount(ComponentUID cmp, int array_idx) const = 0;
	virtual Type getType(ComponentUID cmp, int array_idx, u32 idx) const = 0;
	virtual const char* getName(ComponentUID cmp, int array_idx, u32 idx) const = 0;
	virtual Value getValue(ComponentUID cmp, int array_idx, u32 idx) const = 0;
	virtual Reflection::ResourceAttribute getResourceAttribute(ComponentUID cmp, int array_idx, u32 idx) const = 0;
	virtual void set(ComponentUID cmp, int array_idx, const char* name, Type type, Value value) const = 0;
	virtual void set(ComponentUID cmp, int array_idx, u32 idx, Value value) const = 0;

	const char* name;
};

template <typename T> inline T get(IDynamicProperties::Value);
template <> inline float get(IDynamicProperties::Value v) { return v.f; }
template <> inline i32 get(IDynamicProperties::Value v) { return v.i; }
template <> inline const char* get(IDynamicProperties::Value v) { return v.s; }
template <> inline EntityPtr get(IDynamicProperties::Value v) { return v.e; }
template <> inline bool get(IDynamicProperties::Value v) { return v.b; }
template <> inline Vec3 get(IDynamicProperties::Value v) { return v.v3; }

template <typename T> inline void set(IDynamicProperties::Value& v, T);
template <> inline void set(IDynamicProperties::Value& v, float val) { v.f = val; }
template <> inline void set(IDynamicProperties::Value& v, i32 val) { v.i = val; }
template <> inline void set(IDynamicProperties::Value& v, const char* val) { v.s = val; }
template <> inline void set(IDynamicProperties::Value& v, EntityPtr val) { v.e = val; }
template <> inline void set(IDynamicProperties::Value& v, bool val) { v.b = val; }
template <> inline void set(IDynamicProperties::Value& v, Vec3 val) { v.v3 = val; }

struct IArrayProperty
{
	virtual void addItem(ComponentUID cmp, int index) const = 0;
	virtual void removeItem(ComponentUID cmp, int index) const = 0;
	virtual int getCount(ComponentUID cmp) const = 0;
	virtual void visit(struct IPropertyVisitor& visitor) const = 0;

	const char* name;
};

struct IPropertyVisitor
{
	virtual ~IPropertyVisitor() {}

	virtual void visit(const Property<float>& prop) = 0;
	virtual void visit(const Property<int>& prop) = 0;
	virtual void visit(const Property<u32>& prop) = 0;
	virtual void visit(const Property<EntityPtr>& prop) = 0;
	virtual void visit(const Property<Vec2>& prop) = 0;
	virtual void visit(const Property<Vec3>& prop) = 0;
	virtual void visit(const Property<IVec3>& prop) = 0;
	virtual void visit(const Property<Vec4>& prop) = 0;
	virtual void visit(const Property<Path>& prop) = 0;
	virtual void visit(const Property<bool>& prop) = 0;
	virtual void visit(const Property<const char*>& prop) = 0;
	virtual void visit(const IDynamicProperties& prop) {}
	virtual void visit(const IArrayProperty& prop) = 0;
	virtual void visit(const IBlobProperty& prop) = 0;
};


struct IEmptyPropertyVisitor : IPropertyVisitor
{
	void visit(const Property<float>& prop) override {}
	void visit(const Property<int>& prop) override {}
	void visit(const Property<u32>& prop) override {}
	void visit(const Property<EntityPtr>& prop) override {}
	void visit(const Property<Vec2>& prop) override {}
	void visit(const Property<Vec3>& prop) override {}
	void visit(const Property<IVec3>& prop) override {}
	void visit(const Property<Vec4>& prop) override {}
	void visit(const Property<Path>& prop) override {}
	void visit(const Property<bool>& prop) override {}
	void visit(const Property<const char*>& prop) override {}
	void visit(const IArrayProperty& prop) override {}
	void visit(const IBlobProperty& prop) override {}
	void visit(const IDynamicProperties& prop) override {}
};


struct ComponentBase
{
	virtual ~ComponentBase() {}

	virtual int getPropertyCount() const = 0;
	virtual void visit(IPropertyVisitor&) const = 0;
	virtual Span<const struct FunctionBase* const> getFunctions() const = 0;

	const char* name;
	ComponentType component_type;
};


template <typename T>
bool getPropertyValue(IScene& scene, EntityRef e, ComponentType cmp_type, const char* prop_name, Ref<T> out) {
	struct : IEmptyPropertyVisitor {
		void visit(const Property<T>& prop) override {
			if (equalStrings(prop.name, prop_name)) {
				found = true;
				value = prop.get(cmp, -1);
			}
		}
		ComponentUID cmp;
		const char* prop_name;
		T value = {};
		bool found = false;
	} visitor;
	visitor.prop_name = prop_name;
	visitor.cmp.scene = &scene;
	visitor.cmp.type = cmp_type;
	visitor.cmp.entity = e;
	const Reflection::ComponentBase* cmp_desc = getComponent(cmp_type);
	cmp_desc->visit(visitor);
	out = visitor.value;
	return visitor.found;
}

template <typename Base, typename... T>
struct TupleHolder {
	TupleHolder() {}
	
	TupleHolder(Tuple<T...> tuple) { init(tuple); }
	TupleHolder(TupleHolder&& rhs) { init(rhs.objects); }
	TupleHolder(const TupleHolder& rhs) { init(rhs.objects); }

	void init(const Tuple<T...>& tuple)
	{
		objects = tuple;
		int i = 0;
		apply([&](auto& v){
			ptrs[i] = &v;
			++i;
		}, objects);
	}

	void operator =(Tuple<T...>&& tuple) { init(tuple); }
	void operator =(const TupleHolder& rhs) { init(rhs.objects); }
	void operator =(TupleHolder&& rhs) { init(rhs.objects); }

	Span<const Base* const> get() const {
		return Span(static_cast<const Base*const*>(ptrs), (u32)sizeof...(T));
	}

	Tuple<T...> objects;
	Base* ptrs[sizeof...(T) + 1];
};

template <typename Getter, typename Setter>
struct BlobProperty : IBlobProperty
{
	void getValue(ComponentUID cmp, int index, OutputMemoryStream& stream) const override
	{
		using C = typename ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		(inst->*getter)((EntityRef)cmp.entity, stream);
	}

	void setValue(ComponentUID cmp, int index, InputMemoryStream& stream) const override
	{
		using C = typename ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		(inst->*setter)((EntityRef)cmp.entity, stream);
	}

	Getter getter;
	Setter setter;
};


template <typename T, typename CmpGetter, typename PtrType, typename... Attributes>
struct VarProperty : Property<T>
{
	Span<const IAttribute* const> getAttributes() const override { return attributes.get(); }

	T get(ComponentUID cmp, int index) const override
	{
		using C = typename ClassOf<CmpGetter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		auto& c = (inst->*cmp_getter)((EntityRef)cmp.entity);
		auto& v = c.*ptr;
		return static_cast<T>(v);
	}

	void set(ComponentUID cmp, int index, T value) const override
	{
		using C = typename ClassOf<CmpGetter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		auto& c = (inst->*cmp_getter)((EntityRef)cmp.entity);
		c.*ptr = value;
	}

	CmpGetter cmp_getter;
	PtrType ptr;
	TupleHolder<IAttribute, Attributes...> attributes;
};


namespace detail {

template <typename Getter> struct GetterProxy;

template <typename R, typename C>
struct GetterProxy<R(C::*)(EntityRef, int)>
{
	using Getter = R(C::*)(EntityRef, int);
	static R invoke(C* inst, Getter getter, EntityRef entity, int index)
	{
		return (inst->*getter)(entity, index);
	}
};

template <typename R, typename C>
struct GetterProxy<R(C::*)(EntityRef)>
{
	using Getter = R(C::*)(EntityRef);
	static R invoke(C* inst, Getter getter, EntityRef entity, int index)
	{
		return (inst->*getter)(entity);
	}
};


template <typename Setter> struct SetterProxy;

template <typename C, typename A>
struct SetterProxy<void (C::*)(EntityRef, int, A)>
{
	using Setter = void (C::*)(EntityRef, int, A);
	template <typename T>
	static void invoke(C* inst, Setter setter, EntityRef entity, int index, T value)
	{
		(inst->*setter)(entity, index, static_cast<A>(value));
	}
};

template <typename C, typename A>
struct SetterProxy<void (C::*)(EntityRef, A)>
{
	using Setter = void (C::*)(EntityRef, A);
	template <typename T>
	static void invoke(C* inst, Setter setter, EntityRef entity, int index, T value)
	{
		(inst->*setter)(entity, static_cast<A>(value));
	}
};


} // namespace detail

template <typename T, typename Getter, typename Setter, typename... Attributes>
struct CommonProperty : Property<T>
{
	Span<const IAttribute* const> getAttributes() const override { return attributes.get(); }

	T get(ComponentUID cmp, int index) const override
	{
		using C = typename ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		return static_cast<T>(detail::GetterProxy<Getter>::invoke(inst, getter, (EntityRef)cmp.entity, index));
	}

	void set(ComponentUID cmp, int index, T value) const override
	{
		using C = typename ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		detail::SetterProxy<Setter>::invoke(inst, setter, (EntityRef)cmp.entity, index, value);
	}

	TupleHolder<IAttribute, Attributes...> attributes;
	Getter getter;
	Setter setter;
};


template <typename Counter, typename Adder, typename Remover, typename... Props>
struct ArrayProperty : IArrayProperty
{
	ArrayProperty() {}

	void addItem(ComponentUID cmp, int index) const override
	{
		using C = typename ClassOf<Counter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		(inst->*adder)((EntityRef)cmp.entity, index);
	}


	void removeItem(ComponentUID cmp, int index) const override
	{
		using C = typename ClassOf<Counter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		(inst->*remover)((EntityRef)cmp.entity, index);
	}


	int getCount(ComponentUID cmp) const override
	{ 
		using C = typename ClassOf<Counter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		return (inst->*counter)((EntityRef)cmp.entity);
	}


	void visit(IPropertyVisitor& visitor) const override
	{
		apply([&](auto& x) { visitor.visit(x); }, properties);
	}


	Tuple<Props...> properties;
	Counter counter;
	Adder adder;
	Remover remover;
};


template <typename T>
const IAttribute* getAttribute(const Property<T>& prop, IAttribute::Type type) {
	for (const IAttribute* attr : prop.getAttributes()) {
		if (attr->getType() == type) return attr;
	}
	return nullptr;
}

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

struct Variant {
	Variant() { type = I32; i = 0; }
	enum Type {
		VOID,
		PTR,
		BOOL,
		I32,
		U32,
		FLOAT,
		CSTR,
		ENTITY,
		VEC2,
		VEC3,
		DVEC3
	} type;
	union {
		bool b;
		i32 i;
		u32 u;
		float f;
		const char* s;
		EntityPtr e;
		Vec2 v2;
		Vec3 v3;
		DVec3 dv3;
		void* ptr;
	};

	void operator =(bool v) { b = v; type = BOOL; }
	void operator =(i32 v) { i = v; type = I32; }
	void operator =(u32 v) { u = v; type = U32; }
	void operator =(float v) { f = v; type = FLOAT; }
	void operator =(const char* v) { s = v; type = CSTR; }
	void operator =(EntityPtr v) { e = v; type = ENTITY; }
	void operator =(Vec2 v) { v2 = v; type = VEC2; }
	void operator =(Vec3 v) { v3 = v; type = VEC3; }
	void operator =(const DVec3& v) { dv3 = v; type = DVEC3; }
	void operator =(void* v) { ptr = v; type = PTR; }
};

struct FunctionBase
{
	virtual ~FunctionBase() {}

	virtual u32 getArgCount() const = 0;
	virtual Variant::Type getReturnType() const = 0;
	virtual const char* getReturnTypeName() const = 0;
	virtual const char* getThisTypeName() const = 0;
	virtual Variant::Type getArgType(int i) const = 0;
	virtual Variant invoke(void* obj, Span<Variant> args) const = 0;

	const char* decl_code;
	const char* name;
};

struct SceneBase
{
	virtual ~SceneBase() {}

	virtual Span<const FunctionBase* const> getFunctions() const = 0;
	virtual Span<const ComponentBase* const> getComponents() const = 0;

	const char* name;
	SceneBase* next = nullptr;
};

template <typename Components, typename Funcs> struct Scene;

template <typename... Components, typename... Funcs>
struct Scene<Tuple<Components...>, Tuple<Funcs...>> : SceneBase
{
	Span<const FunctionBase* const> getFunctions() const override {	return functions.get();	}
	Span<const ComponentBase* const> getComponents() const override { return components.get(); }

	TupleHolder<ComponentBase, Components...> components;
	TupleHolder<FunctionBase, Funcs...> functions;
};

template <typename Funcs, typename Props> struct Component;

template <typename... Funcs, typename Props>
struct Component<Tuple<Funcs...>, Props> : ComponentBase
{
	int getPropertyCount() const override { return TupleSize<Props>::result; }


	void visit(IPropertyVisitor& visitor) const override
	{
		apply([&](auto& x) { visitor.visit(x); }, properties);
	}

	Span<const FunctionBase* const> getFunctions() const override { return functions.get(); }

	Props properties;
	TupleHolder<FunctionBase, Funcs...> functions;
};


template <typename... Components, typename... Funcs>
auto scene(const char* name, Tuple<Funcs...> funcs, Components... components)
{
	Scene<Tuple<Components...>, Tuple<Funcs...>> scene;
	scene.name = name;
	scene.functions = funcs;
	scene.components = makeTuple(components...);
	return scene;
}


template <typename... Components>
auto scene(const char* name, Components... components)
{
	Scene<Tuple<Components...>, Tuple<>> scene;
	scene.name = name;
	scene.components = makeTuple(components...);
	return scene;
}

template <typename T> struct VariantTag {};

template <typename T> inline Variant::Type _getVariantType(VariantTag<T*>) { return Variant::PTR; }
inline Variant::Type _getVariantType(VariantTag<void>) { return Variant::VOID; }
inline Variant::Type _getVariantType(VariantTag<bool>) { return Variant::BOOL; }
inline Variant::Type _getVariantType(VariantTag<i32>) { return Variant::I32; }
inline Variant::Type _getVariantType(VariantTag<u32>) { return Variant::U32; }
inline Variant::Type _getVariantType(VariantTag<float>) { return Variant::FLOAT; }
inline Variant::Type _getVariantType(VariantTag<const char*>) { return Variant::CSTR; }
inline Variant::Type _getVariantType(VariantTag<EntityPtr>) { return Variant::ENTITY; }
inline Variant::Type _getVariantType(VariantTag<EntityRef>) { return Variant::ENTITY; }
inline Variant::Type _getVariantType(VariantTag<Vec2>) { return Variant::VEC2; }
inline Variant::Type _getVariantType(VariantTag<Vec3>) { return Variant::VEC3; }
inline Variant::Type _getVariantType(VariantTag<Path>) { return Variant::CSTR; }
inline Variant::Type _getVariantType(VariantTag<DVec3>) { return Variant::DVEC3; }
template <typename T> inline Variant::Type getVariantType() { return _getVariantType(VariantTag<RemoveCVR<T>>{}); }

inline bool fromVariant(int i, Span<Variant> args, VariantTag<bool>) { return args[i].b; }
inline float fromVariant(int i, Span<Variant> args, VariantTag<float>) { return args[i].f; }
inline const char* fromVariant(int i, Span<Variant> args, VariantTag<const char*>) { return args[i].s; }
inline Path fromVariant(int i, Span<Variant> args, VariantTag<Path>) { return Path(args[i].s); }
inline i32 fromVariant(int i, Span<Variant> args, VariantTag<i32>) { return args[i].i; }
inline u32 fromVariant(int i, Span<Variant> args, VariantTag<u32>) { return args[i].u; }
inline Vec2 fromVariant(int i, Span<Variant> args, VariantTag<Vec2>) { return args[i].v2; }
inline Vec3 fromVariant(int i, Span<Variant> args, VariantTag<Vec3>) { return args[i].v3; }
inline DVec3 fromVariant(int i, Span<Variant> args, VariantTag<DVec3>) { return args[i].dv3; }
inline EntityPtr fromVariant(int i, Span<Variant> args, VariantTag<EntityPtr>) { return args[i].e; }
inline EntityRef fromVariant(int i, Span<Variant> args, VariantTag<EntityRef>) { return (EntityRef)args[i].e; }
inline void* fromVariant(int i, Span<Variant> args, VariantTag<void*>) { return args[i].ptr; }
template <typename T> inline T* fromVariant(int i, Span<Variant> args, VariantTag<T*>) { return (T*)args[i].ptr; }

template <typename... Args>
struct VariantCaller {
	template <typename C, typename F, int... I>
	static Variant call(C* inst, F f, Span<Variant> args, Indices<I...>& indices) {
		using R = typename ResultOf<F>::Type;
		if constexpr (IsSame<R, void>::Value) {
			Variant v;
			v.type = Variant::VOID;
			(inst->*f)(fromVariant(I, args, VariantTag<RemoveCVR<Args>>{})...);
			return v;
		}
		else {
			Variant v;
			v = (inst->*f)(fromVariant(I, args, VariantTag<RemoveCVR<Args>>{})...);
			return v;
		}
	}
};

template <typename F> struct Function;

template <typename R, typename C, typename... Args>
struct Function<R (C::*)(Args...)> : FunctionBase
{
	using F = R(C::*)(Args...);
	F function;

	u32 getArgCount() const override { return sizeof...(Args); }
	Variant::Type getReturnType() const override { return getVariantType<R>(); }
	const char* getReturnTypeName() const override { return getTypeName<R>(); }
	const char* getThisTypeName() const override { return getTypeName<C>(); }
	
	Variant::Type getArgType(int i) const override
	{
		Variant::Type expand[] = {
			getVariantType<Args>()...,
			Variant::Type::VOID
		};
		return expand[i];
	}
	
	Variant invoke(void* obj, Span<Variant> args) const override {
		auto indices = typename BuildIndices<-1, sizeof...(Args)>::result{};
		return VariantCaller<Args...>::call((C*)obj, function, args, indices);
	}
};

template <typename R, typename C, typename... Args>
struct Function<R (C::*)(Args...) const> : FunctionBase
{
	using F = R(C::*)(Args...) const;
	F function;

	u32 getArgCount() const override { return sizeof...(Args); }
	Variant::Type getReturnType() const override { return getVariantType<R>(); }
	const char* getReturnTypeName() const override { return getTypeName<R>(); }
	const char* getThisTypeName() const override { return getTypeName<C>(); }
	
	Variant::Type getArgType(int i) const override
	{
		Variant::Type expand[] = {
			getVariantType<Args>()...,
			Variant::Type::VOID
		};
		return expand[i];
	}

	Variant invoke(void* obj, Span<Variant> args) const override {
		auto indices = typename BuildIndices<-1, sizeof...(Args)>::result{};
		return VariantCaller<Args...>::call((const C*)obj, function, args, indices);
	}
};

LUMIX_ENGINE_API Array<FunctionBase*>& allFunctions();

template <typename F>
auto& function(F func, const char* decl_code, const char* name)
{
	static Function<F> ret;
	allFunctions().push(&ret);
	ret.function = func;
	ret.decl_code = decl_code;
	ret.name = name;
	return ret;
}


template <typename... F>
auto functions(F... functions)
{
	Tuple<F...> f = makeTuple(functions...);
	return f;
}


template <typename... Props, typename... Funcs>
auto component(const char* name, Tuple<Funcs...> functions, Props... props)
{
	Component<Tuple<Funcs...>, Tuple<Props...>> cmp;
	cmp.name = name;
	cmp.functions = functions;
	cmp.properties = makeTuple(props...);
	cmp.component_type = getComponentType(name);
	return cmp;
}


template <typename... Props>
auto component(const char* name, Props... props)
{
	Component<Tuple<>, Tuple<Props...>> cmp;
	cmp.name = name;
	cmp.properties = makeTuple(props...);
	cmp.component_type = getComponentType(name);
	return cmp;
}


template <typename Getter, typename Setter>
auto blob_property(const char* name, Getter getter, Setter setter)
{
	BlobProperty<Getter, Setter> p;
	p.getter = getter;
	p.setter = setter;
	p.name = name;
	return p;
}


template <typename CmpGetter, typename PtrType, typename... Attributes>
auto var_property(const char* name, CmpGetter getter, PtrType ptr, Attributes... attributes)
{
	using T = typename ResultOf<PtrType>::Type;
	VarProperty<T, CmpGetter, PtrType, Attributes...> p;
	p.attributes = makeTuple(attributes...);
	p.cmp_getter = getter;
	p.ptr = ptr;
	p.name = name;
	return p;
}


template <typename Getter, typename Setter, typename... Attributes>
auto property(const char* name, Getter getter, Setter setter, Attributes... attributes)
{
	using R = typename ResultOf<Getter>::Type;
	CommonProperty<R, Getter, Setter, Attributes...> p;
	p.attributes = makeTuple(attributes...);
	p.getter = getter;
	p.setter = setter;
	p.name = name;
	return p;
}

template <typename Getter, typename Setter, typename... Attributes>
auto enum_property(const char* name, Getter getter, Setter setter, Attributes... attributes)
{
	using R = typename ResultOf<Getter>::Type;
	CommonProperty<i32, Getter, Setter, Attributes...> p;
	p.attributes = makeTuple(attributes...);
	p.getter = getter;
	p.setter = setter;
	p.name = name;
	return p;
}

template <typename Counter, typename Adder, typename Remover, typename... Props>
auto array(const char* name, Counter counter, Adder adder, Remover remover, Props... properties)
{
	ArrayProperty<Counter, Adder, Remover, Props...> p;
	p.name = name;
	p.counter = counter;
	p.adder = adder;
	p.remover = remover;
	p.properties = makeTuple(properties...);
	return p;
}


} // namespace Reflection


} // namespace Lumix
