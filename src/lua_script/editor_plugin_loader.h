#pragma once


struct lua_State;
class MainWindow;


namespace Lumix
{
class Engine;
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
	void registerAPI(Lumix::Engine& engine);
	void onUniverseCreated();
	void onUniverseDestroyed();

private:
	Lumix::WorldEditor* m_world_editor;
	MainWindow& m_main_window;
	lua_State* m_global_state;
};