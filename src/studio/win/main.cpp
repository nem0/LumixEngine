#include <Windows.h>
#include <Winuser.h>
#include <ShellScalingAPI.h>
#include "editor/studio_app.h"
#define MF_RESOURCE_DONT_INCLUDE_WINDOWS_H
#include "stb/mf_resource.h"
#pragma comment(lib, "Shcore.lib")


INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
{
	SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

	auto* app = Lumix::StudioApp::create();
	app->run();
	int exit_code = app->getExitCode();
	Lumix::StudioApp::destroy(*app);
	return exit_code;
}
