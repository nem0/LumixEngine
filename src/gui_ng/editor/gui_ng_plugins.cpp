#include <imgui/imgui.h>
#include <imgui/imgui_user.h>
#include "code_editor.h"
#include "core/color.h"
#include "core/log.h"
#include "core/path.h"
#include "core/sort.h"
#include "core/stream.h"
#include "core/string.h"
#include "editor/action.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/editor_asset.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/text_filter.h"
#include "editor/world_editor.h"
#include "editor/utils.h"
#include "engine/component_uid.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "gui_ng/gui_ng_module.h"
#include "gui_ng/ui.h"
#include "renderer/draw2d.h"
#include "renderer/font.h"
#include "renderer/gpu/gpu.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"

namespace Lumix {



static ResourceType UI_DOCUMENT_TYPE("ui_document");

struct UIEditorWindow : AssetEditorWindow {
	static inline const char* s_autocomplete_words[] = {
		"panel",
		"span",
		"direction",
		"width",
		"height",
		"bg-color",
		"font-size",
		"font",
		"value",
		"color",
		"margin",
		"fit-content",
		"row",
		"column",
		nullptr
	};

	UIEditorWindow(const Path& path, StudioApp& app)
		: AssetEditorWindow(app)
		, m_app(app)
		, m_path(path)
		, m_stored_text(app.getAllocator())
		, m_autocomplete_list(app.getAllocator())
		, m_autocomplete_action("UI Editor", "Autocomplete", "Autocomplete", "ui_autocomplete", "", Action::NORMAL)
	{
		Engine& engine = m_app.getEngine();
		Renderer& renderer = *static_cast<Renderer*>(engine.getSystemManager().getSystem("renderer"));
		m_pipeline = Pipeline::create(renderer, PipelineType::GUI_EDITOR);
		m_font_manager = LUMIX_NEW(m_app.getAllocator(), UIFontManager)(engine);
		m_document = LUMIX_NEW(m_app.getAllocator(), ui::Document)(m_font_manager, m_app.getAllocator());

		m_editor = createGUICodeEditor(m_app);
		m_editor->focus();
			
		OutputMemoryStream blob(app.getAllocator());
		if (app.getEngine().getFileSystem().getContentSync(path, blob)) {
			StringView v((const char*)blob.data(), (u32)blob.size());
			m_editor->setText(v);
			m_stored_text.resize((u32)blob.size());
			memcpy(m_stored_text.getMutableData(), blob.data(), blob.size());
			StringView text(m_stored_text.c_str(), m_stored_text.length());
			m_parse_success = m_document->parse(text, "markup");
			m_needs_layout = true;
		}
	}

	~UIEditorWindow() {
		m_pipeline.reset();
		if (m_document) LUMIX_DELETE(m_app.getAllocator(), m_document);
		if (m_font_manager) LUMIX_DELETE(m_app.getAllocator(), m_font_manager);
	}

	void fileChangedExternally() override {
		OutputMemoryStream tmp(m_app.getAllocator());
		OutputMemoryStream tmp2(m_app.getAllocator());
		m_editor->serializeText(tmp);
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.getContentSync(m_path, tmp2)) return;

		if (tmp.size() == tmp2.size() && memcmp(tmp.data(), tmp2.data(), tmp.size()) == 0) {
			m_dirty = false;
		}
	}

	void save() {
		OutputMemoryStream blob(m_app.getAllocator());
		m_editor->serializeText(blob);
		m_app.getAssetBrowser().saveResource(m_path, blob);
		m_stored_text.resize((u32)blob.size());
		memcpy(m_stored_text.getMutableData(), blob.data(), blob.size());
		m_dirty = false;
		refresh();
	}

	void refresh() {
		OutputMemoryStream blob(m_app.getAllocator());
		m_editor->serializeText(blob);
		m_stored_text.resize((u32)blob.size());
		memcpy(m_stored_text.getMutableData(), blob.data(), blob.size());
		StringView text(m_stored_text.c_str(), m_stored_text.length());
		m_parse_success = m_document->parse(text, "markup");
		m_needs_layout = true;	
	}

	void windowGUI() override {
		CommonActions& actions = m_app.getCommonActions();

		if (ImGui::BeginMenuBar()) {
			if (actions.save.iconButton(m_dirty, &m_app)) save();
			if (actions.open_externally.iconButton(true, &m_app)) m_app.getAssetBrowser().openInExternalEditor(m_path);
			if (actions.view_in_browser.iconButton(true, &m_app)) m_app.getAssetBrowser().locate(m_path);
			if (ImGuiEx::IconButton(ICON_FA_SYNC, "Refresh")) refresh();
			ImGui::EndMenuBar();
		}

		ImGui::Columns(2);
		if (m_editor->gui("codeeditor", ImVec2(0, 0), m_app.getMonospaceFont(), m_app.getDefaultFont())) m_dirty = true;
		handleAutocomplete();
		autocompletePopupGUI();
		ImGui::NextColumn();
		ImGui::ColorEdit3("Background", &m_clear_color.x);
		ImVec2 canvas_size = ImGui::GetContentRegionAvail();
		if (canvas_size.x > 0 && canvas_size.y > 0) {
			Vec2 current_canvas_size(canvas_size.x, canvas_size.y);
			if (current_canvas_size != m_previous_canvas_size || m_needs_layout) {
				m_previous_canvas_size = current_canvas_size;
				if (m_parse_success) {
					m_document->computeLayout(Vec2(canvas_size.x, canvas_size.y));
				}
				m_needs_layout = false;
			}
			if (m_parse_success) {
				Viewport vp = {};
				vp.w = (int)canvas_size.x;
				vp.h = (int)canvas_size.y;
				m_pipeline->setWorld(m_app.getWorldEditor().getWorld());
				m_pipeline->setViewport(vp);
				m_pipeline->setClearColor(m_clear_color);

				Draw2D& draw2d = m_pipeline->getDraw2D();
				m_document->render(draw2d);

				if (m_pipeline->render(true)) {
					gpu::TextureHandle texture_handle = m_pipeline->getOutput();
					if (texture_handle) {
						if (gpu::isOriginBottomLeft()) {
							ImGui::Image(texture_handle, canvas_size, ImVec2(0, 1), ImVec2(1, 0));
						} else {
							ImGui::Image(texture_handle, canvas_size);
						}
					}
				}
			}
		}
		ImGui::Columns(1);
	}

	void handleAutocomplete() {
		if (!m_editor->canHandleInput()) return;
		if (m_editor->getNumCursors() != 1) return;
		if (!m_app.checkShortcut(m_autocomplete_action)) return;

		StringView prefix = m_editor->getPrefix();

		m_autocomplete_list.clear();

		// keyword autocomplete
		for (const char** kw = s_autocomplete_words; *kw; ++kw) {
			if (prefix.size() > 0 && !startsWith(*kw, prefix)) continue;
			m_autocomplete_list.emplace(*kw, m_app.getAllocator());
		}
		sort(m_autocomplete_list.begin(), m_autocomplete_list.end(), [](const String& a, const String& b) {
			return compareString(a, b) < 0;
		});

		if (m_autocomplete_list.empty()) return;

		if (m_autocomplete_list.size() == 1) {
			m_editor->selectWord();
			m_editor->insertText(m_autocomplete_list[0].c_str());
			m_autocomplete_list.clear();
		} else {
			ImGui::OpenPopup("ui_autocomplete");
			m_autocomplete_filter.clear();
			m_autocomplete_selection_idx = 0;
			ImGui::SetNextWindowPos(m_editor->getCursorScreenPosition());
		}
	}

	void autocompletePopupGUI() {
		if (!ImGui::BeginPopup("ui_autocomplete", ImGuiWindowFlags_NoNav)) return;

		u32 sel_idx = m_autocomplete_selection_idx;
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) m_autocomplete_selection_idx += m_autocomplete_list.size() - 1;
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ++m_autocomplete_selection_idx;
		m_autocomplete_selection_idx = m_autocomplete_selection_idx % m_autocomplete_list.size();
		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			ImGui::CloseCurrentPopup();
			m_editor->focus();
		}

		bool is_child = false;
		if (m_autocomplete_list.size() > 12) {
			ImGui::PushFont(m_app.getDefaultFont());
			m_autocomplete_filter.gui("Filter", 250, ImGui::IsWindowAppearing());
			ImGui::PopFont();
			ImGui::BeginChild("asl", ImVec2(0, ImGui::GetTextLineHeight() * 12));
			is_child = true;
		}

		bool is_enter = ImGui::IsKeyPressed(ImGuiKey_Enter);
		u32 i = 0;
		for (const String& s : m_autocomplete_list) {
			if (!m_autocomplete_filter.pass(s.c_str())) continue;
			if (i - 1 == m_autocomplete_selection_idx) ImGui::SetScrollHereY(0.5f);
			const bool is_selected = i == sel_idx;
			if (ImGui::Selectable(s.c_str(), is_selected) || (is_enter && i == m_autocomplete_selection_idx)) {
				m_editor->selectWord();
				m_editor->insertText(s.c_str());
				ImGui::CloseCurrentPopup();
				m_editor->focus();
				m_autocomplete_list.clear();
				break;
			}
			++i;
		}
		m_autocomplete_selection_idx = minimum(m_autocomplete_selection_idx, i > 0 ? i - 1 : 0);
		if (is_child) ImGui::EndChild();

		ImGui::EndPopup();
	}

	const char* getName() const override { return "UI Editor"; }
	const Path& getPath() override { return m_path; }

	StudioApp& m_app;
	Path m_path;
	UniquePtr<CodeEditor> m_editor;
	String m_stored_text;
	Array<String> m_autocomplete_list;
	u32 m_autocomplete_selection_idx = 0;
	TextFilter m_autocomplete_filter;
	Action m_autocomplete_action;
	ui::IFontManager* m_font_manager = nullptr;
	ui::Document* m_document = nullptr;
	Vec2 m_previous_canvas_size = Vec2(0, 0);
	bool m_parse_success = false;
	bool m_needs_layout = false;
	UniquePtr<Pipeline> m_pipeline;
	Vec3 m_clear_color = Vec3(0.5f, 0.5f, 0.5f);
};

struct UIPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	explicit UIPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("ui", UI_DOCUMENT_TYPE);
	}

	void addSubresources(AssetCompiler& compiler, const Path& path, AtomicI32&) override {
		compiler.addResource(UI_DOCUMENT_TYPE, path);
	}

	void openEditor(const Path& path) override {
		UniquePtr<UIEditorWindow> win = UniquePtr<UIEditorWindow>::create(m_app.getAllocator(), path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	bool compile(const Path& src) override { return m_app.getAssetCompiler().copyCompile(src); }
	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return "ui"; }
	void createResource(OutputMemoryStream& content) override {
		const char* template_str = R"(panel [width=100% height=2em padding=0.5em bg-color=#ffffff align=center font="/engine/editor/fonts/JetBrainsMono-Regular.ttf" font-size=60] { "Hello World" })";
		content.write(template_str, stringLength(template_str));
	}
	const char* getIcon() const override { return ICON_FA_FILE_CODE; }
	const char* getLabel() const override { return "UI Document"; }
	ResourceType getResourceType() const override { return UI_DOCUMENT_TYPE; }

	StudioApp& m_app;
};

struct GUINGPlugin : StudioApp::IPlugin {
	GUINGPlugin(StudioApp& app) : m_app(app), m_ui_plugin(app) {}

	const char* getName() const override { return "gui_ng"; }

	void init() override {
		const char* ui_exts[] = {"ui"};
		m_app.getAssetBrowser().addPlugin(m_ui_plugin, Span(ui_exts));
		m_app.getAssetCompiler().addPlugin(m_ui_plugin, Span(ui_exts));
	}

	bool showGizmo(struct WorldView& view, ComponentUID cmp) override {
		// TODO: Implement gizmos if needed
		return false;
	}

	~GUINGPlugin() {
		m_app.getAssetBrowser().removePlugin(m_ui_plugin);
		m_app.getAssetCompiler().removePlugin(m_ui_plugin);
	}

private:
	StudioApp& m_app;
	UIPlugin m_ui_plugin;
};

LUMIX_STUDIO_ENTRY(gui_ng) {
	return LUMIX_NEW(app.getAllocator(), GUINGPlugin)(app);
}

} // namespace Lumix