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
#include "editor/asset_compiler.h"
#include "editor/studio_app.h"
#include "engine/engine.h"
#include "engine/file_system.h"


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

// Scripts importing each other, committed in data/scripts/tests/evox_deps/<dir>, so the project must be `data`.
// When names[changed] is touched, exactly the files in `recompiled_mask` (bit i = names[i]) must be recompiled,
// and then the compiler must go idle (a dependency cycle must not requeue forever).
void runEvoxDependencyTest(ImGuiTestContext* ctx, const char* dir, Span<const char* const> names, int changed, u32 recompiled_mask) {
	if (isWelcomeScreenOpen(ctx)) {
		ctx->LogInfo("Skipping: no project is open.");
		return;
	}

	FileSystem& fs = g_state.app->getEngine().getFileSystem();
	const int count = (int)names.length();
	IM_CHECK_RETV(count <= 8, (void)0);
	Path paths[8];
	u64 before[8] = {};
	for (int i = 0; i < count; ++i) paths[i] = Path("scripts/tests/evox_deps/", dir, "/", names[i], ".evox");
	if (!fs.fileExists(paths[0])) {
		ctx->LogInfo("Skipping: %s not found, the project is not `data`.", paths[0].c_str());
		return;
	}

	auto compiledTime = [&](int i) -> u64 {
		const Path res(".lumix/resources/", paths[i].getHash().getHashValue(), ".res");
		return fs.fileExists(res) ? fs.getLastModified(res) : 0;
	};
	// rewrite the file with the same content, so it is picked up as changed
	auto touch = [&](int i) {
		OutputMemoryStream content(g_state.app->getAllocator());
		return fs.getContentSync(paths[i], content) && fs.saveContentSync(paths[i], Span((const u8*)content.data(), (u32)content.size()));
	};
	// wait for `cond`, the file watcher and compiler need a few frames
	auto waitFor = [&](auto cond, float timeout) {
		for (float t = 0; t < timeout; t += 0.1f) {
			if (cond()) return true;
			ctx->SleepNoSkip(0.1f, 0.05f);
		}
		return cond();
	};

	// compile everything first, so the dependencies are registered and we have timestamps to compare to
	for (int i = 0; i < count; ++i) before[i] = compiledTime(i);
	for (int i = 0; i < count; ++i) IM_CHECK_RETV(touch(i), (void)0);
	IM_CHECK_RETV(waitFor([&]() {
		for (int i = 0; i < count; ++i) if (compiledTime(i) <= before[i]) return false;
		return true;
	}, 10.f), (void)0);

	// let the initial compilation settle
	ctx->SleepNoSkip(1.5f, 0.1f);
	for (int i = 0; i < count; ++i) before[i] = compiledTime(i);

	IM_CHECK_RETV(touch(changed), (void)0);
	IM_CHECK(waitFor([&]() {
		for (int i = 0; i < count; ++i) {
			if ((recompiled_mask >> i) & 1 && compiledTime(i) <= before[i]) return false;
		}
		return true;
	}, 10.f));

	// no more compiles after the change was handled, and files that don't depend on the change were left alone
	ctx->SleepNoSkip(1.5f, 0.1f);
	u64 after[8] = {};
	for (int i = 0; i < count; ++i) after[i] = compiledTime(i);
	ctx->SleepNoSkip(3.f, 0.1f);
	for (int i = 0; i < count; ++i) {
		IM_CHECK(compiledTime(i) == after[i]);
		if (!((recompiled_mask >> i) & 1)) {
			if (after[i] != before[i]) ctx->LogError("%s was recompiled, but does not depend on the change", names[i]);
			IM_CHECK(after[i] == before[i]);
		}
	}
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

	// the scripts are in data/scripts/tests/evox_deps/<dir>, run Studio with `-data_dir data`
	// mutual: a <-> b
	t = IM_REGISTER_TEST(engine, "asset_compiler", "evox_mutual_import");
	t->TestFunc = [](ImGuiTestContext* ctx) {
		const char* const names[] = { "a", "b" };
		runEvoxDependencyTest(ctx, "mutual", names, 0, 0b11);
	};

	// chain: c imports b, b imports a
	t = IM_REGISTER_TEST(engine, "asset_compiler", "evox_chain_root_changed");
	t->TestFunc = [](ImGuiTestContext* ctx) {
		const char* const names[] = { "a", "b", "c" };
		runEvoxDependencyTest(ctx, "chain", names, 0, 0b111);
	};

	t = IM_REGISTER_TEST(engine, "asset_compiler", "evox_chain_middle_changed");
	t->TestFunc = [](ImGuiTestContext* ctx) {
		const char* const names[] = { "a", "b", "c" };
		runEvoxDependencyTest(ctx, "chain", names, 1, 0b110); // a is a dependency of b, it must not be recompiled
	};

	// diamond: b and c import a, d imports both b and c
	t = IM_REGISTER_TEST(engine, "asset_compiler", "evox_diamond_root_changed");
	t->TestFunc = [](ImGuiTestContext* ctx) {
		const char* const names[] = { "a", "b", "c", "d" };
		runEvoxDependencyTest(ctx, "diamond", names, 0, 0b1111);
	};

	t = IM_REGISTER_TEST(engine, "asset_compiler", "evox_diamond_side_changed");
	t->TestFunc = [](ImGuiTestContext* ctx) {
		const char* const names[] = { "a", "b", "c", "d" };
		runEvoxDependencyTest(ctx, "diamond", names, 1, 0b1010); // only b and d, c is a sibling
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
