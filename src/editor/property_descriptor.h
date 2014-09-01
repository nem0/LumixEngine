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
			STRING
		};

	public:
		virtual ~IPropertyDescriptor() {}

		virtual void set(Component cmp, Blob& stream) const = 0;
		virtual void get(Component cmp, Blob& stream) const = 0;

		uint32_t getNameHash() const { return m_name_hash; }
		Type getType() const { return m_type; }
		const char* getName() const { return m_name.c_str(); }
		void setName(const char* name) { m_name = name; m_name_hash = crc32(name); }
		const char* getFileType() const { return m_file_type.c_str(); }

	protected:
		uint32_t m_name_hash;
		string m_name;
		Type m_type;
		string m_file_type;
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
		typedef void (S::*IntegerGetter)(Component, int&);
		typedef void (S::*IntegerSetter)(Component, const int&);
		typedef void (S::*Vec3Getter)(Component, Vec3&);
		typedef void (S::*Vec3Setter)(Component, const Vec3&);

	public:
		PropertyDescriptor(const char* name, Getter _getter, Setter _setter, Type _type, const char* file_type) { setName(name); m_getter = _getter; m_setter = _setter; m_type = _type; m_file_type = file_type; }
		PropertyDescriptor(const char* name, BoolGetter _getter, BoolSetter _setter) { setName(name); m_bool_getter = _getter; m_bool_setter = _setter; m_type = BOOL; }
		PropertyDescriptor(const char* name, DecimalGetter _getter, DecimalSetter _setter) { setName(name); m_decimal_getter = _getter; m_decimal_setter = _setter; m_type = DECIMAL; }
		PropertyDescriptor(const char* name, IntegerGetter _getter, IntegerSetter _setter) { setName(name); m_integer_getter = _getter; m_integer_setter = _setter; m_type = INTEGER; }
		PropertyDescriptor(const char* name, Vec3Getter _getter, Vec3Setter _setter) { setName(name); m_vec3_getter = _getter; m_vec3_setter = _setter; m_type = VEC3; }
		virtual void set(Component cmp, Blob& stream) const override;
		virtual void get(Component cmp, Blob& stream) const override;

	private:
		union
		{
			Getter m_getter;
			BoolGetter m_bool_getter;
			DecimalGetter m_decimal_getter;
			IntegerGetter m_integer_getter;
			Vec3Getter m_vec3_getter;
		};
		union 
		{
			Setter m_setter;
			BoolSetter m_bool_setter;
			DecimalSetter m_decimal_setter;
			IntegerSetter m_integer_setter;
			Vec3Setter m_vec3_setter;
		};

};



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
		case INTEGER:
			{
				int32_t i;
				stream.read(&i, sizeof(i));
				(static_cast<S*>(cmp.scene)->*m_integer_setter)(cmp, i);
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
		case INTEGER:
			{
				int32_t i;
				(static_cast<S*>(cmp.scene)->*m_integer_getter)(cmp, i);
				len = sizeof(i);
				stream.write(&i, len);
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
