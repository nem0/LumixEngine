#pragma once

#include "universe\universe.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/delegate.h"
#include "core/string.h"


namespace Lumix
{


struct Vec3;
class Blob;


class IPropertyDescriptor
{
	public:
		enum Type
		{
			FILE = 0,
			DECIMAL,
			BOOL,
			VEC3,
			INTEGER,
			STRING,
			ARRAY
		};

	public:
		virtual ~IPropertyDescriptor() {}

		virtual void set(Component cmp, Blob& stream) const = 0;
		virtual void get(Component cmp, Blob& stream) const = 0;
		virtual void set(Component cmp, int index, Blob& stream) const = 0;
		virtual void get(Component cmp, int index, Blob& stream) const = 0;

		uint32_t getNameHash() const { return m_name_hash; }
		Type getType() const { return m_type; }
		const char* getName() const { return m_name.c_str(); }
		void setName(const char* name) { m_name = name; m_name_hash = crc32(name); }
		const char* getFileType() const { return m_file_type.c_str(); }
		void addChild(IPropertyDescriptor* child) { m_children.push(child); }
		const Array<IPropertyDescriptor*>& getChildren() const { return m_children; }
		Array<IPropertyDescriptor*>& getChildren() { return m_children; }

	protected:
		uint32_t m_name_hash;
		string m_name;
		Type m_type;
		string m_file_type;
		Array<IPropertyDescriptor*> m_children;
};


class IIntPropertyDescriptor : public IPropertyDescriptor
{
	public:
		IIntPropertyDescriptor()
		{
			m_min = INT_MIN;
			m_max = INT_MAX;
		}

		void setLimit(int min, int max) { m_min = min; m_max = max; }
		int getMin() const { return m_min; }
		int getMax() const { return m_max; }

	private:
		int m_min;
		int m_max;
};


template <class S>
class IntArrayObjectDescriptor : public IIntPropertyDescriptor
{
	public:
		typedef void (S::*IntegerGetter)(Component, int, int&);
		typedef void (S::*IntegerSetter)(Component, int, const int&);

	public:
		IntArrayObjectDescriptor(const char* name, IntegerGetter _getter, IntegerSetter _setter) 
		{
			setName(name);
			m_integer_getter = _getter;
			m_integer_setter = _setter;
			m_type = INTEGER;
		}

		virtual void set(Component cmp, int index, Blob& stream) const override;
		virtual void get(Component cmp, int index, Blob& stream) const override;
		virtual void set(Component, Blob&) const {};
		virtual void get(Component, Blob&) const {};

	private:
		IntegerGetter m_integer_getter;
		IntegerSetter m_integer_setter;
};


template <class S>
class ArrayObjectDescriptor : public IPropertyDescriptor
{
	public:
		typedef void (S::*Getter)(Component, int, string&);
		typedef void (S::*Setter)(Component, int, const string&);
		typedef void (S::*BoolGetter)(Component, int, bool&);
		typedef void (S::*BoolSetter)(Component, int, const bool&);
		typedef void (S::*DecimalGetter)(Component, int, float&);
		typedef void (S::*DecimalSetter)(Component, int, const float&);
		typedef void (S::*Vec3Getter)(Component, int, Vec3&);
		typedef void (S::*Vec3Setter)(Component, int, const Vec3&);

	public:
		ArrayObjectDescriptor(const char* name, Getter _getter, Setter _setter, Type _type, const char* file_type) { setName(name); m_getter = _getter; m_setter = _setter; m_type = _type; m_file_type = file_type; }
		ArrayObjectDescriptor(const char* name, BoolGetter _getter, BoolSetter _setter) { setName(name); m_bool_getter = _getter; m_bool_setter = _setter; m_type = BOOL; }
		ArrayObjectDescriptor(const char* name, DecimalGetter _getter, DecimalSetter _setter) { setName(name); m_decimal_getter = _getter; m_decimal_setter = _setter; m_type = DECIMAL; }
		ArrayObjectDescriptor(const char* name, Vec3Getter _getter, Vec3Setter _setter) { setName(name); m_vec3_getter = _getter; m_vec3_setter = _setter; m_type = VEC3; }
		virtual void set(Component cmp, int index, Blob& stream) const override;
		virtual void get(Component cmp, int index, Blob& stream) const override;
		virtual void set(Component, Blob&) const {};
		virtual void get(Component, Blob&) const {};

	private:
		union
		{
			Getter m_getter;
			BoolGetter m_bool_getter;
			DecimalGetter m_decimal_getter;
			Vec3Getter m_vec3_getter;
		};
		union 
		{
			Setter m_setter;
			BoolSetter m_bool_setter;
			DecimalSetter m_decimal_setter;
			Vec3Setter m_vec3_setter;
		};
};


class IArrayDescriptor : public IPropertyDescriptor
{
	public:
		virtual void removeArrayItem(Component cmp, int index) const = 0;
		virtual void addArrayItem(Component cmp, int index) const = 0;
		virtual int getCount(Component cmp) const = 0;
};


template <class S>
class ArrayDescriptor : public IArrayDescriptor
{
	public:
		typedef int (S::*Counter)(Component);
		typedef void (S::*Adder)(Component, int);
		typedef void (S::*Remover)(Component, int);

	public:
		ArrayDescriptor(const char* name, Counter counter, Adder adder, Remover remover) { setName(name); m_type = ARRAY; m_counter = counter; m_adder = adder; m_remover = remover; }
		~ArrayDescriptor()
		{
			for(int i = 0; i < m_children.size(); ++i)
			{
				LUMIX_DELETE(m_children[i]);
			}
		}

		virtual void set(Component, Blob&) const override;
		virtual void get(Component, Blob&) const override;
		virtual void set(Component, int, Blob&) const override { ASSERT(false); };
		virtual void get(Component, int, Blob&) const override { ASSERT(false); };

		virtual int getCount(Component cmp) const override { return (static_cast<S*>(cmp.scene)->*m_counter)(cmp); }
		virtual void addArrayItem(Component cmp, int index) const override { (static_cast<S*>(cmp.scene)->*m_adder)(cmp, index); }
		virtual void removeArrayItem(Component cmp, int index) const override { (static_cast<S*>(cmp.scene)->*m_remover)(cmp, index); }

	private:
		Counter m_counter;
		Adder m_adder;
		Remover m_remover;
};

template <class S>
class IntPropertyDescriptor : public IIntPropertyDescriptor
{
	public:
		typedef void (S::*IntegerGetter)(Component, int&);
		typedef void (S::*IntegerSetter)(Component, const int&);

	public:
		IntPropertyDescriptor(const char* name, IntegerGetter _getter, IntegerSetter _setter) { setName(name); m_integer_getter = _getter; m_integer_setter = _setter; m_type = INTEGER; }

		virtual void set(Component cmp, Blob& stream) const override;
		virtual void get(Component cmp, Blob& stream) const override;
		virtual void set(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); set(cmp, stream); };
		virtual void get(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); get(cmp, stream); };

	private:
		IntegerGetter m_integer_getter;
		IntegerSetter m_integer_setter;
};


template <class S>
class PropertyDescriptor : public IPropertyDescriptor
{
	public:
		typedef void (S::*Getter)(Component, string&);
		typedef void (S::*Setter)(Component, const string&);
		typedef void (S::*BoolGetter)(Component, bool&);
		typedef void (S::*BoolSetter)(Component, const bool&);
		typedef void (S::*DecimalGetter)(Component, float&);
		typedef void (S::*DecimalSetter)(Component, const float&);
		typedef void (S::*Vec3Getter)(Component, Vec3&);
		typedef void (S::*Vec3Setter)(Component, const Vec3&);

	public:
		PropertyDescriptor(const char* name, Getter _getter, Setter _setter, Type _type, const char* file_type) { setName(name); m_getter = _getter; m_setter = _setter; m_type = _type; m_file_type = file_type; }
		PropertyDescriptor(const char* name, BoolGetter _getter, BoolSetter _setter) { setName(name); m_bool_getter = _getter; m_bool_setter = _setter; m_type = BOOL; }
		PropertyDescriptor(const char* name, DecimalGetter _getter, DecimalSetter _setter) { setName(name); m_decimal_getter = _getter; m_decimal_setter = _setter; m_type = DECIMAL; }
		PropertyDescriptor(const char* name, Vec3Getter _getter, Vec3Setter _setter) { setName(name); m_vec3_getter = _getter; m_vec3_setter = _setter; m_type = VEC3; }
		virtual void set(Component cmp, Blob& stream) const override;
		virtual void get(Component cmp, Blob& stream) const override;
		virtual void set(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); set(cmp, stream); };
		virtual void get(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); get(cmp, stream);};

	private:
		union
		{
			Getter m_getter;
			BoolGetter m_bool_getter;
			DecimalGetter m_decimal_getter;
			Vec3Getter m_vec3_getter;
		};
		union 
		{
			Setter m_setter;
			BoolSetter m_bool_setter;
			DecimalSetter m_decimal_setter;
			Vec3Setter m_vec3_setter;
		};

};


template <class S>
void ArrayDescriptor<S>::set(Component cmp, Blob& stream) const
{
	int count;
	stream.read(count);
	while(getCount(cmp) < count)
	{
		addArrayItem(cmp, -1);
	}
	while(getCount(cmp) > count)
	{
		removeArrayItem(cmp, getCount(cmp) - 1);
	}
	for(int i = 0; i < count; ++i)
	{
		for(int j = 0, cj = getChildren().size(); j < cj; ++j)
		{
			getChildren()[j]->set(cmp, i, stream);
		}
	}
}


template <class S>
void ArrayDescriptor<S>::get(Component cmp, Blob& stream) const
{
	int count = getCount(cmp);
	stream.write(count);
	for(int i = 0; i < count; ++i)
	{
		for(int j = 0, cj = getChildren().size(); j < cj; ++j)
		{
			getChildren()[j]->get(cmp, i, stream);
		}
	}
}


template <class S>
void IntArrayObjectDescriptor<S>::set(Component cmp, int index, Blob& stream) const
{
	int32_t i;
	stream.read(&i, sizeof(i));
	(static_cast<S*>(cmp.scene)->*m_integer_setter)(cmp, index, i);
}


template <class S>
void ArrayObjectDescriptor<S>::set(Component cmp, int index, Blob& stream) const
{
	switch(m_type)
	{
		case DECIMAL:
			{
				float f;
				stream.read(&f, sizeof(f));
				(static_cast<S*>(cmp.scene)->*m_decimal_setter)(cmp, index, f); 
			}
			break;
		case BOOL:
			{
				bool b;
				stream.read(&b, sizeof(b));
				(static_cast<S*>(cmp.scene)->*m_bool_setter)(cmp, index, b);
			}
			break;
		case STRING:
		case FILE:
			{
				char tmp[300];
				char* c = tmp;
				do
				{
					stream.read(c, 1);
					++c;
				}
				while (*(c - 1) && (c - 1) - tmp < 300);
				string s((char*)tmp);
				(static_cast<S*>(cmp.scene)->*m_setter)(cmp, index, s);
			}
			break;
		case VEC3:
			{
				Vec3 v;
				stream.read(&v, sizeof(v));
				(static_cast<S*>(cmp.scene)->*m_vec3_setter)(cmp, index, v);
			}
			break;
		default:
			ASSERT(false);
			break;
	}
}


template <class S>
void IntArrayObjectDescriptor<S>::get(Component cmp, int index, Blob& stream) const
{
	int32_t i;
	(static_cast<S*>(cmp.scene)->*m_integer_getter)(cmp, index, i);
	int len = sizeof(i);
	stream.write(&i, len);
}


template <class S>
void ArrayObjectDescriptor<S>::get(Component cmp, int index, Blob& stream) const
{
	int len = 4;
	switch(m_type)
	{
		case STRING:
		case FILE:
			{
				string value;
				(static_cast<S*>(cmp.scene)->*m_getter)(cmp, index, value);
				len = value.length() + 1;
				stream.write(value.c_str(), len);
			}
			break;
		case DECIMAL:
			{
				float f;
				(static_cast<S*>(cmp.scene)->*m_decimal_getter)(cmp, index, f);
				len = sizeof(f);
				stream.write(&f, len);
			}
			break;
		case BOOL:
			{
				bool b;
				(static_cast<S*>(cmp.scene)->*m_bool_getter)(cmp, index, b);
				len = sizeof(b);
				stream.write(&b, len);
			}
			break;
		case VEC3:
			{
				Vec3 v;
				(static_cast<S*>(cmp.scene)->*m_vec3_getter)(cmp, index, v);
				len = sizeof(v);
				stream.write(&v, len);
			}
			break;
		default:
			ASSERT(false);
			break;
	}
}


template <class S>
void IntPropertyDescriptor<S>::set(Component cmp, Blob& stream) const
{
	int32_t i;
	stream.read(&i, sizeof(i));
	(static_cast<S*>(cmp.scene)->*m_integer_setter)(cmp, i);
}


template <class S>
void IntPropertyDescriptor<S>::get(Component cmp, Blob& stream) const
{
	int32_t i;
	(static_cast<S*>(cmp.scene)->*m_integer_getter)(cmp, i);
	len = sizeof(i);
	stream.write(&i, len);
}


template <class S>
void PropertyDescriptor<S>::set(Component cmp, Blob& stream) const
{
	switch(m_type)
	{
		case DECIMAL:
			{
				float f;
				stream.read(&f, sizeof(f));
				(static_cast<S*>(cmp.scene)->*m_decimal_setter)(cmp, f); 
			}
			break;
		case BOOL:
			{
				bool b;
				stream.read(&b, sizeof(b));
				(static_cast<S*>(cmp.scene)->*m_bool_setter)(cmp, b);
			}
			break;
		case STRING:
		case FILE:
			{
				char tmp[300];
				char* c = tmp;
				do
				{
					stream.read(c, 1);
					++c;
				}
				while (*(c - 1) && (c - 1) - tmp < 300);
				string s((char*)tmp);
				(static_cast<S*>(cmp.scene)->*m_setter)(cmp, s);
			}
			break;
		case VEC3:
			{
				Vec3 v;
				stream.read(&v, sizeof(v));
				(static_cast<S*>(cmp.scene)->*m_vec3_setter)(cmp, v);
			}
			break;
		default:
			ASSERT(false);
			break;
	}
}


template <class S>
void PropertyDescriptor<S>::get(Component cmp, Blob& stream) const
{
	int len = 4;
	switch(m_type)
	{
		case STRING:
		case FILE:
			{
				string value;
				(static_cast<S*>(cmp.scene)->*m_getter)(cmp, value);
				len = value.length() + 1;
				stream.write(value.c_str(), len);
			}
			break;
		case DECIMAL:
			{
				float f;
				(static_cast<S*>(cmp.scene)->*m_decimal_getter)(cmp, f);
				len = sizeof(f);
				stream.write(&f, len);
			}
			break;
		case BOOL:
			{
				bool b;
				(static_cast<S*>(cmp.scene)->*m_bool_getter)(cmp, b);
				len = sizeof(b);
				stream.write(&b, len);
			}
			break;
		case VEC3:
			{
				Vec3 v;
				(static_cast<S*>(cmp.scene)->*m_vec3_getter)(cmp, v);
				len = sizeof(v);
				stream.write(&v, len);
			}
			break;
		default:
			ASSERT(false);
			break;
	}
}


} // !namespace Lumix
