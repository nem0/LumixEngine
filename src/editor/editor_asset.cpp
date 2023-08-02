#include "editor_asset.h"

namespace Lumix {

EditorAssetPlugin::EditorAssetPlugin(const char* name, const char* ext, ResourceType type, StudioApp& app, IAllocator& allocator)
	: m_app(app)
	, m_name(name)
	, m_extension(ext)
	, m_resource_type(type)
{
	AssetCompiler& compiler = app.getAssetCompiler();
	compiler.registerExtension(ext, type);
	const char* extensions[] = { ext, nullptr };
	compiler.addPlugin(*this, extensions);
	AssetBrowser& browser = app.getAssetBrowser();
	browser.addPlugin(*this);
}

EditorAssetPlugin::~EditorAssetPlugin() {
	m_app.getAssetBrowser().removePlugin(*this);
	m_app.getAssetCompiler().removePlugin(*this);
}

void AssetEditorWindow::onGUI() {
	bool open = true;
	m_has_focus = false;

	ImGui::SetNextWindowDockID(m_dock_id ? m_dock_id : m_app.getDockspaceID(), ImGuiCond_Appearing);

	if (m_focus_request) {
		ImGui::SetNextWindowFocus();
		m_focus_request = false;
	}

	ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoSavedSettings;
	if (m_dirty) flags |= ImGuiWindowFlags_UnsavedDocument;

	Span<const char> basename = Path::getBasename(getPath().c_str());
	StaticString<128> title(basename, "##ae", (uintptr)this);

	if (ImGui::Begin(title, &open, flags)) {
		m_has_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		m_dock_id = ImGui::GetWindowDockID();
		windowGUI();
	}
	if (!open) {
		if (m_dirty) {
			ImGui::OpenPopup("Confirm##cvse");
		}
		else {
			m_app.getAssetBrowser().closeWindow(*this);
		}
	}

	if (ImGui::BeginPopupModal("Confirm##cvse", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextUnformatted("All changes will be lost. Continue anyway?");
		if (ImGui::Selectable("Yes")) m_app.getAssetBrowser().closeWindow(*this);
		ImGui::Selectable("No");
		ImGui::EndPopup();
	}
	ImGui::End();
}

}