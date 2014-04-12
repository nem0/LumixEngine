#pragma once
#include "core/lux.h"
#include "script_visitor.h"
#include "core/map.h"
#include "core/string.h"


namespace Lux
{


	class SetPropertyVisitor : public ScriptVisitor
	{
		public:
			SetPropertyVisitor(const char* name, void* value, int value_size);
			virtual ~SetPropertyVisitor();

			virtual void visit(const char* name, float& value) override;
		
		private:
			Lux::string m_property_name;
			char* m_value;
			int m_value_size;
	};


} // ~ namespace Lux