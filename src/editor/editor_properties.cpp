#include "editor_properties.h"


namespace Lux
{


map<Component::Type, vector<EditorProperty> > g_properties;


Gwen::Controls::Property::Base* createGwenProperty(EditorProperty& prop, Component cmp, Gwen::Controls::Base* parent)
{
	Gwen::Controls::Property::Base* ret = 0;
	switch(prop.type)
	{
		default:
		case EditorProperty::TEXT:
			ret = new CustomGwenTextProperty(cmp, parent);
			break;
		case EditorProperty::BOOLEAN:
			ret = new CustomGwenBooleanProperty(cmp, parent);
			break;
		case EditorProperty::FILE:
			ret = new CustomGwenFileProperty(cmp, parent);
			break;
	}

	ret->UserData.Set("component", cmp);
	ret->UserData.Set("property", &prop);
	return ret;
}


CustomGwenFileProperty::CustomGwenFileProperty(Component _cmp, Gwen::Controls::Base* parent)
	: Gwen::Controls::Property::File(parent)
{
	cmp = _cmp;
	onChange.Add(this, &CustomGwenFileProperty::onPropertyChange);
}


void CustomGwenFileProperty::onPropertyChange(Gwen::Controls::Base* ctrl)
{
	Component cmp = ctrl->UserData.Get<Component>("component");
	EditorProperty* prop = ctrl->UserData.Get<EditorProperty*>("property");
	(static_cast<EditorProperty::S*>(cmp.system)->*prop->setter)(cmp, m_TextBox->GetValue().c_str());
}


CustomGwenTextProperty::CustomGwenTextProperty(Component _cmp, Gwen::Controls::Base* parent)
	: Gwen::Controls::Property::Text(parent)
{
	cmp = _cmp;
	onChange.Add(this, &CustomGwenTextProperty::onPropertyChange);
}


void CustomGwenTextProperty::onPropertyChange(Gwen::Controls::Base* ctrl)
{
	Component cmp = ctrl->UserData.Get<Component>("component");
	EditorProperty* prop = ctrl->UserData.Get<EditorProperty*>("property");
	(static_cast<EditorProperty::S*>(cmp.system)->*prop->setter)(cmp, m_TextBox->GetValue().c_str());
}



CustomGwenBooleanProperty::CustomGwenBooleanProperty(Component _cmp, Gwen::Controls::Base* parent)
	: Gwen::Controls::Property::Checkbox(parent)
{
	cmp = _cmp;
	onChange.Add(this, &CustomGwenBooleanProperty::onPropertyChange);
}


void CustomGwenBooleanProperty::onPropertyChange(Gwen::Controls::Base* ctrl)
{
	Component cmp = ctrl->UserData.Get<Component>("component");
	EditorProperty* prop = ctrl->UserData.Get<EditorProperty*>("property");
	(static_cast<EditorProperty::S*>(cmp.system)->*prop->bool_setter)(cmp, m_Checkbox->IsChecked());
}


} // !namespace Lux