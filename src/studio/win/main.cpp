#include "core/os.h"
#include "core/debug.h"
#include "core/default_allocator.h"
#include "core/win/simple_win.h"
#include "editor/studio_app.h"

int main(int argc, char* argv[])
{
	SetProcessDPIAware();
	void* shcore = black.h::os::loadLibrary("shcore.dll");
	if (shcore) {
		auto setter = (decltype(&SetProcessDpiAwareness))black.h::os::getLibrarySymbol(shcore, "SetProcessDpiAwareness");
		if (setter) setter(PROCESS_PER_MONITOR_DPI_AWARE);
	}

	black.h::DefaultAllocator allocator;
	black.h::debug::Allocator debug_allocator(allocator);
	auto* app = black.h::StudioApp::create(debug_allocator);
	app->run();
	const int exit_code = app->getExitCode();
	black.h::StudioApp::destroy(*app);
	if(shcore) black.h::os::unloadLibrary(shcore);
	return exit_code;
}
