#include "set_property_visitor.h"


namespace Lumix
{

	SetPropertyVisitor::SetPropertyVisitor(const char* name, void* value, int value_size)
	{
		m_property_name = name;
		m_value_size = value_size;
		m_value = LUMIX_NEW_ARRAY(char, m_value_size);
		memcpy(m_value, value, m_value_size);
	}

	SetPropertyVisitor::~SetPropertyVisitor()
	{
		LUX_DELETE_ARRAY(m_value);
	}

	void SetPropertyVisitor::visit(const char* name, float& value)
	{
		if(m_property_name == name && m_value_size == sizeof(value))
		{
			memcpy(&value, m_value, sizeof(value));
		}
	}



} // ~namespace Lumix
