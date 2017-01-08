#include "editor/studio_app.h"
#include "engine/win/simple_win.h"
#define STB_RESOURCE_DONT_INCLUDE_WINDOWS_H
#include "stb/stb_resource.h"


INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
{
	auto* app = StudioApp::create();
	app->run();
	int exit_code = app->getExitCode();
	StudioApp::destroy(*app);
	return exit_code;
}
