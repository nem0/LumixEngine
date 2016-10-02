#pragma once

#include "engine/blob.h"
#include "engine/iplugin.h"
#include "engine/iproperty_descriptor.h"
#include "engine/path.h"
#include "engine/resource.h"
#include "engine/string.h"
#include "engine/universe/universe.h"


namespace Lumix
{


LUMIX_ENGINE_API int getIntPropertyMin();
LUMIX_ENGINE_API int getIntPropertyMax();


template <typename T> inline IPropertyDescriptor::Type toPropertyType();
template <> inline IPropertyDescriptor::Type toPropertyType<int>() { return IPropertyDescriptor::INTEGER; }
template <> inline IPropertyDescriptor::Type toPropertyType<Int2>() { return IPropertyDescriptor::INT2; }
template <> inline IPropertyDescriptor::Type toPropertyType<Vec2>() { return IPropertyDescriptor::VEC2; }
template <> inline IPropertyDescriptor::Type toPropertyType<Vec3>() { return IPropertyDescriptor::VEC3; }
template <> inline IPropertyDescriptor::Type toPropertyType<Vec4>() { return IPropertyDescriptor::VEC4; }



template <class S> class StringPropertyDescriptor : public IPropertyDescriptor
{
private:
	static const int MAX_STRING_SIZE = 300;

public:
	typedef const char* (S::*Getter)(ComponentHandle);
	typedef void (S::*Setter)(ComponentHandle, const char*);
	typedef const char* (S::*ArrayGetter)(ComponentHandle, int);
	typedef void (S::*ArraySetter)(ComponentHandle, int, const char*);

public:
	StringPropertyDescriptor(const char* name, Getter getter, Setter setter)
	{
		setName(name);
		m_single.getter = getter;
		m_single.setter = setter;
		m_type = IPropertyDescriptor::STRING;
	}


	StringPropertyDescriptor(const char* name, ArrayGetter getter, ArraySetter setter, IAllocator& allocator)
		: IPropertyDescriptor(allocator)
	{
		setName(name);
		m_array.getter = getter;
		m_array.setter = setter;
		m_type = IPropertyDescriptor::STRING;
	}


	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		char tmp[MAX_STRING_SIZE];
		char* c = tmp;
		do
		{
			stream.read(c, 1);
			++c;
		} while (*(c - 1) && (c - 1) - tmp < MAX_STRING_SIZE);

		if (index < 0)
		{
			(static_cast<S*>(cmp.scene)->*m_single.setter)(cmp.handle, tmp);
		}
		else
		{
			(static_cast<S*>(cmp.scene)->*m_array.setter)(cmp.handle, index, tmp);
		}
	}


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		const char* value;
		if (index < 0)
		{
			value = (static_cast<S*>(cmp.scene)->*m_single.getter)(cmp.handle);
		}
		else
		{
			value = (static_cast<S*>(cmp.scene)->*m_array.getter)(cmp.handle, index);
		}
		int len = stringLength(value) + 1;
		stream.write(value, len);
	}


private:
	union
	{
		struct
		{
			Getter getter;
			Setter setter;
		} m_single;
		struct
		{
			ArrayGetter getter;
			ArraySetter setter;
		} m_array;
	};
};


template <class S> class ArrayDescriptor : public IArrayDescriptor
{
public:
	typedef int (S::*Counter)(ComponentHandle);
	typedef void (S::*Adder)(ComponentHandle, int);
	typedef void (S::*Remover)(ComponentHandle, int);

public:
	ArrayDescriptor(const char* name,
		Counter counter,
		Adder adder,
		Remover remover,
		IAllocator& allocator)
		: IArrayDescriptor(allocator)
	{
		setName(name);
		m_type = ARRAY;
		m_counter = counter;
		m_adder = adder;
		m_remover = remover;
	}


	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		ASSERT(index == -1);

		int count;
		stream.read(count);
		if (m_adder)
		{
			while (getCount(cmp) < count)
			{
				addArrayItem(cmp, -1);
			}
		}
		if (m_remover)
		{
			while (getCount(cmp) > count)
			{
				removeArrayItem(cmp, getCount(cmp) - 1);
			}
		}
		for (int i = 0; i < count; ++i)
		{
			for (int j = 0, cj = getChildren().size(); j < cj; ++j)
			{
				getChildren()[j]->set(cmp, i, stream);
			}
		}
	}


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		ASSERT(index == -1);
		int count = getCount(cmp);
		stream.write(count);
		for (int i = 0; i < count; ++i)
		{
			for (int j = 0, cj = getChildren().size(); j < cj; ++j)
			{
				getChildren()[j]->get(cmp, i, stream);
			}
		}
	}


	int getCount(ComponentUID cmp) const override
	{
		return (static_cast<S*>(cmp.scene)->*m_counter)(cmp.handle);
	}


	void addArrayItem(ComponentUID cmp, int index) const override
	{
		if (m_adder) (static_cast<S*>(cmp.scene)->*m_adder)(cmp.handle, index);
	}


	void removeArrayItem(ComponentUID cmp, int index) const override
	{
		(static_cast<S*>(cmp.scene)->*m_remover)(cmp.handle, index);
	}


	bool canAdd() const override
	{
		return m_adder != nullptr;
	}


	bool canRemove() const override
	{
		return m_remover != nullptr;
	}


private:
	Counter m_counter;
	Adder m_adder;
	Remover m_remover;
};


template <class S> class IntPropertyDescriptor : public IPropertyDescriptor
{
public:
	typedef int (S::*Getter)(ComponentHandle);
	typedef void (S::*Setter)(ComponentHandle, int);
	typedef int (S::*ArrayGetter)(ComponentHandle, int);
	typedef void (S::*ArraySetter)(ComponentHandle, int, int);

public:
	IntPropertyDescriptor() {}

	IntPropertyDescriptor(const char* name, Getter _getter, Setter _setter, IAllocator& allocator)
		: IPropertyDescriptor(allocator)
	{
		setName(name);
		m_single.getter = _getter;
		m_single.setter = _setter;
		m_type = INTEGER;
		m_min = getIntPropertyMin();
		m_max = getIntPropertyMax();
	}


	IntPropertyDescriptor(const char* name, ArrayGetter _getter, ArraySetter _setter)
	{
		setName(name);
		m_array.getter = _getter;
		m_array.setter = _setter;
		m_type = INTEGER;
		m_min = getIntPropertyMin();
		m_max = getIntPropertyMax();
	}


	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		int32 i;
		stream.read(&i, sizeof(i));
		if(index < 0)
		{
			(static_cast<S*>(cmp.scene)->*m_single.setter)(cmp.handle, i);
		}
		else
		{
			(static_cast<S*>(cmp.scene)->*m_array.setter)(cmp.handle, index, i);
		}
	};


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		int32 i = 0;
		if(index < 0)
		{
			i = (static_cast<S*>(cmp.scene)->*m_single.getter)(cmp.handle);
		}
		else
		{
			i = (static_cast<S*>(cmp.scene)->*m_array.getter)(cmp.handle, index);
		}
		stream.write(i);
	};


	void setLimit(int min, int max)
	{
		m_min = min;
		m_max = max;
	}


	int getMin() const { return m_min; }
	int getMax() const { return m_max; }


private:
	union
	{
		struct
		{
			Getter getter;
			Setter setter;
		} m_single;
		struct
		{
			ArrayGetter getter;
			ArraySetter setter;
		} m_array;
	};

	int m_min;
	int m_max;
};


template <class S> class BoolPropertyDescriptor : public IPropertyDescriptor
{
public:
	typedef bool (S::*Getter)(ComponentHandle);
	typedef void (S::*Setter)(ComponentHandle, bool);

public:
	BoolPropertyDescriptor(const char* name, Getter getter, Setter setter)
	{
		setName(name);
		m_getter = getter;
		m_setter = setter;
		m_type = IPropertyDescriptor::BOOL;
	}


	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		ASSERT(index == -1);
		bool b;
		stream.read(&b, sizeof(b));
		(static_cast<S*>(cmp.scene)->*m_setter)(cmp.handle, b);
	};


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		ASSERT(index == -1);
		bool b = (static_cast<S*>(cmp.scene)->*m_getter)(cmp.handle);
		int len = sizeof(b);
		stream.write(&b, len);
	};

private:
	Getter m_getter;
	Setter m_setter;
};


template <typename T, class S> class SimplePropertyDescriptor : public IPropertyDescriptor
{
public:
	typedef T (S::*Getter)(ComponentHandle);
	typedef void (S::*Setter)(ComponentHandle, const T&);
	typedef T(S::*ArrayGetter)(ComponentHandle, int);
	typedef void (S::*ArraySetter)(ComponentHandle, int, const T&);

public:
	SimplePropertyDescriptor(const char* name, Getter getter, Setter setter)
	{
		setName(name);
		m_single.getter = getter;
		m_single.setter = setter;
		m_type = toPropertyType<T>();
	}


	SimplePropertyDescriptor(const char* name, ArrayGetter getter, ArraySetter setter, IAllocator& allocator)
		: IPropertyDescriptor(allocator)
	{
		setName(name);
		m_array.getter = getter;
		m_array.setter = setter;
		m_type = toPropertyType<T>();
	}


	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		T v;
		stream.read(&v, sizeof(v));
		if (index < 0)
		{
			(static_cast<S*>(cmp.scene)->*m_single.setter)(cmp.handle, v);
		}
		else
		{
			(static_cast<S*>(cmp.scene)->*m_array.setter)(cmp.handle, index, v);
		}
	};


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		int len = sizeof(T);
		if (index < 0)
		{
			T v = (static_cast<S*>(cmp.scene)->*m_single.getter)(cmp.handle);
			stream.write(&v, len);
		}
		else
		{
			T v = (static_cast<S*>(cmp.scene)->*m_array.getter)(cmp.handle, index);
			stream.write(&v, len);
		}
	};


private:
	union
	{
		struct
		{
			Getter getter;
			Setter setter;
		} m_single;
		struct
		{
			ArrayGetter getter;
			ArraySetter setter;
		} m_array;
	};
};


template <class S>
class FilePropertyDescriptor : public IPropertyDescriptor
{
public:
	typedef Path (S::*Getter)(ComponentHandle);
	typedef void (S::*Setter)(ComponentHandle, const Path&);
	typedef Path (S::*ArrayGetter)(ComponentHandle, int);
	typedef void (S::*ArraySetter)(ComponentHandle, int, const Path&);

public:
	FilePropertyDescriptor(const char* name, Getter getter, Setter setter, const char* file_type)
	{
		setName(name);
		m_single.getter = getter;
		m_single.setter = setter;
		m_type = IPropertyDescriptor::FILE;
		copyString(m_file_type, file_type);
	}


	FilePropertyDescriptor(const char* name, ArrayGetter getter, ArraySetter setter, const char* file_type)
	{
		setName(name);
		m_array.getter = getter;
		m_array.setter = setter;
		m_type = IPropertyDescriptor::FILE;
		copyString(m_file_type, file_type);
	}



	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		char tmp[MAX_PATH_LENGTH];
		char* c = tmp;
		do
		{
			stream.read(c, 1);
			++c;
		} while(*(c - 1) && (c - 1) - tmp < lengthOf(tmp));

		if(index < 0)
		{
			(static_cast<S*>(cmp.scene)->*m_single.setter)(cmp.handle, Path(tmp));
		}
		else
		{
			(static_cast<S*>(cmp.scene)->*m_array.setter)(cmp.handle, index, Path(tmp));
		}
	}


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		Path value;
		if(index < 0)
		{
			value = (static_cast<S*>(cmp.scene)->*m_single.getter)(cmp.handle);
		}
		else
		{
			value = (static_cast<S*>(cmp.scene)->*m_array.getter)(cmp.handle, index);
		}
		int len = value.length() + 1;
		stream.write(value.c_str(), len);
	}



private:
	union
	{
		struct
		{
			Getter getter;
			Setter setter;
		} m_single;
		struct
		{
			ArrayGetter getter;
			ArraySetter setter;
		} m_array;
	};
	char m_file_type[MAX_PATH_LENGTH];
};


template <class T>
class ResourcePropertyDescriptor : public IResourcePropertyDescriptor
{
public:
	using Getter = typename FilePropertyDescriptor<T>::Getter;
	using Setter = typename FilePropertyDescriptor<T>::Setter;
	using ArrayGetter = typename FilePropertyDescriptor<T>::ArrayGetter;
	using ArraySetter = typename FilePropertyDescriptor<T>::ArraySetter;

	ResourcePropertyDescriptor(const char* name,
		Getter getter,
		Setter setter,
		const char* file_type,
		ResourceType resource_type)
		: m_file_descriptor(name, getter, setter, file_type)
		, m_resource_type(resource_type)
	{
		setName(name);
	}

	ResourcePropertyDescriptor(const char* name,
		ArrayGetter getter,
		ArraySetter setter,
		const char* file_type,
		ResourceType resource_type)
		: m_file_descriptor(name, getter, setter, file_type)
		, m_resource_type(resource_type)
	{
		setName(name);
	}

	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		m_file_descriptor.set(cmp, index, stream);
	}

	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		m_file_descriptor.get(cmp, index, stream);
	}

	ResourceType getResourceType() override { return m_resource_type; }

	ResourceType m_resource_type;
	FilePropertyDescriptor<T> m_file_descriptor;
};


template <class S> class SampledFunctionDescriptor : public ISampledFunctionDescriptor
{
public:
	typedef const Lumix::Vec2* (S::*Getter)(ComponentHandle);
	typedef int (S::*CountGetter)(ComponentHandle);
	typedef void (S::*Setter)(ComponentHandle, const Lumix::Vec2*, int);

public:
	SampledFunctionDescriptor(const char* name,
		Getter getter,
		Setter setter,
		CountGetter count_getter,
		float max_x,
		float max_y)
	{
		setName(name);
		m_getter = getter;
		m_setter = setter;
		m_count_getter = count_getter;
		m_max_x = max_x;
		m_max_y = max_y;
		m_type = SAMPLED_FUNCTION;
	}


	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		ASSERT(index == -1);
		int count;
		stream.read(count);
		auto* buf = (const Lumix::Vec2*)stream.skip(sizeof(Lumix::Vec2) * count);
		(static_cast<S*>(cmp.scene)->*m_setter)(cmp.handle, buf, count);
	};


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		ASSERT(index == -1);
		int count = (static_cast<S*>(cmp.scene)->*m_count_getter)(cmp.handle);
		stream.write(count);
		const Lumix::Vec2* values = (static_cast<S*>(cmp.scene)->*m_getter)(cmp.handle);
		stream.write(values, sizeof(values[0]) * count);
	};


	float getMaxX() override { return m_max_x; }
	float getMaxY() override { return m_max_y; }

private:
	Getter m_getter;
	Setter m_setter;
	CountGetter m_count_getter;
	float m_max_x;
	float m_max_y;
};



template <class S> class EntityPropertyDescriptor : public IPropertyDescriptor
{
public:
	typedef Entity (S::*Getter)(ComponentHandle);
	typedef void (S::*Setter)(ComponentHandle, Entity);
	typedef Entity (S::*ArrayGetter)(ComponentHandle, int);
	typedef void (S::*ArraySetter)(ComponentHandle, int, Entity);

public:
	EntityPropertyDescriptor(const char* name, Getter _getter, Setter _setter)
	{
		setName(name);
		m_single.getter = _getter;
		m_single.setter = _setter;
		m_type = ENTITY;
	}


	EntityPropertyDescriptor(const char* name, ArrayGetter _getter, ArraySetter _setter)
	{
		setName(name);
		m_array.getter = _getter;
		m_array.setter = _setter;
		m_type = ENTITY;
	}


	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		int value;
		stream.read(&value, sizeof(value));
		auto entity = value < 0 ? INVALID_ENTITY : cmp.scene->getUniverse().getEntityFromDenseIdx(value);
		if (index == -1)
		{
			(static_cast<S*>(cmp.scene)->*m_single.setter)(cmp.handle, entity);
		}
		else
		{
			(static_cast<S*>(cmp.scene)->*m_array.setter)(cmp.handle, index, entity);
		}
	};


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		Entity value;
		if (index == -1)
		{
			value = (static_cast<S*>(cmp.scene)->*m_single.getter)(cmp.handle);
		}
		else
		{
			value = (static_cast<S*>(cmp.scene)->*m_array.getter)(cmp.handle, index);
		}
		auto dense_idx = cmp.scene->getUniverse().getDenseIdx(value);
		int len = sizeof(dense_idx);
		stream.write(&dense_idx, len);
	};


private:
	union {
		struct
		{
			Getter getter;
			Setter setter;
		} m_single;
		struct
		{
			ArrayGetter getter;
			ArraySetter setter;
		} m_array;
	};
};


template <typename S>
class BlobPropertyDescriptor : public IPropertyDescriptor
{
public:
	typedef void (S::*Getter)(ComponentHandle, OutputBlob&);
	typedef void (S::*Setter)(ComponentHandle, InputBlob&);

public:
	BlobPropertyDescriptor(const char* name, Getter _getter, Setter _setter)
	{
		m_getter = _getter;
		m_setter = _setter;
		setName(name);
		IPropertyDescriptor::m_type = IPropertyDescriptor::BLOB;
	}


	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		ASSERT(index < 0);
		(static_cast<S*>(cmp.scene)->*m_setter)(cmp.handle, stream);
	}


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		ASSERT(index < 0);
		(static_cast<S*>(cmp.scene)->*m_getter)(cmp.handle, stream);
	}

	Getter m_getter;
	Setter m_setter;
};


template <class S> class DecimalPropertyDescriptor : public IDecimalPropertyDescriptor
{
public:
	typedef float (S::*Getter)(ComponentHandle);
	typedef void (S::*Setter)(ComponentHandle, float);
	typedef float (S::*ArrayGetter)(ComponentHandle, int);
	typedef void (S::*ArraySetter)(ComponentHandle, int,float);

public:
	DecimalPropertyDescriptor(const char* name,
		Getter _getter,
		Setter _setter,
		float min,
		float max,
		float step)
	{
		setName(name);
		m_getter = _getter;
		m_setter = _setter;
		m_array_getter = nullptr;
		m_array_setter = nullptr;
		m_min = min;
		m_max = max;
		m_step = step;
		m_type = DECIMAL;
	}


	DecimalPropertyDescriptor(const char* name,
		ArrayGetter _getter,
		ArraySetter _setter,
		float min,
		float max,
		float step)
	{
		setName(name);
		m_array_getter = _getter;
		m_array_setter = _setter;
		m_getter = nullptr;
		m_setter = nullptr;
		m_min = min;
		m_max = max;
		m_step = step;
		m_type = DECIMAL;
	}



	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		float f;
		stream.read(&f, sizeof(f));
		if(index >= 0)
		{
			(static_cast<S*>(cmp.scene)->*m_array_setter)(cmp.handle, index, f);
		}
		else
		{
			(static_cast<S*>(cmp.scene)->*m_setter)(cmp.handle, f);
		}
	};


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		float f = 0;
		if(index >= 0)
		{
			f = (static_cast<S*>(cmp.scene)->*m_array_getter)(cmp.handle, index);
		}
		else
		{
			f = (static_cast<S*>(cmp.scene)->*m_getter)(cmp.handle);
		}
		int len = sizeof(f);
		stream.write(&f, len);
	};

private:
	Getter m_getter;
	Setter m_setter;
	ArrayGetter m_array_getter;
	ArraySetter m_array_setter;
};


template <class S> class ColorPropertyDescriptor : public SimplePropertyDescriptor<Vec3, S>
{
public:
	using Getter = typename SimplePropertyDescriptor<Vec3, S>::Getter;
	using Setter = typename SimplePropertyDescriptor<Vec3, S>::Setter;
	using ArrayGetter = typename SimplePropertyDescriptor<Vec3, S>::ArrayGetter;
	using ArraySetter = typename SimplePropertyDescriptor<Vec3, S>::ArraySetter;

	ColorPropertyDescriptor(const char* name, Getter _getter, Setter _setter)
		: SimplePropertyDescriptor<Vec3, S>(name, _getter, _setter)
	{
		IPropertyDescriptor::m_type = IPropertyDescriptor::COLOR;
	}
};


template <class S> class EnumPropertyDescriptor : public IEnumPropertyDescriptor
{
public:
	typedef int (S::*Getter)(ComponentHandle);
	typedef void (S::*Setter)(ComponentHandle, int);
	typedef int (S::*EnumCountGetter)() const;
	typedef const char* (S::*EnumNameGetter)(int);

public:
	EnumPropertyDescriptor(const char* name,
		Getter _getter,
		Setter _setter,
		EnumCountGetter count_getter,
		EnumNameGetter enum_name_getter)
	{
		setName(name);
		m_getter = _getter;
		m_setter = _setter;
		m_enum_count_getter = count_getter;
		m_enum_name_getter = enum_name_getter;
		m_type = ENUM;
	}


	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		ASSERT(index == -1);
		int value;
		stream.read(&value, sizeof(value));
		(static_cast<S*>(cmp.scene)->*m_setter)(cmp.handle, value);
	};


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		ASSERT(index == -1);
		int value = (static_cast<S*>(cmp.scene)->*m_getter)(cmp.handle);
		int len = sizeof(value);
		stream.write(&value, len);
	};


	int getEnumCount(IScene* scene, ComponentHandle) override { return (static_cast<S*>(scene)->*m_enum_count_getter)(); }


	const char* getEnumItemName(IScene* scene, ComponentHandle, int index) override
	{
		return (static_cast<S*>(scene)->*m_enum_name_getter)(index);
	}

private:
	Getter m_getter;
	Setter m_setter;
	EnumCountGetter m_enum_count_getter;
	EnumNameGetter m_enum_name_getter;

};


} // !namespace Lumix
