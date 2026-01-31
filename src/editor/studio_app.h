#pragma once

#include "core/span.h"
#include "engine/black.h.h"

struct ImFont;

#ifdef STATIC_PLUGINS
	#define BLACK_STUDIO_ENTRY(plugin_name) \
		extern "C" black.h::StudioApp::IPlugin* setStudioApp_##plugin_name(black.h::StudioApp& app); \
		extern "C" black.h::StudioApp::IPlugin* setStudioApp_##plugin_name(black.h::StudioApp& app)
#else
	#define BLACK_STUDIO_ENTRY(plugin_name) \
		extern "C" BLACK_LIBRARY_EXPORT black.h::StudioApp::IPlugin* setStudioApp(black.h::StudioApp& app)
#endif


namespace black {


template <typename T> struct Array;
template <typename T> struct DelegateList;
template <typename T> struct UniquePtr;
struct Action;
struct ComponentUID;
struct Path;
namespace Gizmo { struct Config; }
namespace os {
	using WindowHandle = void*;
	enum class MouseButton;
	struct Event;
	struct Rect;
}

//@ object
struct BLACK_EDITOR_API StudioApp {
	struct IPlugin {
		virtual ~IPlugin() {}
		virtual void init() = 0;
		virtual bool dependsOn(IPlugin& plugin) const { return false; }
		virtual const char* getName() const = 0;
		virtual void update(float time_delta) {}

		virtual bool showGizmo(struct WorldView& view, struct ComponentUID cmp) = 0;
	};

	struct BLACK_EDITOR_API MousePlugin {
		virtual ~MousePlugin() {}

		virtual bool onMouseDown(WorldView& view, int x, int y) { return false; }
		virtual void onMouseUp(WorldView& view, int x, int y, os::MouseButton button) {}
		virtual void onMouseMove(WorldView& view, int x, int y, int rel_x, int rel_y) {}
		virtual void onMouseWheel(float value) {}
		virtual const char* getName() const = 0;
	};

	struct BLACK_EDITOR_API GUIPlugin {
		virtual ~GUIPlugin() {}
		virtual void onGUI() = 0;
		virtual void update(float) {}
		virtual void pluginAdded(GUIPlugin& plugin) {}
		virtual const char* getName() const = 0;
		virtual bool onDropFile(const char* file) { return false; }
		virtual bool exportData(const char* dest_dir) { return true; }
		virtual void guiEndFrame() {}
		virtual void onSettingsLoaded() {}
		virtual void onBeforeSettingsSaved() {}
		virtual void setProjectDir(const char* project_dir) {}

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

	static StudioApp* create(struct IAllocator& allocator);
	static void destroy(StudioApp& app);

	virtual struct IAllocator& getAllocator() = 0;
	virtual struct Engine& getEngine() = 0;
	virtual WorldEditor& getWorldEditor() = 0;
	virtual void run() = 0;
	virtual int getExitCode() const = 0;
	//@ function
	virtual void exitWithCode(int exit_code) = 0;
	//@ function
	virtual void exitGameMode() = 0;
	virtual const char* getProjectDir() = 0;
	
	virtual struct PropertyGrid& getPropertyGrid() = 0;
	virtual struct LogUI& getLogUI() = 0;
	virtual struct AssetBrowser& getAssetBrowser() = 0;
	virtual struct AssetCompiler& getAssetCompiler() = 0;
	virtual struct FileSelector& getFileSelector() = 0;
	virtual struct DirSelector& getDirSelector() = 0;
	
	virtual struct Settings& getSettings() = 0;
	virtual struct RenderInterface* getRenderInterface() = 0;
	virtual void setRenderInterface(RenderInterface* ifc) = 0;
	virtual DelegateList<void(const char*)>& fileChanged() = 0;
	virtual void tryLoadWorld(const struct Path& path, bool additive) = 0;

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

	virtual Action* getAction(const char* name) = 0;
	
	//@ function
	virtual void newWorld() = 0;
	virtual void setFullscreen(bool fullscreen) = 0;
	virtual float getFOV() const = 0;
	virtual void setFOV(float fov_radians) = 0;
	virtual Gizmo::Config& getGizmoConfig() = 0;
	virtual void saveSettings() = 0;
	virtual int getImGuiKey(int keycode) const = 0;
	virtual u32 getDockspaceID() const = 0;
	virtual void beginCustomTicking() = 0;
	virtual void endCustomTicking() = 0;
	virtual void beginCustomTick() = 0;
	virtual void endCustomTick() = 0;

	// clip mouse cursor = keep it in specified rectangle
	// cursor is automatically unclipped when app is inactive
	virtual void clipMouseCursor() = 0;
	// some platforms can't clip to `screen_rect` so they ignore it and use just `win`'s client rectangle
	virtual void setMouseClipRect(os::WindowHandle win, const os::Rect &screen_rect) = 0;
	virtual void unclipMouseCursor() = 0;
	virtual bool isMouseCursorClipped() const = 0;
	// if true, shortcuts are not processed
	// use case - when there's active game in game view, we don't want delete keypress to delete entities
	virtual void setCaptureInput(bool capture) = 0;
	virtual bool checkShortcut(Action& action, bool global = false, bool force = false) = 0;

	virtual Span<const os::Event> getEvents() const = 0;
	virtual ImFont* getDefaultFont() = 0;
	virtual ImFont* getBoldFont() = 0;
	virtual ImFont* getBigIconFont() = 0;
	virtual ImFont* getMonospaceFont() = 0;
	
	virtual struct CommonActions& getCommonActions() = 0;

	virtual ~StudioApp() {}
};


} // namespace black
