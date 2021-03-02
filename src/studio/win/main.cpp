#define NOGDI
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Winuser.h>
#include <ShellScalingAPI.h>
#include "editor/studio_app.h"

int main(int argc, char* argv[])
{
	SetProcessDPIAware();
	HMODULE shcore = LoadLibrary("shcore.dll");
	if (shcore) {
		auto setter = (decltype(&SetProcessDpiAwareness))GetProcAddress(shcore, "SetProcessDpiAwareness");
		if (setter) setter(PROCESS_PER_MONITOR_DPI_AWARE);
	}

	auto* app = Lumix::StudioApp::create();
	app->run();
	const int exit_code = app->getExitCode();
	Lumix::StudioApp::destroy(*app);
	if(shcore) FreeLibrary(shcore);
	return exit_code;
}
