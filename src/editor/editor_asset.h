#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/utils.h"

namespace Lumix {

// use this if you want an editor-only asset to be visible in asset browser
// editor only assets do not inherit from Resource, e.g. particle system function
// it can also be used as a base for normal asset plugin
struct EditorAssetPlugin : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	EditorAssetPlugin(const char* name, const char* ext, ResourceType type, StudioApp& app, IAllocator& allocator);
	~EditorAssetPlugin();

	bool compile(const Path& src) override { return true; }
	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return m_extension; }
	const char* getLabel() const override { return m_name; }

protected:
	StudioApp& m_app;
	const char* m_extension;
	const char* m_name;
};

// common funcitonality for asset editor windows
struct AssetEditorWindow : StudioApp::GUIPlugin {
	AssetEditorWindow(StudioApp& app) : m_app(app) {}

	virtual void windowGUI() = 0;
	virtual const Path& getPath() = 0;

	bool hasFocus() const override { return m_has_focus; }
	void onGUI() override;

	StudioApp& m_app;
	ImGuiID m_dock_id = 0;
	bool m_focus_request = false;
	bool m_has_focus = false;
	bool m_dirty = false;
};

}