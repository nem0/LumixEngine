#include "engine/iplugin.h"
#include "engine/string.h"


namespace Lumix
{
	IPlugin::~IPlugin() {}


	
	static StaticPluginRegister* s_first_plugin = nullptr;


	StaticPluginRegister::StaticPluginRegister(const char* name, IPlugin* (*creator)(Engine& engine))
	{
		this->creator = creator;
		this->name = name;
		next = s_first_plugin;
		s_first_plugin = this;
	}


	IPlugin* StaticPluginRegister::create(const char* name, Engine& engine)
	{
		auto* i = s_first_plugin;
		while (i)
		{
			if (equalStrings(name, i->name))
			{
				return i->creator(engine);
			}
			i = i->next;
		}
		return nullptr;
	}
}
