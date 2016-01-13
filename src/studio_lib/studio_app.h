#pragma once


#include "lumix.h"
#include "core/array.h"


namespace Lumix
{
class WorldEditor;
}


class LUMIX_STUDIO_LIB_API StudioApp
{
public:
	class IPlugin
	{
	public:
		virtual ~IPlugin() {}

		virtual void onWindowGUI() = 0;
		virtual bool hasFocus() { return false; }
		virtual void update(float) {}

		struct Action* m_action;
	};

public:
	static StudioApp* create();
	static void destroy(StudioApp& app);

	virtual class PropertyGrid* getPropertyGrid() = 0;
	virtual class LogUI* getLogUI() = 0;
	virtual class AssetBrowser* getAssetBrowser() = 0;
	virtual Lumix::WorldEditor* getWorldEditor() = 0;
	virtual void addPlugin(IPlugin& plugin) = 0;
	virtual void removePlugin(IPlugin& plugin) = 0;
	virtual int getExitCode() const = 0;
	virtual void runScript(const char* src, const char* script_name) = 0;
	virtual void registerLuaFunction(const char* name, int (*function)(struct lua_State*)) = 0;
	virtual void registerLuaGlobal(const char* name, void* data) = 0;
	virtual Lumix::Array<Action*>& getActions() = 0;

	virtual ~StudioApp() {}
	virtual void run() = 0;
};

