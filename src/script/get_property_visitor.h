#pragma once
#include "core/lumix.h"
#include "script_visitor.h"
#include "core/map.h"
#include "core/string.h"


namespace Lumix
{


	class GetPropertyVisitor : public ScriptVisitor
	{
		public:
			GetPropertyVisitor(const char* name);
			virtual ~GetPropertyVisitor();

			virtual void visit(const char* name, float& value) override;

		public:
			enum PropertyType
			{
				FLOAT
			};

		public:	
			char* m_value;
			int m_value_size;
			PropertyType m_type;
		
		private:
			Lumix::string m_property_name;
	};


} // ~ namespace Lumix
