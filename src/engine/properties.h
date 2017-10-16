#pragma once


#include "engine/lumix.h"
#include "engine/blob.h"
#include "engine/resource.h"
#include "engine/universe/component.h"
#include <tuple>



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


namespace detail {
	template <class T> struct RemoveReference { typedef T type; };
	template <class T> struct RemoveReference<T&> { typedef T type; };
	template <class T> struct RemoveReference<T&&> { typedef T type; };
	template <class T> struct RemoveConst { typedef T type; };
	template <class T> struct RemoveConst<const T> { typedef T type; };

	template <class T> using RemoveCR = typename RemoveConst<typename RemoveReference<T>::type>::type;


	template<bool B, class T = void> struct EnableIf {};
	template<class T> struct EnableIf<true, T> { typedef T type; };
	template< bool B, class T = void> using EnableIf_t = typename EnableIf<B, T>::type;

	template <class F, class Tuple, std::size_t... I>
	constexpr void apply_impl(F& f, Tuple& t, std::index_sequence<I...>)
	{
		using expand = bool[];
		(void)expand
		{
			(
				f(std::get<I>(t)),
				true
				)...
		};
	}

	template <class F, class Tuple>
	constexpr void apply_impl(F& f, Tuple& t, std::index_sequence<>) {}

	template <typename T> struct ArgCount;
	template <typename R, typename C, typename... Args> struct ArgCount<R(C::*)(Args...)>
	{
		enum { value = sizeof...(Args) };
	};

	template <typename T> struct ResultOf;
	template <typename R, typename C, typename... Args> struct ResultOf<R(C::*)(Args...)> { using type = R; };

	template <typename T> struct ClassOf;
	template <typename R, typename C, typename... Args>
	struct ClassOf<R(C::*)(Args...)>
	{
		using type = C;
	};

	template <typename T> struct ArgOf2;
	template <typename R, typename C, typename A0, typename A1, typename A2, typename... Args>
	struct ArgOf2<R(C::*)(A0, A1, A2, Args...)>
	{
		using type = A2;
	};

	template <typename T> struct ArgOf1;
	template <typename R, typename C, typename A0, typename A1, typename... Args>
	struct ArgOf1<R(C::*)(A0, A1, Args...)>
	{
		using type = A1;
	};

	template <typename T> void writeToStream(OutputBlob& stream, T value)
	{
		stream.write(value);
	};
	
	template <typename T> T readFromStream(InputBlob& stream)
	{
		return stream.read<T>();
	};

	template <> Path readFromStream<Path>(InputBlob& stream);
	template <> void writeToStream<Path>(OutputBlob& stream, Path);
	template <> void writeToStream<const Path&>(OutputBlob& stream, const Path& path);
	template <> const char* readFromStream<const char*>(InputBlob& stream);
	template <> void writeToStream<const char*>(OutputBlob& stream, const char* path);


	template <typename Getter, typename Enabled = void> struct GetterProxy;

	template <typename Getter>
	struct GetterProxy<Getter, EnableIf_t<ArgCount<Getter>::value == 2>>
	{
		using C = typename ClassOf<Getter>::type;
		static auto invoke(OutputBlob& stream, C* inst, Getter getter, ComponentHandle cmp, int index)
		{
			auto value = (inst->*getter)(cmp, index);
			writeToStream(stream, value);
		}
	};

	template <typename Getter>
	struct GetterProxy<Getter, EnableIf_t<ArgCount<Getter>::value == 1>>
	{
		using C = typename ClassOf<Getter>::type;
		static void invoke(OutputBlob& stream, C* inst, Getter getter, ComponentHandle cmp, int index)
		{
			auto value = (inst->*getter)(cmp);
			writeToStream(stream, value);
		}
	};


	template <typename Setter, typename Enabled = void> struct SetterProxy;

	template <typename Setter>
	struct SetterProxy<Setter, EnableIf_t<ArgCount<Setter>::value == 3>>
	{
		using C = typename ClassOf<Setter>::type;
		static void invoke(InputBlob& stream, C* inst, Setter setter, ComponentHandle cmp, int index)
		{
			using Value = RemoveCR<typename ArgOf2<Setter>::type>;
			auto value = readFromStream<Value>(stream);
			(inst->*setter)(cmp, index, value);
		}
	};

	template <typename Setter>
	struct SetterProxy<Setter, EnableIf_t<ArgCount<Setter>::value == 2>>
	{
		using C = typename ClassOf<Setter>::type;
		static void invoke(InputBlob& stream, C* inst, Setter setter, ComponentHandle cmp, int index)
		{
			using Value = RemoveCR<typename ArgOf1<Setter>::type>;
			auto value = readFromStream<Value>(stream);
			(inst->*setter)(cmp, value);
		}
	};
} // namespace detail


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


struct IAttributeVisitor
{
	virtual void visit(const IAttribute& attr) = 0;
};


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


struct PropertyBase
{
	virtual void visit(IAttributeVisitor& visitor) const = 0;
	virtual void setValue(ComponentUID cmp, int index, InputBlob& stream) const = 0;
	virtual void getValue(ComponentUID cmp, int index, OutputBlob& stream) const = 0;

	const char* name;
};


template <typename T> struct Property : PropertyBase {};


struct IBlobProperty : PropertyBase {};


struct IEnumProperty : public PropertyBase
{
	void visit(IAttributeVisitor& visitor) const override {}
	virtual int getEnumCount(ComponentUID cmp) const = 0;
	virtual const char* getEnumName(ComponentUID cmp, int index) const = 0;
};




template <typename T, typename... Attributes> struct PropertyWithAttributes;


template <typename T, typename... Attributes>
struct PropertyWithAttributes<T, Attributes...> : Property<T>
{
	void visit(IAttributeVisitor& visitor) const override
	{
		apply([&](auto& x) { visitor.visit(x); }, attributes);
	}
	std::tuple<Attributes...> attributes;
};

template <typename T>
struct PropertyWithAttributes<T> : Property<T>
{
	void visit(IAttributeVisitor& visitor) const override {}

	std::tuple<> attributes;
};


struct IComponentVisitor;


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
};


struct IComponentDescriptor
{
	virtual ComponentType getComponentType() const = 0;
	virtual void visit(IComponentVisitor&) const = 0;
};


template <typename Getter, typename Setter, typename Namer, typename Enabled = void>
struct EnumProperty;


template <typename Getter, typename Setter, typename Namer>
struct EnumProperty<Getter, Setter, Namer, detail::EnableIf_t<detail::ArgCount<Getter>::value == 2>> : IEnumProperty
{
	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::type;
		C* x = static_cast<C*>(cmp.scene);
		int value = static_cast<int>((x->*getter)(cmp.handle, index));
		stream.write(value);
	}

	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::type;
		C* x = static_cast<C*>(cmp.scene);

		using T = typename detail::ResultOf<Getter>::type;
		int value = stream.read<int>();
		(x->*setter)(cmp.handle, index, static_cast<T>(value));
	}


	int getEnumCount(ComponentUID cmp) const override
	{
		return count;
	}


	const char* getEnumName(ComponentUID cmp, int index) const override
	{
		return namer(index);
	}

	const char* getName() override { return name; }

	const char* name;
	Getter getter;
	Setter setter;
	int count;
	Namer namer;
};


template <typename Getter, typename Setter, typename Namer>
struct EnumProperty<Getter, Setter, Namer, detail::EnableIf_t<detail::ArgCount<Getter>::value == 1>> : IEnumProperty
{
	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::type;
		C* x = static_cast<C*>(cmp.scene);
		int value = static_cast<int>((x->*getter)(cmp.handle));
		stream.write(value);
	}

	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::type;
		C* x = static_cast<C*>(cmp.scene);

		using T = typename detail::ResultOf<Getter>::type;
		int value = stream.read<int>();
		(x->*setter)(cmp.handle, static_cast<T>(value));
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


template <typename Getter, typename Setter, typename... Attributes>
struct BlobProperty : IBlobProperty
{
	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::type;
		C* x = static_cast<C*>(cmp.scene);
		(x->*getter)(cmp.handle, stream);
	}

	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::type;
		C* x = static_cast<C*>(cmp.scene);
		(x->*setter)(cmp.handle, stream);
	}


	void visit(IAttributeVisitor& visitor) const override {
		apply([&](auto& x) { visitor.visit(x); }, attributes);
	}


	std::tuple<Attributes...> attributes;
	Getter getter;
	Setter setter;
};


template <typename T, typename Getter, typename Setter, typename... Attributes>
struct PropertyX : PropertyWithAttributes<T, Attributes...>
{
	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::type;
		C* x = static_cast<C*>(cmp.scene);
		detail::GetterProxy<Getter>::invoke(stream, x, getter, cmp.handle, index);
	}

	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		using C = typename detail::ClassOf<Getter>::type;
		C* x = static_cast<C*>(cmp.scene);
		detail::SetterProxy<Setter>::invoke(stream, x, setter, cmp.handle, index);
	}

	Getter getter;
	Setter setter;
};

template <class F, class Tuple>
constexpr void apply(F& f, Tuple& t)
{
	detail::apply_impl(f, t, std::make_index_sequence<std::tuple_size_v<std::decay_t<Tuple>>>{});
}

template <typename Counter, typename Adder, typename Remover, typename... Properties>
struct ArrayProperty : IArrayProperty
{
	std::tuple<Properties...> properties;


	ArrayProperty() {
	}


	bool canAddRemove() const override { return true; }


	ArrayProperty(const char* name, Counter counter, Adder adder, Remover remover, Properties... properties)
	{
		this->properties = std::make_tuple(properties...);
		this->name = name;
		this->counter = counter;
		this->remover = remover;
		this->adder = adder;
	}


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
		using C = typename detail::ClassOf<Counter>::type;
		C* x = static_cast<C*>(cmp.scene);

		(x->*adder)(cmp.handle, index);

	}


	void removeItem(ComponentUID cmp, int index) const override
	{
		using C = typename detail::ClassOf<Counter>::type;
		C* x = static_cast<C*>(cmp.scene);

		(x->*remover)(cmp.handle, index);
	}


	int getCount(ComponentUID cmp) const override
	{ 
		using C = typename detail::ClassOf<Counter>::type;
		C* x = static_cast<C*>(cmp.scene);

		return (x->*counter)(cmp.handle);
	}


	void visit(IComponentVisitor& visitor) const override
	{
		apply([&](auto& x) { visitor.visit(x); }, properties);
	}


	void visit(IAttributeVisitor& visitor) const override {
	
	}


	Counter counter;
	Adder adder;
	Remover remover;
};

template <typename Counter, typename... Properties>
struct ConstArrayProperty : IArrayProperty
{
	std::tuple<Properties...> properties;


	ConstArrayProperty() {
	}


	bool canAddRemove() const override { return false; }

	ConstArrayProperty(const char* name, Counter counter, Properties... properties)
	{
		this->properties = std::make_tuple(properties...);
		this->name = name;
		this->counter = counter;
	}


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
		using C = typename detail::ClassOf<Counter>::type;
		C* x = static_cast<C*>(cmp.scene);

		return (x->*counter)(cmp.handle);
	}


	void visit(IComponentVisitor& visitor) const override
	{
		apply([&](auto& x) { visitor.visit(x); }, properties);
	}


	void visit(IAttributeVisitor& visitor) const override {

	}


	Counter counter;
};


template <typename... Properties>
struct ComponentDesciptor : IComponentDescriptor
{
	std::tuple<Properties...> properties;
	const char* name;
	ComponentType cmp_type;


	ComponentType getComponentType() const override { return cmp_type; }


	ComponentDesciptor(const char* name, Properties... properties)
	{
		this->properties = std::make_tuple(properties...);
		this->name = name;
		this->cmp_type = Properties::getComponentType(name);
	}


	void visit(IComponentVisitor& visitor) const override
	{
		apply([&](auto& x) { visitor.visit(x); }, properties);
	}
};


template <typename... Properties>
ComponentDesciptor<Properties...> component(const char* name, Properties... props)
{
	return ComponentDesciptor<Properties...>(name, props...);
}


template <typename Getter, typename Setter, typename... Attributes>
BlobProperty<Getter, Setter, Attributes...>
blob_property(const char* name, Getter getter, Setter setter, Attributes... attributes)
{
	BlobProperty<Getter, Setter, Attributes...> p;
	p.attributes = std::make_tuple(attributes...);
	p.getter = getter;
	p.setter = setter;
	p.name = name;
	return p;
}


template <typename Getter, typename Setter, typename... Attributes>
auto property(const char* name, Getter getter, Setter setter, Attributes... attributes)
{
	using R = typename detail::ResultOf<Getter>::type;
	PropertyX<R, Getter, Setter, Attributes...> p;
	p.attributes = std::make_tuple(attributes...);
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


template <typename Counter, typename Adder, typename Remover, typename... Properties>
auto array(const char* name, Counter counter, Adder adder, Remover remover, Properties... properties)
{
	ArrayProperty<Counter, Adder, Remover, Properties...> p(name, counter, adder, remover, properties...);
	return p;
}


template <typename Counter, typename... Properties>
auto const_array(const char* name, Counter counter, Properties... properties)
{
	ConstArrayProperty<Counter, Properties...> p(name, counter, properties...);
	return p;
}


LUMIX_ENGINE_API const IAttribute* getAttribute(const PropertyBase& prop, IAttribute::Type type);
LUMIX_ENGINE_API void init(IAllocator& allocator);
LUMIX_ENGINE_API void shutdown();


LUMIX_ENGINE_API void registerComponent(IComponentDescriptor* desc);
LUMIX_ENGINE_API IComponentDescriptor* getComponent(ComponentType cmp_type);
LUMIX_ENGINE_API const PropertyBase* getProperty(ComponentType cmp_type, const char* property);
LUMIX_ENGINE_API const PropertyBase* getProperty(ComponentType cmp_type, u32 property_name_hash);
LUMIX_ENGINE_API const PropertyBase* getProperty(ComponentType cmp_type, const char* property, const char* subproperty);


LUMIX_ENGINE_API ComponentType getComponentType(const char* id);
LUMIX_ENGINE_API u32 getComponentTypeHash(ComponentType type);
LUMIX_ENGINE_API ComponentType getComponentTypeFromHash(u32 hash);
LUMIX_ENGINE_API int getComponentTypesCount();
LUMIX_ENGINE_API const char* getComponentTypeID(int index);



} // namespace Properties


} // namespace Lumix
