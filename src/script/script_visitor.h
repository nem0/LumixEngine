#pragma once


namespace Lux
{


	class ScriptVisitor
	{
		public:
			virtual ~ScriptVisitor() {}

			virtual void visit(const char* name, float& value) = 0;
	};


} // ~ namespace Lux