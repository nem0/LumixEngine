#pragma once
#include "core/lumix.h"
#include "script_visitor.h"
#include "core/map.h"
#include "core/string.h"


namespace Lumix
{


	class SetPropertyVisitor : public ScriptVisitor
	{
		public:
			SetPropertyVisitor(const char* name, void* value, int value_size);
			virtual ~SetPropertyVisitor();

			virtual void visit(const char* name, float& value) override;
		
		private:
			Lumix::string m_property_name;
			char* m_value;
			int m_value_size;
	};


} // ~ namespace Lumix
