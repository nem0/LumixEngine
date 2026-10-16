#include <imgui/imgui.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_ui.h>

#include "imgui_tests.h"
#include "core/command_line_parser.h"
#include "core/log.h"
#include "core/stream.h"
#include "core/path.h"
#include "core/string.h"
#include "editor/action.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/editor_asset.h"
#include "editor/studio_app.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/resource_manager.h"
#include "renderer/texture.h"


namespace Lumix {

namespace {

struct ImGuiTestsState {
	ImGuiTestEngine* engine = nullptr;
	StudioApp* app = nullptr;
	bool run_and_exit = false; // -imgui_run_tests <filter>: run matching tests, then exit with 0 (all passed) or 1
};

ImGuiTestsState g_state;

static bool isWelcomeScreenOpen(ImGuiTestContext* ctx) {
	return ctx->ItemExists("//Welcome");
}

void registerEditorTests(ImGuiTestEngine* engine) {
	ImGuiTest* t = IM_REGISTER_TEST(engine, "editor", "welcome_screen_smoke");
	t->TestFunc = [](ImGuiTestContext* ctx) {
		if (!isWelcomeScreenOpen(ctx)) {
			ctx->LogInfo("Skipping: welcome screen is not active.");
			return;
		}

		ctx->SetRef("Welcome");
		IM_CHECK(ctx->ItemExists("titlebardrag"));
		IM_CHECK(ctx->ItemExists("Welcome to Lumix Studio"));
		IM_CHECK(ctx->ItemExists("**/Open / Create folder"));
	};

	t = IM_REGISTER_TEST(engine, "editor", "menu_bar_navigation");
	t->TestFunc = [](ImGuiTestContext* ctx) {
		if (isWelcomeScreenOpen(ctx)) {
			ctx->LogInfo("Skipping: main menu bar is hidden by welcome screen.");
			return;
		}

		ctx->MenuClick("//##MainMenuBar/File");
		ctx->MenuClick("//##MainMenuBar/Edit");
		ctx->MenuClick("//##MainMenuBar/Entity");
		ctx->MenuClick("//##MainMenuBar/Tools");
		ctx->MenuClick("//##MainMenuBar/View");
	};

	t = IM_REGISTER_TEST(engine, "editor", "asset_browser_toggle");
	t->TestFunc = [](ImGuiTestContext* ctx) {
		if (isWelcomeScreenOpen(ctx)) {
			ctx->LogInfo("Skipping: main menu bar is hidden by welcome screen.");
			return;
		}

		const bool assets_was_open = ctx->ItemExists("//Assets");
		ctx->MenuClick("//##MainMenuBar/View/Asset browser");
		ctx->Yield(2);
		IM_CHECK(ctx->ItemExists("//Assets") != assets_was_open);

		ctx->MenuClick("//##MainMenuBar/View/Asset browser");
		ctx->Yield(2);
		IM_CHECK(ctx->ItemExists("//Assets") == assets_was_open);
	};

	t = IM_REGISTER_TEST(engine, "editor", "view_window_toggle_matrix");
	t->TestFunc = [](ImGuiTestContext* ctx) {
		if (isWelcomeScreenOpen(ctx)) {
			ctx->LogInfo("Skipping: main menu bar is hidden by welcome screen.");
			return;
		}

		int toggled = 0;
		for (Action* action = Action::first_action; action; action = action->next) {
			if (action->type != Action::Type::WINDOW) continue;
			if (action->label_short[0] == '\0') continue;

			StaticString<128> path("//##MainMenuBar/View/", action->label_short);
			ctx->MenuClick(path.data);
			ctx->Yield(2);
			ctx->MenuClick(path.data);
			ctx->Yield(2);
			++toggled;
		}
		IM_CHECK(toggled > 0);
	};

	t = IM_REGISTER_TEST(engine, "editor", "ltct_open_close");
	t->TestFunc = [](ImGuiTestContext* ctx) {
		IM_CHECK_RETV(!isWelcomeScreenOpen(ctx), (void)0);
		const Path path("scripts/tests/texture_recipe.ltct");
		FileSystem& fs = g_state.app->getEngine().getFileSystem();
		IM_CHECK_RETV(fs.fileExists(path), (void)0);

		AssetBrowser& browser = g_state.app->getAssetBrowser();
		IM_CHECK_RETV(browser.getWindow(path) == nullptr, (void)0);
		browser.openEditor(path);
		ctx->Yield(3);
		AssetEditorWindow* window = browser.getWindow(path);
		IM_CHECK_RETV(window != nullptr, (void)0);
		StaticString<128> title("//", Path::getBasename(path), "##ae", (uintptr)window);
		ctx->WindowFocus(title.data);
		ctx->SetRef(title.data);
		ctx->Yield(2);
		IM_CHECK(ctx->WindowInfo("tab/recipe_editor").Window != nullptr);
		g_state.app->getCommonActions().close_window.request = true;
		ctx->Yield(3);
		IM_CHECK(browser.getWindow(path) == nullptr);
	};

	t = IM_REGISTER_TEST(engine, "editor", "ltct_array_open_close");
	t->TestFunc = [](ImGuiTestContext* ctx) {
		IM_CHECK_RETV(!isWelcomeScreenOpen(ctx), (void)0);
		const Path path("scripts/tests/texture_recipe_array.ltct");
		AssetBrowser& browser = g_state.app->getAssetBrowser();
		IM_CHECK_RETV(g_state.app->getEngine().getFileSystem().fileExists(path), (void)0);
		IM_CHECK_RETV(browser.getWindow(path) == nullptr, (void)0);
		browser.openEditor(path);
		ctx->Yield(3);
		AssetEditorWindow* window = browser.getWindow(path);
		IM_CHECK_RETV(window != nullptr, (void)0);
		Texture* texture = g_state.app->getEngine().getResourceManager().load<Texture>(path);
		for (float elapsed = 0; elapsed < 10.f && texture->isEmpty(); elapsed += 0.1f) ctx->SleepNoSkip(0.1f, 0.05f);
		IM_CHECK(texture->isReady());
		IM_CHECK(texture->depth == 3);
		texture->decRefCount();
		StaticString<128> title("//", Path::getBasename(path), "##ae", (uintptr)window);
		ctx->WindowFocus(title.data);
		ctx->SetRef(title.data);
		ctx->Yield(2);
		IM_CHECK(ctx->WindowInfo("tab/recipe_editor").Window != nullptr);
		IM_CHECK(ctx->ItemExists("tab/View layer"));
		g_state.app->getCommonActions().close_window.request = true;
		ctx->Yield(3);
		IM_CHECK(browser.getWindow(path) == nullptr);
	};

	t = IM_REGISTER_TEST(engine, "editor", "ltct_cube_open_close");
	t->TestFunc = [](ImGuiTestContext* ctx) {
		IM_CHECK_RETV(!isWelcomeScreenOpen(ctx), (void)0);
		const Path path("scripts/tests/texture_recipe_cube.ltct");
		AssetBrowser& browser = g_state.app->getAssetBrowser();
		IM_CHECK_RETV(g_state.app->getEngine().getFileSystem().fileExists(path), (void)0);
		IM_CHECK_RETV(browser.getWindow(path) == nullptr, (void)0);
		browser.openEditor(path);
		ctx->Yield(3);
		AssetEditorWindow* window = browser.getWindow(path);
		IM_CHECK_RETV(window != nullptr, (void)0);
		Texture* texture = g_state.app->getEngine().getResourceManager().load<Texture>(path);
		for (float elapsed = 0; elapsed < 10.f && texture->isEmpty(); elapsed += 0.1f) ctx->SleepNoSkip(0.1f, 0.05f);
		IM_CHECK(texture->isReady());
		IM_CHECK(texture->is_cubemap);
		texture->decRefCount();
		StaticString<128> title("//", Path::getBasename(path), "##ae", (uintptr)window);
		ctx->WindowFocus(title.data);
		ctx->SetRef(title.data);
		ctx->Yield(2);
		IM_CHECK(ctx->WindowInfo("tab/recipe_editor").Window != nullptr);
		IM_CHECK(ctx->ItemExists("tab/Side"));
		g_state.app->getCommonActions().close_window.request = true;
		ctx->Yield(3);
		IM_CHECK(browser.getWindow(path) == nullptr);
	};
}

} // namespace


void initImGuiTests(StudioApp& app) {
	if (g_state.engine) return;

	g_state.app = &app;
	g_state.engine = ImGuiTestEngine_CreateContext();
	ImGuiTestEngineIO& test_io = ImGuiTestEngine_GetIO(g_state.engine);
	test_io.ConfigSavedSettings = false;
	test_io.ConfigCaptureEnabled = false;
	test_io.ConfigCaptureOnError = false;
	test_io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
	test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
	registerEditorTests(g_state.engine);
	ImGuiTestEngine_Start(g_state.engine, ImGui::GetCurrentContext());

	char cmd_line[4096];
	if (os::getCommandLine(Span(cmd_line))) {
		CommandLineParser parser(cmd_line);
		while (parser.next()) {
			if (!parser.currentEquals("-imgui_run_tests")) continue;
			char filter[128] = "";
			if (parser.next()) parser.getCurrent(filter, sizeof(filter));
			test_io.ConfigNoThrottle = true;
			test_io.ConfigLogToTTY = true;
			ImGuiTestEngine_QueueTests(g_state.engine, ImGuiTestGroup_Tests, filter);
			g_state.run_and_exit = true;
			break;
		}
	}
}


void shutdownImGuiTests() {
	if (!g_state.engine) return;
	ImGuiTestEngine_Stop(g_state.engine);
	ImGuiTestEngine_DestroyContext(g_state.engine);
	g_state.engine = nullptr;
}


void postSwapImGuiTests() {
	if (!g_state.engine) return;
	ImGuiTestEngine_PostSwap(g_state.engine);

	if (g_state.run_and_exit && ImGuiTestEngine_IsTestQueueEmpty(g_state.engine)) {
		int tested = 0, succeeded = 0;
		ImGuiTestEngine_GetResult(g_state.engine, tested, succeeded);
		logInfo("ImGui tests: ", succeeded, "/", tested, " passed");
		g_state.run_and_exit = false;
		g_state.app->exitWithCode(tested > 0 && tested == succeeded ? 0 : 1);
	}
}


void showImGuiTestEngineWindows() {
	if (!g_state.engine) return;
	ImGuiTestEngine_ShowTestEngineWindows(g_state.engine, nullptr);
}

} // namespace Lumix
