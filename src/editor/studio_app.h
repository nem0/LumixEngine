#pragma once


#include "engine/lumix.h"
#include "engine/array.h"


#ifdef STATIC_PLUGINS
	#define LUMIX_STUDIO_ENTRY(plugin_name) \
		extern "C" void setStudioApp_##plugin_name(StudioApp& app); \
		extern "C" { StudioApp::StaticPluginRegister s_##plugin_name##_editor_register(#plugin_name, setStudioApp_##plugin_name); } \
		extern "C" void setStudioApp_##plugin_name(StudioApp& app)
#else
	#define LUMIX_STUDIO_ENTRY(plugin_name) \
		extern "C" LUMIX_LIBRARY_EXPORT void setStudioApp(StudioApp& app)	
#endif


struct SDL_Window;


namespace Lumix
{
struct ComponentUID;
struct ResourceType;
class WorldEditor;
}


struct Action;


class LUMIX_EDITOR_API StudioApp
{
public:
	struct IPlugin
	{
		virtual ~IPlugin() {}

		virtual void onWindowGUI() = 0;
		virtual bool hasFocus() { return false; }
		virtual void update(float) {}
		virtual void pluginAdded(IPlugin& plugin) {}
		virtual const char* getName() const = 0;
	};

	struct IAddComponentPlugin
	{
		virtual ~IAddComponentPlugin() {}
		virtual void onGUI(bool create_entity, bool from_filter) = 0;
		virtual const char* getLabel() const = 0;
	};

	struct AddCmpTreeNode
	{
		IAddComponentPlugin* plugin = nullptr;
		AddCmpTreeNode* child = nullptr;
		AddCmpTreeNode* next = nullptr;
		char label[50];
	};

	struct DragData
	{
		enum Type
		{
			NONE,
			PATH,
			ENTITY
		};
		Type type;
		void* data;
		int size;
	};

	struct LUMIX_EDITOR_API StaticPluginRegister
	{
		typedef void (*Creator)(StudioApp& app);
		StaticPluginRegister(const char* name, Creator creator);

		static void create(StudioApp& app);

		StaticPluginRegister* next;
		Creator creator;
		const char* name;
	};

public:
	static StudioApp* create();
	static void destroy(StudioApp& app);

	virtual class Metadata* getMetadata() = 0;
	virtual class PropertyGrid* getPropertyGrid() = 0;
	virtual class LogUI* getLogUI() = 0;
	virtual class AssetBrowser* getAssetBrowser() = 0;
	virtual Lumix::WorldEditor* getWorldEditor() = 0;
	virtual void addPlugin(IPlugin& plugin) = 0;
	virtual void removePlugin(IPlugin& plugin) = 0;
	virtual const char* getComponentTypeName(Lumix::ComponentType cmp_type) const = 0;
	virtual void registerComponent(const char* id, const char* label) = 0;
	virtual void registerComponent(const char* id, IAddComponentPlugin& plugin) = 0;
	virtual void registerComponentWithResource(const char* id,
		const char* label,
		Lumix::ResourceType resource_type,
		const char* property_name) = 0;
	virtual const AddCmpTreeNode& getAddComponentTreeRoot() const = 0;
	virtual int getExitCode() const = 0;
	virtual void runScript(const char* src, const char* script_name) = 0;
	virtual const Lumix::Array<Action*>& getActions() = 0;
	virtual Lumix::Array<Action*>& getToolbarActions() = 0;
	virtual void addAction(Action* action) = 0;
	virtual void addWindowAction(Action* action) = 0;
	virtual Action* getAction(const char* name) = 0;
	virtual SDL_Window* getWindow() = 0;
	virtual void startDrag(DragData::Type type, const void* data, int size) = 0;
	virtual DragData getDragData() = 0;

	virtual ~StudioApp() {}
	virtual void run() = 0;
};


