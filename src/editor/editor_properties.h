#pragma once


#include "core/string.h"
#include "universe/universe.h"



namespace Lux
{


struct EditorProperty
{
	enum Type
	{
		TEXT,
		FILE,
		BOOLEAN
	};

	//typedef void (*Setter)(Component, const string&);
	//typedef void (*Getter)(Component, string&);
	string name;
	struct S {};
	typedef void (S::*BoolSetter)(Component, const bool&);
	typedef void (S::*BoolGetter)(Component, bool&);
	typedef void (S::*Setter)(Component, const string&);
	typedef void (S::*Getter)(Component, string&);
	union
	{
		Setter setter;
		BoolSetter bool_setter;
	};

	union
	{
		Getter getter;
		BoolGetter bool_getter;
	};
	Type type;

	EditorProperty(string _name, BoolGetter _getter, BoolSetter _setter, Type _type)
		: name(_name)
		, bool_getter(_getter)
		, bool_setter(_setter)
		, type(_type)
	{
	}

	EditorProperty(string _name, Getter _getter, Setter _setter, Type _type)
		: name(_name)
		, getter(_getter)
		, setter(_setter)
		, type(_type)
	{
	}
};


extern map<Component::Type, vector<EditorProperty> > g_properties;


} // !namespace Lux