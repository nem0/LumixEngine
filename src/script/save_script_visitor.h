#pragma once
#include "core/lumix.h"
#include "script_visitor.h"
#include "core/map.h"
#include "core/string.h"


namespace Lumix
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
			Lumix::Map<Lumix::string, char*> m_items;
	};


} // ~ namespace Lumix
