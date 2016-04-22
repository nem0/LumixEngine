#pragma once


#include "engine/lumix.h"
#include "engine/core/array.h"


#ifdef STATIC_PLUGINS
	#define LUMIX_STUDIO_ENTRY(plugin_name) \
		extern "C" void setStudioApp_##plugin_name(StudioApp& app); \
		extern "C" StudioApp::StaticPluginRegister s_##plugin_name##_editor_register(#plugin_name, setStudioApp_##plugin_name); \
		extern "C" void setStudioApp_##plugin_name(StudioApp& app)
#else
	#define LUMIX_STUDIO_ENTRY(plugin_name) \
		extern "C" LUMIX_LIBRARY_EXPORT void setStudioApp(StudioApp& app)	
#endif


namespace Lumix
{
class WorldEditor;
}


class LUMIX_EDITOR_API StudioApp
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

	struct LUMIX_EDITOR_API StaticPluginRegister
	{
		typedef void (*Creator)(StudioApp& app);
		StaticPluginRegister(const char* name, Creator creator);

		static void create(const char* name, StudioApp& app);

		StaticPluginRegister* next;
		Creator creator;
		const char* name;
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
	virtual Lumix::Array<Action*>& getActions() = 0;

	virtual ~StudioApp() {}
	virtual void run() = 0;
};


