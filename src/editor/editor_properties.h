#pragma once


#include "core/string.h"
#include "universe/universe.h"
#include "Gwen/Controls.h"



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



struct CustomGwenBooleanProperty : public Gwen::Controls::Property::Checkbox
{
	CustomGwenBooleanProperty(Component _cmp, Gwen::Controls::Base* parent);
	void onPropertyChange(Gwen::Controls::Base* ctrl);

	Component cmp;
};


struct CustomGwenTextProperty : public Gwen::Controls::Property::Text
{
	CustomGwenTextProperty(Component _cmp, Gwen::Controls::Base* parent);
	void onPropertyChange(Gwen::Controls::Base* ctrl);

	Component cmp;
};


struct CustomGwenFileProperty : public Gwen::Controls::Property::File
{
	CustomGwenFileProperty(Component _cmp, Gwen::Controls::Base* parent);
	void onPropertyChange(Gwen::Controls::Base* ctrl);

	Component cmp;
};


Gwen::Controls::Property::Base* createGwenProperty(EditorProperty& prop, Component cmp, Gwen::Controls::Base* parent);


extern map<Component::Type, vector<EditorProperty> > g_properties;


} // !namespace Lux