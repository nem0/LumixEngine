#include <imgui/imgui.h>
#include <imgui/imgui_user.h>
#include "lumscript/lumscript.h"
#include "lumscript/lumscript_engine_api.h"
#include "lumscript/lumscript_resource.h"
#include "core/log.h"
#include "core/path.h"
#include "editor/action.h"
#include "editor/studio_app.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/editor_asset.h"
#include "editor/utils.h"
#include "engine/engine.h"
#include "engine/component_uid.h"
#include "engine/file_system.h"
#include "core/stream.h"

namespace Lumix {

namespace LumScriptTokens {

static inline const u32 token_colors[] = {
	IM_COL32(0xFF, 0x00, 0xFF, 0xff),
	IM_COL32(0xe1, 0xe1, 0xe1, 0xff),
	IM_COL32(0xf7, 0xc9, 0x5c, 0xff),
	IM_COL32(0xE5, 0x8A, 0xC9, 0xff),
	IM_COL32(0x93, 0xDD, 0xFA, 0xff),
	IM_COL32(0x67, 0x6b, 0x6f, 0xff),
};

enum class TokenType : u8 {
	EMPTY,
	IDENTIFIER,
	NUMBER,
	KEYWORD,
	OPERATOR,
	COMMENT,
};

static bool isWordChar(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static bool tokenize(const char* str, u32& token_len, u8& token_type, u8) {
	static const char* keywords[] = {
		"and",
		"as",
		"bool",
		"case",
		"const",
		"defer",
		"else",
		"enum",
		"f32",
		"f64",
		"false",
		"fn",
		"i8",
		"i16",
		"i32",
		"i64",
		"if",
		"import",
		"match",
		"null",
		"not",
		"or",
		"ref",
		"return",
		"struct",
		"true",
		"u8",
		"u16",
		"u32",
		"u64",
		"var",
		"void",
		"while",
	};

	const char* c = str;
	if (!*c) return false;

	if (c[0] == '/' && c[1] == '/') {
		token_type = (u8)TokenType::COMMENT;
		while (*c) ++c;
		token_len = u32(c - str);
		return *c;
	}

	const char operators[] = "*/+-%.<>;=(),:{}!";
	for (char op : operators) {
		if (*c == op) {
			token_type = (u8)TokenType::OPERATOR;
			token_len = 1;
			return *c;
		}
	}

	if (*c >= '0' && *c <= '9') {
		token_type = (u8)TokenType::NUMBER;
		while (*c >= '0' && *c <= '9') ++c;
		if (*c == '.') {
			++c;
			while (*c >= '0' && *c <= '9') ++c;
		}
		token_len = u32(c - str);
		return *c;
	}

	if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '_') {
		token_type = (u8)TokenType::IDENTIFIER;
		while (isWordChar(*c)) ++c;
		token_len = u32(c - str);
		const StringView token_view(str, str + token_len);
		for (const char* kw : keywords) {
			if (equalStrings(kw, token_view)) {
				token_type = (u8)TokenType::KEYWORD;
				break;
			}
		}
		return *c;
	}

	token_type = (u8)TokenType::IDENTIFIER;
	token_len = 1;
	++c;
	return *c;
}

} // namespace LumScriptTokens

struct LumScriptEditorWindow final : AssetEditorWindow {
	LumScriptEditorWindow(const Path& path, StudioApp& app)
		: AssetEditorWindow(app)
		, m_path(path)
		, m_message(app.getAllocator())
	{
		m_editor = createCodeEditor(app);
		m_editor->setTokenColors(LumScriptTokens::token_colors);
		m_editor->setTokenizer(&LumScriptTokens::tokenize);
		m_editor->focus();

		OutputMemoryStream blob(app.getAllocator());
		if (app.getEngine().getFileSystem().getContentSync(path, blob)) {
			m_editor->setText(StringView((const char*)blob.data(), (u32)blob.size()));
		}
	}

	const char* getName() const override { return "lumscript_editor"; }
	const Path& getPath() override { return m_path; }

	void fileChangedExternally() override {
		OutputMemoryStream editor_blob(m_app.getAllocator());
		OutputMemoryStream file_blob(m_app.getAllocator());
		m_editor->serializeText(editor_blob);
		if (!m_app.getEngine().getFileSystem().getContentSync(m_path, file_blob)) return;
		if (editor_blob.size() == file_blob.size() && memcmp(editor_blob.data(), file_blob.data(), editor_blob.size()) == 0) {
			m_dirty = false;
		}
	}

	void save() {
		OutputMemoryStream blob(m_app.getAllocator());
		m_editor->serializeText(blob);
		m_app.getAssetBrowser().saveResource(m_path, blob);
		m_dirty = false;
	}

	void check() {
		OutputMemoryStream blob(m_app.getAllocator());
		m_editor->serializeText(blob);
		LumScript::Module module(m_app.getAllocator());
		LumScript::Diagnostics diagnostics(m_app.getAllocator());
		auto import_resolver = [](LumScript::Module& module, StringView path, StringView alias, StringView* source, void*) -> bool {
			if (LumScript::resolveEngineImport(module, nullptr, path, alias)) {
				*source = {};
				return true;
			}
			return false;
		};
		if (LumScript::compileWithBuiltins(module, StringView((const char*)blob.data(), (u32)blob.size()), diagnostics, import_resolver, nullptr)) {
			m_message = "OK";
			logInfo("LumScript check OK: ", m_path);
		}
		else {
			m_message = diagnostics.message;
			logError("LumScript check failed: ", m_path, ": ", diagnostics.message);
		}
	}

	void windowGUI() override {
		CommonActions& actions = m_app.getCommonActions();
		if (ImGui::BeginMenuBar()) {
			if (actions.save.iconButton(m_dirty, &m_app)) save();
			if (actions.open_externally.iconButton(true, &m_app)) m_app.getAssetBrowser().openInExternalEditor(m_path);
			if (actions.view_in_browser.iconButton(true, &m_app)) m_app.getAssetBrowser().locate(m_path);
			if (ImGuiEx::IconButton(ICON_FA_CHECK, "Check")) check();
			ImGui::EndMenuBar();
		}

		if (m_message.length() > 0) {
			ImGui::TextUnformatted(m_message.c_str());
			ImGui::Separator();
		}
		if (m_editor->gui("lumscript_editor", ImGui::GetContentRegionAvail(), m_app.getMonospaceFont(), m_app.getDefaultFont())) {
			m_dirty = true;
		}
	}

	Path m_path;
	UniquePtr<CodeEditor> m_editor;
	String m_message;
};

struct LumScriptAssetPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	explicit LumScriptAssetPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("lum", LumScriptResource::TYPE);
	}

	void addSubresources(AssetCompiler& compiler, const Path& path, AtomicI32&) override {
		compiler.addResource(LumScriptResource::TYPE, path);
	}

	void openEditor(const Path& path) override {
		auto win = UniquePtr<LumScriptEditorWindow>::create(m_app.getAllocator(), path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	bool compile(const Path& src) override {
		// For LumScript, we just copy the source file as-is
		// The runtime will parse and compile it when loaded
		return m_app.getAssetCompiler().copyCompile(src);
	}

	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return "lum"; }
	
	void createResource(OutputMemoryStream& content) override {
		const char* template_str = R"(fn update(dt : f32) : void {
	// Update logic here
}
)";
		content.write(template_str, stringLength(template_str));
	}
	
	const char* getIcon() const override { return ICON_FA_FILE_CODE; }
	const char* getLabel() const override { return "LumScript"; }
	ResourceType getResourceType() const override { return LumScriptResource::TYPE; }

	StudioApp& m_app;
};

struct LumScriptPlugin : StudioApp::IPlugin {
	explicit LumScriptPlugin(StudioApp& app)
		: m_app(app)
		, m_asset_plugin(app)
	{
	}

	const char* getName() const override { return "lumscript"; }

	void init() override {
		const char* lum_exts[] = {"lum"};
		m_app.getAssetBrowser().addPlugin(m_asset_plugin, Span(lum_exts));
		m_app.getAssetCompiler().addPlugin(m_asset_plugin, Span(lum_exts));
	}

	bool showGizmo(struct WorldView& view, struct ComponentUID cmp) override {
		return false;
	}

	~LumScriptPlugin() {
		m_app.getAssetBrowser().removePlugin(m_asset_plugin);
		m_app.getAssetCompiler().removePlugin(m_asset_plugin);
	}

private:
	StudioApp& m_app;
	LumScriptAssetPlugin m_asset_plugin;
};

LUMIX_STUDIO_ENTRY(lumscript) {
	auto& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, LumScriptPlugin)(app);
}

} // namespace Lumix
