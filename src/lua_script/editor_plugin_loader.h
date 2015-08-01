#pragma once


struct lua_State;
class MainWindow;


namespace Lumix
{
class WorldEditor;
}


class EditorPluginLoader
{
public:
	EditorPluginLoader(MainWindow& win);
	~EditorPluginLoader();

	void setWorldEditor(Lumix::WorldEditor& editor);
	MainWindow& getMainWindow() const { return m_main_window; }

private:
	void registerAPI();

private:
	MainWindow& m_main_window;
	lua_State* m_global_state;
};