#pragma once


#include "engine/lumix.h"


struct ImFont;

#ifdef STATIC_PLUGINS
	#define LUMIX_STUDIO_ENTRY(plugin_name) \
		extern "C" Lumix::StudioApp::IPlugin* setStudioApp_##plugin_name(Lumix::StudioApp& app); \
		extern "C" Lumix::StudioApp::IPlugin* setStudioApp_##plugin_name(Lumix::StudioApp& app)
#else
	#define LUMIX_STUDIO_ENTRY(plugin_name) \
		extern "C" LUMIX_LIBRARY_EXPORT Lumix::StudioApp::IPlugin* setStudioApp(Lumix::StudioApp& app)
#endif


namespace Lumix {


template <typename T> struct Array;
template <typename T> struct UniquePtr;
struct Action;
struct ComponentUID;
namespace Gizmo { struct Config; }
namespace os {
	enum class MouseButton;
	struct Event;
}

struct LUMIX_EDITOR_API StudioApp {
	struct IPlugin {
		virtual ~IPlugin() {}
		virtual void init() = 0;
		virtual bool dependsOn(IPlugin& plugin) const { return false; }
		virtual const char* getName() const = 0;

		virtual bool showGizmo(struct WorldView& view, struct ComponentUID cmp) = 0;
	};

	struct MousePlugin {
		virtual ~MousePlugin() {}

		virtual bool onMouseDown(WorldView& view, int x, int y) { return false; }
		virtual void onMouseUp(WorldView& view, int x, int y, os::MouseButton button) {}
		virtual void onMouseMove(WorldView& view, int x, int y, int rel_x, int rel_y) {}
		virtual void onMouseWheel(float value) {}
		virtual const char* getName() const = 0;
	};

	struct GUIPlugin {
		virtual ~GUIPlugin() {}
		virtual void onGUI() = 0;
		virtual bool hasFocus() const { return false; }
		virtual bool onAction(const Action& action) { return false; }
		virtual void update(float) {}
		virtual void pluginAdded(GUIPlugin& plugin) {}
		virtual const char* getName() const = 0;
		virtual bool onDropFile(const char* file) { return false; }
		virtual bool exportData(const char* dest_dir) { return true; }
		virtual void guiEndFrame() {}
		virtual void onSettingsLoaded() {}
		virtual void onBeforeSettingsSaved() {}
	};

	struct IAddComponentPlugin {
		virtual ~IAddComponentPlugin() {}
		virtual void onGUI(bool create_entity, bool from_filter, EntityPtr parent, struct WorldEditor& editor) = 0;
		virtual const char* getLabel() const = 0;
	};

	struct AddCmpTreeNode {
		IAddComponentPlugin* plugin = nullptr;
		AddCmpTreeNode* child = nullptr;
		AddCmpTreeNode* next = nullptr;
		char label[50];
	};

	static StudioApp* create();
	static void destroy(StudioApp& app);

	virtual struct IAllocator& getAllocator() = 0;
	virtual struct Engine& getEngine() = 0;
	virtual WorldEditor& getWorldEditor() = 0;
	virtual void run() = 0;
	virtual int getExitCode() const = 0;
	
	virtual struct PropertyGrid& getPropertyGrid() = 0;
	virtual struct LogUI& getLogUI() = 0;
	virtual struct AssetBrowser& getAssetBrowser() = 0;
	virtual struct AssetCompiler& getAssetCompiler() = 0;
	virtual struct FileSelector& getFileSelector() = 0;
	virtual struct DirSelector& getDirSelector() = 0;
	
	virtual struct Settings& getSettings() = 0;
	virtual struct RenderInterface* getRenderInterface() = 0;
	virtual void setRenderInterface(RenderInterface* ifc) = 0;

	virtual void addPlugin(IPlugin& plugin) = 0;
	virtual void addPlugin(MousePlugin& plugin) = 0;
	virtual void addPlugin(GUIPlugin& plugin) = 0;
	virtual void removePlugin(GUIPlugin& plugin) = 0;
	virtual void removePlugin(MousePlugin& plugin) = 0;
	virtual IPlugin* getIPlugin(const char* name) = 0;
	virtual GUIPlugin* getGUIPlugin(const char* name) = 0;
	virtual MousePlugin* getMousePlugin(const char* name) = 0;
	virtual Span<MousePlugin*> getMousePlugins() = 0;

	virtual const char* getComponentTypeName(ComponentType cmp_type) const = 0;
	virtual const char* getComponentIcon(ComponentType cmp_type) const = 0;
	virtual void registerComponent(const char* icon, const char* id, IAddComponentPlugin& plugin) = 0;
	virtual const AddCmpTreeNode& getAddComponentTreeRoot() const = 0;

	virtual const Array<Action*>& getActions() = 0;
	virtual void addAction(Action* action) = 0;
	virtual void removeAction(Action* action) = 0;
	virtual void addToolAction(Action* action) = 0;
	virtual void addWindowAction(Action* action) = 0;
	virtual Action* getAction(const char* name) = 0;
	
	virtual void scanWorlds() = 0;
	virtual void runScript(const char* src, const char* script_name) = 0;
	virtual void setFullscreen(bool fullscreen) = 0;
	virtual void snapDown() = 0;
	virtual float getFOV() const = 0;
	virtual void setFOV(float fov_radians) = 0;
	virtual Gizmo::Config& getGizmoConfig() = 0;
	virtual void setCursorCaptured(bool captured) = 0;
	virtual void saveSettings() = 0;
	virtual int getImGuiKey(int keycode) const = 0;
	virtual u32 getDockspaceID() const = 0;

	virtual Span<const os::Event> getEvents() const = 0;
	virtual ImFont* getBoldFont() = 0;
	virtual ImFont* getBigIconFont() = 0;
	
	virtual struct CommonActions& getCommonActions() = 0;

	virtual ~StudioApp() {}
};


} // namespace Lumix
