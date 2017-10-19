#pragma once


#include "engine/lumix.h"
#include "engine/blob.h"
#include "engine/resource.h"
#include "engine/universe/component.h"
#include "engine/metaprogramming.h"


namespace Lumix
{


template <typename T> class Array;
struct IAllocator;
class Path;
class PropertyDescriptorBase;
struct Int2;
struct Vec2;
struct Vec3;
struct Vec4;


namespace Properties
{


struct IAttribute
{
	enum Type
	{
		MIN,
		CLAMP,
		RADIANS,
		COLOR,
		RESOURCE
	};

	virtual int getType() const = 0;
};

struct ComponentBase;
struct IComponentVisitor;

struct IAttributeVisitor
{
	virtual void visit(const IAttribute& attr) = 0;
};

struct PropertyBase
{
	virtual void visit(IAttributeVisitor& visitor) const = 0;
	virtual void setValue(ComponentUID cmp, int index, InputBlob& stream) const = 0;
	virtual void getValue(ComponentUID cmp, int index, OutputBlob& stream) const = 0;

	const char* name;
};


LUMIX_ENGINE_API void init(IAllocator& allocator);
LUMIX_ENGINE_API void shutdown();


LUMIX_ENGINE_API const IAttribute* getAttribute(const PropertyBase& prop, IAttribute::Type type);
LUMIX_ENGINE_API void registerComponent(const ComponentBase* desc);
LUMIX_ENGINE_API const ComponentBase* getComponent(ComponentType cmp_type);
LUMIX_ENGINE_API const PropertyBase* getProperty(ComponentType cmp_type, const char* property);
LUMIX_ENGINE_API const PropertyBase* getProperty(ComponentType cmp_type, u32 property_name_hash);
LUMIX_ENGINE_API const PropertyBase* getProperty(ComponentType cmp_type, const char* property, const char* subproperty);


LUMIX_ENGINE_API ComponentType getComponentType(const char* id);
LUMIX_ENGINE_API u32 getComponentTypeHash(ComponentType type);
LUMIX_ENGINE_API ComponentType getComponentTypeFromHash(u32 hash);
LUMIX_ENGINE_API int getComponentTypesCount();
LUMIX_ENGINE_API const char* getComponentTypeID(int index);


namespace detail 
{


template <typename T> struct ResultOf;
template <typename R, typename C, typename... Args> struct ResultOf<R(C::*)(Args...)> { using Type = R; };

template <typename T> struct ClassOf;
template <typename R, typename C, typename... Args>
struct ClassOf<R(C::*)(Args...)>
{
	using Type = C;
};

template <typename T> void writeToStream(OutputBlob& stream, T value)
{
	stream.write(value);
};
	
template <typename T> T readFromStream(InputBlob& stream)
{
	return stream.read<T>();
};

template <> LUMIX_ENGINE_API Path readFromStream<Path>(InputBlob& stream);
template <> LUMIX_ENGINE_API void writeToStream<Path>(OutputBlob& stream, Path);
template <> LUMIX_ENGINE_API void writeToStream<const Path&>(OutputBlob& stream, const Path& path);
template <> LUMIX_ENGINE_API const char* readFromStream<const char*>(InputBlob& stream);
template <> LUMIX_ENGINE_API void writeToStream<const char*>(OutputBlob& stream, const char* path);


template <typename Getter> struct GetterProxy;

template <typename R, typename C>
struct GetterProxy<R(C::*)(ComponentHandle, int)>
{
	using Getter = R(C::*)(ComponentHandle, int);
	static void invoke(OutputBlob& stream, C* inst, Getter getter, ComponentHandle cmp, int index)
	{
		R value = (inst->*getter)(cmp, index);
		writeToStream(stream, value);
	}
};

template <typename R, typename C>
struct GetterProxy<R(C::*)(ComponentHandle)>
{
	using Getter = R(C::*)(ComponentHandle);
	static void invoke(OutputBlob& stream, C* inst, Getter getter, ComponentHandle cmp, int index)
	{
		R value = (inst->*getter)(cmp);
		writeToStream(stream, value);
	}
};


template <typename Setter> struct SetterProxy;

template <typename C, typename A>
struct SetterProxy<void (C::*)(ComponentHandle, int, A)>
{
	using Setter = void (C::*)(ComponentHandle, int, A);
	static void invoke(InputBlob& stream, C* inst, Setter setter, ComponentHandle cmp, int index)
	{
		using Value = RemoveCR<A>;
		auto value = readFromStream<Value>(stream);
		(inst->*setter)(cmp, index, value);
	}
};

template <typename C, typename A>
struct SetterProxy<void (C::*)(ComponentHandle, A)>
{
	using Setter = void (C::*)(ComponentHandle, A);
	static void invoke(InputBlob& stream, C* inst, Setter setter, ComponentHandle cmp, int index)
	{
		using Value = RemoveCR<A>;
		auto value = readFromStream<Value>(stream);
		(inst->*setter)(cmp, value);
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
	MinAttribute(float min) { this->min = min; }
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


struct RadiansAttribute : IAttribute
{
	int getType() const override { return RADIANS; }
};


struct ColorAttribute : IAttribute
{
	int getType() const override { return COLOR; }
};


template <typename T> struct Property : PropertyBase {};


struct IBlobProperty : PropertyBase {};


struct ISampledFuncProperty : PropertyBase
{
	virtual float getMaxX() const = 0;
};


struct IEnumProperty : public PropertyBase
{
	void visit(IAttributeVisitor& visitor) const override {}
	virtual int getEnumCount(ComponentUID cmp) const = 0;
	virtual const char* getEnumName(ComponentUID cmp, int index) const = 0;
};


struct IArrayProperty : PropertyBase
{
	virtual bool canAddRemove() const = 0;
	virtual void addItem(ComponentUID cmp, int index) const = 0;
	virtual void removeItem(ComponentUID cmp, int index) const = 0;
	virtual int getCount(ComponentUID cmp) const = 0;
	virtual void visit(IComponentVisitor& visitor) const = 0;
};


struct IComponentVisitor
{
	virtual void begin(const ComponentBase&) {}
	virtual void visit(const Property<float>& prop) = 0;
	virtual void visit(const Property<int>& prop) = 0;
	virtual void visit(const Property<Entity>& prop) = 0;
	virtual void visit(const Property<Int2>& prop) = 0;
	virtual void visit(const Property<Vec2>& prop) = 0;
	virtual void visit(const Property<Vec3>& prop) = 0;
	virtual void visit(const Property<Vec4>& prop) = 0;
	virtual void visit(const Property<Path>& prop) = 0;
	virtual void visit(const Property<bool>& prop) = 0;
	virtual void visit(const Property<const char*>& prop) = 0;
	virtual void visit(const IArrayProperty& prop) = 0;
	virtual void visit(const IEnumProperty& prop) = 0;
	virtual void visit(const IBlobProperty& prop) = 0;
	virtual void visit(const ISampledFuncProperty& prop) = 0;
	virtual void end(const ComponentBase&) {}
};


struct ISimpleComponentVisitor : IComponentVisitor
{
	virtual void visitProperty(const PropertyBase& prop) = 0;

	void visit(const Property<float>& prop) override { visitProperty(prop); }
	void visit(const Property<int>& prop) override { visitProperty(prop); }
	void visit(const Property<Entity>& prop) override { visitProperty(prop); }
	void visit(const Property<Int2>& prop) override { visitProperty(prop); }
	void visit(const Property<Vec2>& prop) override { visitProperty(prop); }
	void visit(const Property<Vec3>& prop) override { visitProperty(prop); }
	void visit(const Property<Vec4>& prop) override { visitProperty(prop); }
	void visit(const Property<Path>& prop) override { visitProperty(prop); }
	void visit(const Property<bool>& prop) override { visitProperty(prop); }
	void visit(const Property<const char*>& prop) override { visitProperty(prop); }
	void visit(const IArrayProperty& prop) override { visitProperty(prop); }
	void visit(const IEnumProperty& prop) override { visitProperty(prop); }
	void visit(const IBlobProperty& prop) override { visitProperty(prop); }
	void visit(const ISampledFuncProperty& prop) override { visitProperty(prop); }
};


struct ComponentBase
{
	virtual int getPropertyCount() const = 0;
	virtual void visit(IComponentVisitor&) const = 0;

	const char* name;
	ComponentType component_type;
};


template <typename Getter, typename Setter, typename Namer>
struct EnumProperty : IEnumProperty
{
	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		static_assert(4 == sizeof(detail::ResultOf<Getter>::Type), "enum must have 4 bytes");
		detail::GetterProxy<Getter>::invoke(stream, inst, getter, cmp.handle, index);
	}

	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);

		static_assert(4 == sizeof(detail::ResultOf<Getter>::Type), "enum must have 4 bytes");
		detail::SetterProxy<Setter>::invoke(stream, inst, setter, cmp.handle, index);
	}


	int getEnumCount(ComponentUID cmp) const override
	{
		return count;
	}


	const char* getEnumName(ComponentUID cmp, int index) const override
	{
		return namer(index);
	}

	Getter getter;
	Setter setter;
	int count;
	Namer namer;
};


template <typename Getter, typename Setter, typename Counter, typename Namer>
struct DynEnumProperty : IEnumProperty
{
	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		static_assert(4 == sizeof(detail::ResultOf<Getter>::Type), "enum must have 4 bytes");
		detail::GetterProxy<Getter>::invoke(stream, inst, getter, cmp.handle, index);
	}

	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);

		static_assert(4 == sizeof(detail::ResultOf<Getter>::Type), "enum must have 4 bytes");
		detail::SetterProxy<Setter>::invoke(stream, inst, setter, cmp.handle, index);
	}


	int getEnumCount(ComponentUID cmp) const override
	{
		using C = typename detail::ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		return (inst->*counter)();
	}


	const char* getEnumName(ComponentUID cmp, int index) const override
	{
		using C = typename detail::ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		return (inst->*namer)(index);
	}

	Getter getter;
	Setter setter;
	Counter counter;
	Namer namer;
};


template <typename Getter, typename Setter, typename Counter>
struct SampledFuncProperty : ISampledFuncProperty
{
	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		ASSERT(index == -1);
		using C = typename detail::ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		int count = (inst->*counter)(cmp.handle);
		stream.write(count);
		const Vec2* values = (inst->*getter)(cmp.handle);
		stream.write(values, sizeof(values[0]) * count);
	}

	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		ASSERT(index == -1);
		using C = typename detail::ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		int count;
		stream.read(count);
		auto* buf = (const Vec2*)stream.skip(sizeof(Vec2) * count);
		(inst->*setter)(cmp.handle, buf, count);
	}

	float getMaxX() const override { return max_x; }

	void visit(IAttributeVisitor& visitor) const override {}

	Getter getter;
	Setter setter;
	Counter counter;
	float max_x;
};


template <typename Getter, typename Setter, typename... Attributes>
struct BlobProperty : IBlobProperty
{
	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		(inst->*getter)(cmp.handle, stream);
	}

	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		(inst->*setter)(cmp.handle, stream);
	}

	void visit(IAttributeVisitor& visitor) const override {
		apply([&](auto& x) { visitor.visit(x); }, attributes);
	}

	Tuple<Attributes...> attributes;
	Getter getter;
	Setter setter;
};


template <typename T, typename Getter, typename Setter, typename... Attributes>
struct CommonProperty : Property<T>
{
	void visit(IAttributeVisitor& visitor) const override
	{
		apply([&](auto& x) { visitor.visit(x); }, attributes);
	}

	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		detail::GetterProxy<Getter>::invoke(stream, inst, getter, cmp.handle, index);
	}

	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		detail::SetterProxy<Setter>::invoke(stream, inst, setter, cmp.handle, index);
	}


	Tuple<Attributes...> attributes;
	Getter getter;
	Setter setter;
};


template <typename Counter, typename Adder, typename Remover, typename... Props>
struct ArrayProperty : IArrayProperty
{
	ArrayProperty() {}


	bool canAddRemove() const override { return true; }


	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		ASSERT(index == -1);

		int count;
		stream.read(count);
		while (getCount(cmp) < count)
		{
			addItem(cmp, -1);
		}
		while (getCount(cmp) > count)
		{
			removeItem(cmp, getCount(cmp) - 1);
		}
		for (int i = 0; i < count; ++i)
		{
			struct : ISimpleComponentVisitor
			{
				void visitProperty(const PropertyBase& prop) override
				{
					prop.setValue(cmp, index, *stream);
				}

				InputBlob* stream;
				int index;
				ComponentUID cmp;

			} v;
			v.stream = &stream;
			v.index = i;
			v.cmp = cmp;
			visit(v);
		}
	}


	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		ASSERT(index == -1);
		int count = getCount(cmp);
		stream.write(count);
		for (int i = 0; i < count; ++i)
		{
			struct : ISimpleComponentVisitor
			{
				void visitProperty(const PropertyBase& prop) override
				{
					prop.getValue(cmp, index, *stream);
				}

				OutputBlob* stream;
				int index;
				ComponentUID cmp;

			} v;
			v.stream = &stream;
			v.index = i;
			v.cmp = cmp;
			visit(v);
		}
	}


	void addItem(ComponentUID cmp, int index) const override
	{
		using C = typename detail::ClassOf<Counter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		(inst->*adder)(cmp.handle, index);
	}


	void removeItem(ComponentUID cmp, int index) const override
	{
		using C = typename detail::ClassOf<Counter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		(inst->*remover)(cmp.handle, index);
	}


	int getCount(ComponentUID cmp) const override
	{ 
		using C = typename detail::ClassOf<Counter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		return (inst->*counter)(cmp.handle);
	}


	void visit(IComponentVisitor& visitor) const override
	{
		apply([&](auto& x) { visitor.visit(x); }, properties);
	}


	void visit(IAttributeVisitor& visitor) const override {}


	Tuple<Props...> properties;
	Counter counter;
	Adder adder;
	Remover remover;
};


template <typename Counter, typename... Props>
struct ConstArrayProperty : IArrayProperty
{
	ConstArrayProperty() {}


	bool canAddRemove() const override { return false; }


	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		ASSERT(index == -1);

		int count;
		stream.read(count);
		if (getCount(cmp) != count) return;
		
		for (int i = 0; i < count; ++i)
		{
			struct : ISimpleComponentVisitor
			{
				void visitProperty(const PropertyBase& prop) override
				{
					prop.setValue(cmp, index, *stream);
				}

				InputBlob* stream;
				int index;
				ComponentUID cmp;

			} v;
			v.stream = &stream;
			v.index = i;
			v.cmp = cmp;
			visit(v);
		}
	}


	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		ASSERT(index == -1);
		int count = getCount(cmp);
		stream.write(count);
		for (int i = 0; i < count; ++i)
		{
			struct : ISimpleComponentVisitor
			{
				void visitProperty(const PropertyBase& prop) override
				{
					prop.getValue(cmp, index, *stream);
				}

				OutputBlob* stream;
				int index;
				ComponentUID cmp;

			} v;
			v.stream = &stream;
			v.index = i;
			v.cmp = cmp;
			visit(v);
		}
	}


	void addItem(ComponentUID cmp, int index) const override { ASSERT(false); }
	void removeItem(ComponentUID cmp, int index) const override { ASSERT(false); }


	int getCount(ComponentUID cmp) const override
	{
		using C = typename detail::ClassOf<Counter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		return (inst->*counter)(cmp.handle);
	}


	void visit(IComponentVisitor& visitor) const override
	{
		apply([&](auto& x) { visitor.visit(x); }, properties);
	}


	void visit(IAttributeVisitor& visitor) const override {

	}


	Tuple<Props...> properties;
	Counter counter;
};


template <typename... Components>
struct Scene
{
	void registerScene()
	{
		apply([&](auto& cmp) { registerComponent(&cmp); }, components);
	}


	Tuple<Components...> components;
	const char* name;
};


template <typename... Props>
struct Component : ComponentBase
{
	int getPropertyCount() const override { return sizeof...(Props); }


	void visit(IComponentVisitor& visitor) const override
	{
		visitor.begin(*this);
		apply([&](auto& x) { visitor.visit(x); }, properties);
		visitor.end(*this);
	}


	Tuple<Props...> properties;
};


template <typename... Components>
auto scene(const char* name, Components... components)
{
	Scene<Components...> scene;
	scene.name = name;
	scene.components = makeTuple(components...);
	return scene;
}


template <typename... Props>
auto component(const char* name, Props... props)
{
	Component<Props...> cmp;
	cmp.name = name;
	cmp.properties = makeTuple(props...);
	cmp.component_type = getComponentType(name);
	return cmp;
}


template <typename Getter, typename Setter, typename... Attributes>
auto blob_property(const char* name, Getter getter, Setter setter, Attributes... attributes)
{
	BlobProperty<Getter, Setter, Attributes...> p;
	p.attributes = makeTuple(attributes...);
	p.getter = getter;
	p.setter = setter;
	p.name = name;
	return p;
}


template <typename Getter, typename Setter, typename Counter>
auto sampled_func_property(const char* name, Getter getter, Setter setter, Counter counter, float max_x)
{
	using R = typename detail::ResultOf<Getter>::Type;
	SampledFuncProperty<Getter, Setter, Counter> p;
	p.getter = getter;
	p.setter = setter;
	p.counter = counter;
	p.name = name;
	p.max_x = max_x;
	return p;
}


template <typename Getter, typename Setter, typename... Attributes>
auto property(const char* name, Getter getter, Setter setter, Attributes... attributes)
{
	using R = typename detail::ResultOf<Getter>::Type;
	CommonProperty<R, Getter, Setter, Attributes...> p;
	p.attributes = makeTuple(attributes...);
	p.getter = getter;
	p.setter = setter;
	p.name = name;
	return p;
}


template <typename Getter, typename Setter, typename Namer, typename... Attributes>
auto enum_property(const char* name, Getter getter, Setter setter, int count, Namer namer, Attributes... attributes)
{
	EnumProperty<Getter, Setter, Namer> p;
	p.getter = getter;
	p.setter = setter;
	p.namer = namer;
	p.count = count;
	p.name = name;
	return p;
}


template <typename Getter, typename Setter, typename Counter, typename Namer, typename... Attributes>
auto dyn_enum_property(const char* name, Getter getter, Setter setter, Counter counter, Namer namer, Attributes... attributes)
{
	DynEnumProperty<Getter, Setter, Counter, Namer> p;
	p.getter = getter;
	p.setter = setter;
	p.namer = namer;
	p.counter = counter;
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


template <typename Counter, typename... Props>
auto const_array(const char* name, Counter counter, Props... properties)
{
	ConstArrayProperty<Counter, Props...> p;
	p.name = name;
	p.counter = counter;
	p.properties = makeTuple(properties...);
	return p;
}


} // namespace Properties


} // namespace Lumix
