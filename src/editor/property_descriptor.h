#pragma once

#include "universe\universe.h"
#include "core/string.h"


namespace Lux
{


class PropertyDescriptor
{
	public:
		enum Type
		{
			FILE = 0,
			DECIMAL,
			BOOL
		};
		struct S {};
		typedef void (S::*Getter)(Component, string&);
		typedef void (S::*Setter)(Component, const string&);
		typedef void (S::*BoolGetter)(Component, bool&);
		typedef void (S::*BoolSetter)(Component, const bool&);
		typedef void (S::*DecimalGetter)(Component, float&);
		typedef void (S::*DecimalSetter)(Component, const float&);

	public:
		PropertyDescriptor(const char* _name, Getter _getter, Setter _setter, Type _type) { m_name = _name; m_getter = _getter; m_setter = _setter; m_type = _type; }
		PropertyDescriptor(const char* _name, BoolGetter _getter, BoolSetter _setter) { m_name = _name; m_bool_getter = _getter; m_bool_setter = _setter; m_type = BOOL; }
		PropertyDescriptor(const char* _name, DecimalGetter _getter, DecimalSetter _setter) { m_name = _name; m_decimal_getter = _getter; m_decimal_setter = _setter; m_type = DECIMAL; }
		void set(Component cmp, const string& value) const;
		void get(Component cmp, string& value) const;
		const string& getName() const { return m_name; }
		Type getType() const { return m_type; }

	private:
		string m_name;
		union
		{
			Getter m_getter;
			BoolGetter m_bool_getter;
			DecimalGetter m_decimal_getter;
		};
		union 
		{
			Setter m_setter;
			BoolSetter m_bool_setter;
			DecimalSetter m_decimal_setter;
		};
		Type m_type;

};


} // !namespace Lux
