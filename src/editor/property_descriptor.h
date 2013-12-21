#pragma once

#include "universe\universe.h"
#include "core/string.h"


namespace Lux
{


struct Vec3;
class Blob;


class PropertyDescriptor
{
	public:
		enum Type
		{
			FILE = 0,
			DECIMAL,
			BOOL,
			VEC3
		};
		struct S {};
		typedef void (S::*Getter)(Component, string&);
		typedef void (S::*Setter)(Component, const string&);
		typedef void (S::*BoolGetter)(Component, bool&);
		typedef void (S::*BoolSetter)(Component, const bool&);
		typedef void (S::*DecimalGetter)(Component, float&);
		typedef void (S::*DecimalSetter)(Component, const float&);
		typedef void (S::*Vec3Getter)(Component, Vec3&);
		typedef void (S::*Vec3Setter)(Component, const Vec3&);

	public:
		PropertyDescriptor(uint32_t _name_hash, Getter _getter, Setter _setter, Type _type) { m_name_hash = _name_hash; m_getter = _getter; m_setter = _setter; m_type = _type; }
		PropertyDescriptor(uint32_t _name_hash, BoolGetter _getter, BoolSetter _setter) { m_name_hash = _name_hash; m_bool_getter = _getter; m_bool_setter = _setter; m_type = BOOL; }
		PropertyDescriptor(uint32_t _name_hash, DecimalGetter _getter, DecimalSetter _setter) { m_name_hash = _name_hash; m_decimal_getter = _getter; m_decimal_setter = _setter; m_type = DECIMAL; }
		PropertyDescriptor(uint32_t _name_hash, Vec3Getter _getter, Vec3Setter _setter) { m_name_hash = _name_hash; m_vec3_getter = _getter; m_vec3_setter = _setter; m_type = VEC3; }
		void set(Component cmp, Blob& stream) const;
		void get(Component cmp, Blob& stream) const;
		uint32_t getNameHash() const { return m_name_hash; }
		Type getType() const { return m_type; }

	private:
		uint32_t m_name_hash;
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
		Type m_type;

};


} // !namespace Lux
