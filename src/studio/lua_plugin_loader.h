#pragma once


struct lua_State;
class MainWindow;


namespace Lumix
{
class WorldEditor;
}


class LuaPluginLoader
{
public:
	LuaPluginLoader(MainWindow& win);
	~LuaPluginLoader();

	void setWorldEditor(Lumix::WorldEditor& editor);

private:
	void registerAPI();

private:
	MainWindow& m_main_window;
	lua_State* m_global_state;
};