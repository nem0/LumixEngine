#include "get_property_visitor.h"


namespace Lux
{

	GetPropertyVisitor::GetPropertyVisitor(const char* name)
	{
		m_property_name = name;
		m_value = 0;
		m_value_size = 0;
	}

	GetPropertyVisitor::~GetPropertyVisitor()
	{
		delete[] m_value;
	}

	void GetPropertyVisitor::visit(const char* name, float& value)
	{
		if(m_property_name == name && m_value == 0)
		{
			m_type = FLOAT;
			m_value = new char[sizeof(float)];
			memcpy(m_value, &value, sizeof(value));
			m_value_size = sizeof(value);
		}
	}



} // ~namespace Lux