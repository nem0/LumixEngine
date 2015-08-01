#include "lua_plugin_loader.h"
#include "core/log.h"
#include "editor/world_editor.h"
#include "engine/lua_wrapper.h"
#include "mainwindow.h"
#include <lua.hpp>
#include <qdir.h>


LuaPluginLoader::LuaPluginLoader(MainWindow& main_window)
	: m_main_window(main_window)
{
	m_global_state = nullptr;
}


LuaPluginLoader::~LuaPluginLoader()
{
	if (m_global_state)
	{
		lua_close(m_global_state);
	}
}


static void
registerMenuFunction(lua_State* L, const char* name, const char* function)
{
	if (lua_getglobal(L, "API_plugin_loader") == LUA_TLIGHTUSERDATA)
	{
		auto loader = (LuaPluginLoader*)lua_touserdata(L, -1);
		TODO("finish this");
	}
	lua_pop(L, 1);
}


void LuaPluginLoader::registerAPI()
{
	lua_pushlightuserdata(m_global_state, this);
	lua_setglobal(m_global_state, "API_plugin_loader");

	auto f = Lumix::LuaWrapper::wrap<decltype(&registerMenuFunction),
									 registerMenuFunction>;
	lua_register(m_global_state, "API_registerMenuFunction", f);
}


void LuaPluginLoader::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_global_state = luaL_newstate();
	registerAPI();
	auto lua_plugins = QDir("plugins").entryInfoList(QStringList() << "*.lua");
	for (auto& lua_plugin : lua_plugins)
	{
		QFile file(lua_plugin.absoluteFilePath());
		if (file.open(QIODevice::ReadOnly))
		{
			auto content = file.readAll();
			bool errors =
				luaL_loadbuffer(
					m_global_state,
					content.data(),
					content.size(),
					lua_plugin.absoluteFilePath().toLatin1().data()) != LUA_OK;
			errors = errors ||
					 lua_pcall(m_global_state, 0, LUA_MULTRET, 0) != LUA_OK;
			if (errors)
			{
				Lumix::g_log_error.log("editor")
					<< lua_plugin.absoluteFilePath().toLatin1().data() << ": "
					<< lua_tostring(m_global_state, -1);
				lua_pop(m_global_state, 1);
			}
		}
		else
		{
			Lumix::g_log_warning.log("editor")
				<< "Could not open plugin "
				<< lua_plugin.absoluteFilePath().toLatin1().data();
		}
	}
}
