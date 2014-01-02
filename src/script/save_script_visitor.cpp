#include "save_script_visitor.h"


namespace Lux
{


	SaveScriptVisitor::~SaveScriptVisitor()
	{
		Lux::map<Lux::string, char*>::iterator iter = m_items.begin(), end = m_items.end();
		for(; iter != end; ++iter)
		{
			delete[] iter.second();
		}
	}


	void SaveScriptVisitor::visit(const char* name, float& value)
	{
		/// TODO check if saved size == loaded size
		if(m_mode == SAVE)
		{
			char* data = new char[sizeof(value)];
			memcpy(data, &value, sizeof(value));
			m_items.insert(string(name), data);
		}
		else
		{
			char* item_value;
			if(m_items.find(string(name), item_value))
			{
				memcpy(&value, item_value, sizeof(value));
			}
		}
	}

	void SaveScriptVisitor::startSaving()
	{
		m_mode = SAVE;
	}

	void SaveScriptVisitor::startLoading()
	{
		m_mode = LOAD;
	}



} // ~namespace Lux