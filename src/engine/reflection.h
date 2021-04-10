#pragma once


#include "engine/lumix.h"
#include "engine/metaprogramming.h"
#include "engine/resource.h"
#include "engine/string.h"
#include "engine/universe.h"


#define LUMIX_SCENE(Class, Label) using ReflScene = Class; reflection::build_scene(Label)
#define LUMIX_FUNC_EX(F, Name) function<&ReflScene::F>(Name, #F)
#define LUMIX_FUNC(F) function<&F>(#F, #F)
#define LUMIX_CMP(Cmp, Name, Label) cmp<&ReflScene::create##Cmp, &ReflScene::destroy##Cmp>(Name, Label)
#define LUMIX_PROP(Property, Label) prop<&ReflScene::get##Property, &ReflScene::set##Property>(Label)
#define LUMIX_READONLY_PROP(Property, Label) prop<&ReflScene::get##Property>(Label)
#define LUMIX_ENUM_PROP(Property, Label) enum_prop<&ReflScene::get##Property, &ReflScene::set##Property>(Label)
#define LUMIX_READONLY_ENUM_PROP(Property, Label) enum_prop<&ReflScene::get##Property, &ReflScene::set##Property>(Label)
#define LUMIX_GLOBAL_FUNC(Func) reflection::function(&Func, #Func, nullptr)

namespace Lumix
{

template <typename T> struct Array;
template <typename T> struct Span;
struct Path;
struct IVec3;
struct Vec2;
struct Vec3;
struct Vec4;

namespace reflection
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

// we don't use method pointers here because VS has sizeof issues if IScene is forward declared
using CreateComponent = void (*)(IScene*, EntityRef);
using DestroyComponent = void (*)(IScene*, EntityRef);

struct RegisteredComponent {
	u32 name_hash = 0;
	u32 scene = 0;
	struct ComponentBase* cmp = nullptr;
};

LUMIX_ENGINE_API const ComponentBase* getComponent(ComponentType cmp_type);
LUMIX_ENGINE_API const struct PropertyBase* getProperty(ComponentType cmp_type, const char* prop);
LUMIX_ENGINE_API Span<const RegisteredComponent> getComponents();

LUMIX_ENGINE_API ComponentType getComponentType(const char* id);
LUMIX_ENGINE_API ComponentType getComponentTypeFromHash(u32 hash);

struct ResourceAttribute : IAttribute
{
	ResourceAttribute(ResourceType type) : resource_type(type) {}
	ResourceAttribute() {}

	int getType() const override { return RESOURCE; }

	ResourceType resource_type;
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

struct ColorAttribute : IAttribute {
	int getType() const override { return COLOR; }
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


struct PropertyBase {
	PropertyBase(IAllocator& allocator) : attributes(allocator) {}
	Array<IAttribute*> attributes;

	virtual void visit(struct IPropertyVisitor& visitor) const = 0;
	const char* name;
};


struct DynamicProperties : PropertyBase {
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
	DynamicProperties(IAllocator& allocator) : PropertyBase(allocator) {}

	virtual u32 getCount(ComponentUID cmp, int array_idx) const = 0;
	virtual Type getType(ComponentUID cmp, int array_idx, u32 idx) const = 0;
	virtual const char* getName(ComponentUID cmp, int array_idx, u32 idx) const = 0;
	virtual Value getValue(ComponentUID cmp, int array_idx, u32 idx) const = 0;
	virtual ResourceAttribute getResourceAttribute(ComponentUID cmp, int array_idx, u32 idx) const = 0;
	virtual void set(ComponentUID cmp, int array_idx, const char* name, Type type, Value value) const = 0;
	virtual void set(ComponentUID cmp, int array_idx, u32 idx, Value value) const = 0;
	
	void visit(IPropertyVisitor& visitor) const override;
};

template <typename T> inline T get(DynamicProperties::Value);
template <> inline float get(DynamicProperties::Value v) { return v.f; }
template <> inline i32 get(DynamicProperties::Value v) { return v.i; }
template <> inline const char* get(DynamicProperties::Value v) { return v.s; }
template <> inline EntityPtr get(DynamicProperties::Value v) { return v.e; }
template <> inline bool get(DynamicProperties::Value v) { return v.b; }
template <> inline Vec3 get(DynamicProperties::Value v) { return v.v3; }

template <typename T> inline void set(DynamicProperties::Value& v, T);
template <> inline void set(DynamicProperties::Value& v, float val) { v.f = val; }
template <> inline void set(DynamicProperties::Value& v, i32 val) { v.i = val; }
template <> inline void set(DynamicProperties::Value& v, const char* val) { v.s = val; }
template <> inline void set(DynamicProperties::Value& v, EntityPtr val) { v.e = val; }
template <> inline void set(DynamicProperties::Value& v, bool val) { v.b = val; }
template <> inline void set(DynamicProperties::Value& v, Vec3 val) { v.v3 = val; }


template <typename T>
struct Property : PropertyBase {
	Property(IAllocator& allocator) : PropertyBase(allocator) {}

	typedef void (*Setter)(IScene*, EntityRef, u32, const T&);
	typedef T (*Getter)(IScene*, EntityRef, u32);

	void visit(IPropertyVisitor& visitor) const override;

	virtual T get(ComponentUID cmp, u32 idx) const {
		return getter(cmp.scene, (EntityRef)cmp.entity, idx);
	}

	virtual void set(ComponentUID cmp, u32 idx, T val) const {
		setter(cmp.scene, (EntityRef)cmp.entity, idx, val);
	}

	Setter setter;
	Getter getter;
};

struct IPropertyVisitor {
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
	virtual void visit(const struct ArrayProperty& prop) = 0;
	virtual void visit(const struct BlobProperty& prop) = 0;
	virtual void visit(const DynamicProperties& prop) {}
};

template <typename T>
void Property<T>::visit(IPropertyVisitor& visitor) const {
	visitor.visit(*this);
}


struct IEmptyPropertyVisitor : IPropertyVisitor {
	virtual ~IEmptyPropertyVisitor() {}
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
	void visit(const ArrayProperty& prop) override {}
	void visit(const BlobProperty& prop) override {}
	void visit(const DynamicProperties& prop) override {}
};

struct LUMIX_ENGINE_API ArrayProperty : PropertyBase {
	typedef u32 (*Counter)(IScene*, EntityRef);
	typedef void (*Adder)(IScene*, EntityRef, u32);
	typedef void (*Remover)(IScene*, EntityRef, u32);

	ArrayProperty(IAllocator& allocator);

	u32 getCount(ComponentUID cmp) const;
	void addItem(ComponentUID cmp, u32 idx) const;
	void removeItem(ComponentUID cmp, u32 idx) const;

	void visit(struct IPropertyVisitor& visitor) const override;
	void visitChildren(struct IPropertyVisitor& visitor) const;

	Array<PropertyBase*> children;
	Counter counter;
	Adder adder;
	Remover remover;
};

struct LUMIX_ENGINE_API BlobProperty : PropertyBase {
	BlobProperty(IAllocator& allocator);

	void visit(struct IPropertyVisitor& visitor) const override;
	void getValue(ComponentUID cmp, u32 idx, OutputMemoryStream& stream) const;
	void setValue(ComponentUID cmp, u32 idx, InputMemoryStream& stream) const;

	typedef void (*Getter)(IScene*, EntityRef, u32, OutputMemoryStream&);
	typedef void (*Setter)(IScene*, EntityRef, u32, InputMemoryStream&);

	Getter getter;
	Setter setter;
};

struct Icon { const char* name; };
inline Icon icon(const char* name) { return {name}; }

namespace detail {

static const unsigned int FRONT_SIZE = sizeof("Lumix::detail::GetTypeNameHelper<") - 1u;
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

} // namespace detail


template <typename T>
const IAttribute* getAttribute(const Property<T>& prop, IAttribute::Type type) {
	for (const IAttribute* attr : prop.attributes) {
		if (attr->getType() == type) return attr;
	}
	return nullptr;
}

template <typename T>
const char* getTypeName()
{
	return detail::GetTypeNameHelper<T>::GetTypeName();
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

struct ComponentBase {
	ComponentBase(IAllocator& allocator);

	void visit(IPropertyVisitor& visitor) const;

	const char* icon = "";
	const char* name;
	const char* label;

	u32 scene;
	CreateComponent creator;
	DestroyComponent destroyer;
	ComponentType component_type;
	Array<PropertyBase*> props;
	Array<FunctionBase*> functions;
};

template <typename T>
bool getPropertyValue(IScene& scene, EntityRef e, ComponentType cmp_type, const char* prop_name, T& out) {
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
	const ComponentBase* cmp_desc = getComponent(cmp_type);
	cmp_desc->visit(visitor);
	out = visitor.value;
	return visitor.found;
}

struct Scene {
	Scene(IAllocator& allocator);

	Array<FunctionBase*> functions;
	Array<ComponentBase*> cmps;
	const char* name;
	Scene* next = nullptr;
};

LUMIX_ENGINE_API Scene* getFirstScene();

struct builder {
	builder(IAllocator& allocator);

	template <auto Creator, auto Destroyer>
	builder& cmp(const char* name, const char* label) {
		auto creator = [](IScene* scene, EntityRef e){ (scene->*static_cast<void (IScene::*)(EntityRef)>(Creator))(e); };
		auto destroyer = [](IScene* scene, EntityRef e){ (scene->*static_cast<void (IScene::*)(EntityRef)>(Destroyer))(e); };
	
		ComponentBase* cmp = LUMIX_NEW(allocator, ComponentBase)(allocator);
		cmp->name = name;
		cmp->label = label;
		cmp->component_type = getComponentType(name);
		cmp->creator = creator;
		cmp->destroyer = destroyer;
		registerCmp(cmp);

		return *this;
	}

	template <auto Getter>
	builder& enum_prop(const char* name) {
		auto* p = LUMIX_NEW(allocator, Property<i32>)(allocator);
		p->getter = [](IScene* scene, EntityRef e, u32 idx) -> i32 {
			using T = typename ResultOf<decltype(Getter)>::Type;
			using C = typename ClassOf<decltype(Getter)>::Type;
			if constexpr (ArgsCount<decltype(Getter)>::value == 1) {
				return static_cast<i32>((static_cast<C*>(scene)->*Getter)(e));
			}
			else {
				return static_cast<i32>((static_cast<C*>(scene)->*Getter)(e, idx));
			}
		};
		p->name = name;
		addProp(p);
		return *this;
	}

	template <auto Getter, auto Setter>
	builder& enum_prop(const char* name) {
		auto* p = LUMIX_NEW(allocator, Property<i32>)(allocator);
		p->setter = [](IScene* scene, EntityRef e, u32 idx, const i32& value) {
			using T = typename ResultOf<decltype(Getter)>::Type;
			using C = typename ClassOf<decltype(Setter)>::Type;
			if constexpr (ArgsCount<decltype(Setter)>::value == 2) {
				(static_cast<C*>(scene)->*Setter)(e, static_cast<T>(value));
			}
			else {
				(static_cast<C*>(scene)->*Setter)(e, idx, static_cast<T>(value));
			}
		};

		p->getter = [](IScene* scene, EntityRef e, u32 idx) -> i32 {
			using T = typename ResultOf<decltype(Getter)>::Type;
			using C = typename ClassOf<decltype(Getter)>::Type;
			if constexpr (ArgsCount<decltype(Getter)>::value == 1) {
				return static_cast<i32>((static_cast<C*>(scene)->*Getter)(e));
			}
			else {
				return static_cast<i32>((static_cast<C*>(scene)->*Getter)(e, idx));
			}
		};
		p->name = name;
		addProp(p);
		return *this;
	}

	template <typename T>
	builder& property() {
		auto* p = LUMIX_NEW(allocator, T)(allocator);
		addProp(p);
		return *this;
	}


	template <auto Getter>
	builder& prop(const char* name) {
		using T = typename ResultOf<decltype(Getter)>::Type;
		auto* p = LUMIX_NEW(allocator, Property<T>)(allocator);
		
		p->getter = [](IScene* scene, EntityRef e, u32 idx) -> T {
			using C = typename ClassOf<decltype(Getter)>::Type;
			if constexpr (ArgsCount<decltype(Getter)>::value == 1) {
				return (static_cast<C*>(scene)->*Getter)(e);
			}
			else {
				return (static_cast<C*>(scene)->*Getter)(e, idx);
			}
		};

		p->name = name;
		addProp(p);
		return *this;
	}

	template <auto Getter, auto Setter>
	builder& prop(const char* name) {
		using T = typename ResultOf<decltype(Getter)>::Type;
		auto* p = LUMIX_NEW(allocator, Property<T>)(allocator);
		
		p->setter = [](IScene* scene, EntityRef e, u32 idx, const T& value) {
			using C = typename ClassOf<decltype(Setter)>::Type;
			if constexpr (ArgsCount<decltype(Setter)>::value == 2) {
				(static_cast<C*>(scene)->*Setter)(e, value);
			}
			else {
				(static_cast<C*>(scene)->*Setter)(e, idx, value);
			}
		};

		p->getter = [](IScene* scene, EntityRef e, u32 idx) -> T {
			using C = typename ClassOf<decltype(Getter)>::Type;
			if constexpr (ArgsCount<decltype(Getter)>::value == 1) {
				return (static_cast<C*>(scene)->*Getter)(e);
			}
			else {
				return (static_cast<C*>(scene)->*Getter)(e, idx);
			}
		};

		p->name = name;
		addProp(p);
		return *this;
	}

	template <auto Getter, auto PropGetter>
	builder& blob_property(const char* name) {
		auto* p = LUMIX_NEW(allocator, BlobProperty)(allocator);
		p->name = name;
		addProp(p);
		return *this;
	}

	template <auto Getter, auto PropGetter>
	builder& var_prop(const char* name) {
		using T = typename ResultOf<decltype(PropGetter)>::Type;
		auto* p = LUMIX_NEW(allocator, Property<T>)(allocator);
		p->setter = [](IScene* scene, EntityRef e, u32, const T& value) {
			using C = typename ClassOf<decltype(Getter)>::Type;
			auto& c = (static_cast<C*>(scene)->*Getter)(e);
			auto& v = c.*PropGetter;
			v = value;
		};
		p->getter = [](IScene* scene, EntityRef e, u32) -> T {
			using C = typename ClassOf<decltype(Getter)>::Type;
			auto& c = (static_cast<C*>(scene)->*Getter)(e);
			auto& v = c.*PropGetter;
			return static_cast<T>(v);
		};
		p->name = name;
		addProp(p);
		return *this;
	}


	template <auto Counter, auto Adder, auto Remover>
	builder& begin_array(const char* name) {
		ArrayProperty* prop = LUMIX_NEW(allocator, ArrayProperty)(allocator);
		using C = typename ClassOf<decltype(Counter)>::Type;
		prop->counter = [](IScene* scene, EntityRef e) -> u32 {
			return (static_cast<C*>(scene)->*Counter)(e);
		};
		prop->adder = [](IScene* scene, EntityRef e, u32 idx) {
			(static_cast<C*>(scene)->*Adder)(e, idx);
		};
		prop->remover = [](IScene* scene, EntityRef e, u32 idx) {
			(static_cast<C*>(scene)->*Remover)(e, idx);
		};
		prop->name = name;
		scene->cmps.back()->props.push(prop);
		array = prop;
		last_prop = prop;
		return *this;
	}

	template <typename T>
	builder& attribute() {
		auto* a = LUMIX_NEW(allocator, T);
		last_prop->attributes.push(a);
		return *this;
	}

	template <auto F>
	builder& function(const char* name, const char* decl_code) {
		auto* f = LUMIX_NEW(allocator, Function<decltype(F)>);
		f->function = F;
		f->name = name;
		f->decl_code = decl_code;
		if (scene->cmps.empty()) {
			scene->functions.push(f);
		}
		else {
			scene->cmps.back()->functions.push(f);
		}
		return *this;
	}

	void registerCmp(ComponentBase* cmp);

	builder& minAttribute(float value);
	builder& clampAttribute(float min, float max);
	builder& resourceAttribute(ResourceType type);
	builder& radiansAttribute();
	builder& colorAttribute();
	builder& noUIAttribute();
	builder& multilineAttribute();
	builder& icon(const char* icon);
	builder& end_array();

	void addProp(PropertyBase* prop);

	IAllocator& allocator;
	Scene* scene;
	ArrayProperty* array = nullptr;
	PropertyBase* last_prop = nullptr;
};

builder build_scene(const char* scene_name);

} // namespace reflection

} // namespace Lumix
