#include "imgui_tests.h"
#include "editor/action.h"

#include <imgui/imgui.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_ui.h>

namespace Lumix {

namespace {

struct ImGuiTestsState {
	ImGuiTestEngine* engine = nullptr;
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
}

} // namespace


void initImGuiTests() {
	if (g_state.engine) return;

	g_state.engine = ImGuiTestEngine_CreateContext();
	ImGuiTestEngineIO& test_io = ImGuiTestEngine_GetIO(g_state.engine);
	test_io.ConfigSavedSettings = false;
	test_io.ConfigCaptureEnabled = false;
	test_io.ConfigCaptureOnError = false;
	test_io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
	test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
	registerEditorTests(g_state.engine);
	ImGuiTestEngine_Start(g_state.engine, ImGui::GetCurrentContext());
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
}


void showImGuiTestEngineWindows() {
	if (!g_state.engine) return;
	ImGuiTestEngine_ShowTestEngineWindows(g_state.engine, nullptr);
}

} // namespace Lumix
