#pragma once


#include "core/lumix.h"
#include "core/string.h"
#include "engine/iplugin.h"
#include "universe/universe.h"


namespace Lumix
{


class Engine;
class Path;


class LUMIX_SCRIPT_API ScriptScene : public IScene
{
	public:
		virtual void getScriptPath(Component cmp, string& str) = 0;
		virtual const Lumix::Path& getScriptPath(Component cmp) = 0;
		virtual void setScriptPath(Component cmp, const string& str) = 0;

		virtual void serializeScripts(OutputBlob& blob) = 0;
		virtual void deserializeScripts(InputBlob& blob) = 0;

		virtual void beforeScriptCompiled() = 0;
		virtual void afterScriptCompiled() = 0;

		virtual DelegateList<void(const Path&, const Path&)>& scriptRenamed() = 0;

		virtual Component getFirstScript() = 0;
		virtual Component getNextScript(const Component& cmp) = 0;

		virtual Engine& getEngine() = 0;
};


extern "C"
{
	LUMIX_SCRIPT_API IPlugin* createPlugin(Engine& engine);
}


} // ~namespace Lumix
