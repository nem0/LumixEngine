#include "get_property_visitor.h"


namespace Lumix
{

	GetPropertyVisitor::GetPropertyVisitor(const char* name)
	{
		m_property_name = name;
		m_value = 0;
		m_value_size = 0;
	}

	GetPropertyVisitor::~GetPropertyVisitor()
	{
		LUMIX_DELETE_ARRAY(m_value);
	}

	void GetPropertyVisitor::visit(const char* name, float& value)
	{
		if(m_property_name == name && m_value == 0)
		{
			m_type = FLOAT;
			m_value = LUMIX_NEW_ARRAY(char, sizeof(float));
			memcpy(m_value, &value, sizeof(value));
			m_value_size = sizeof(value);
		}
	}



} // ~namespace Lumix
