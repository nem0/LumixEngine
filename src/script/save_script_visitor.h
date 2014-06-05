#pragma once
#include "core/lux.h"
#include "script_visitor.h"
#include "core/map.h"
#include "core/string.h"


namespace Lux
{


	class SaveScriptVisitor : public ScriptVisitor
	{
		public:
			virtual ~SaveScriptVisitor();

			void startSaving();
			void startLoading();

			virtual void visit(const char* name, float& value) override;
		
		private:
			enum Mode
			{
				SAVE,
				LOAD
			};

		private:
			Mode m_mode;
			Lux::Map<Lux::string, char*> m_items;
	};


} // ~ namespace Lux