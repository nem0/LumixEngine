#include "core/os.hpp"
#include "core/win/simple_win.hpp"
#include "editor/studio_app.hpp"

int main(int argc, char* argv[])
{
	SetProcessDPIAware();
	void* shcore = Lumix::os::loadLibrary("shcore.dll");
	if (shcore) {
		auto setter = (decltype(&SetProcessDpiAwareness))Lumix::os::getLibrarySymbol(shcore, "SetProcessDpiAwareness");
		if (setter) setter(PROCESS_PER_MONITOR_DPI_AWARE);
	}

	auto* app = Lumix::StudioApp::create();
	app->run();
	const int exit_code = app->getExitCode();
	Lumix::StudioApp::destroy(*app);
	if(shcore) Lumix::os::unloadLibrary(shcore);
	return exit_code;
}
