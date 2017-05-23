#include "editor/studio_app.h"
#include "engine/win/simple_win.h"
#define MF_RESOURCE_DONT_INCLUDE_WINDOWS_H
#include "stb/mf_resource.h"


INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
{
	auto* app = Lumix::StudioApp::create();
	app->run();
	int exit_code = app->getExitCode();
	Lumix::StudioApp::destroy(*app);
	return exit_code;
}
