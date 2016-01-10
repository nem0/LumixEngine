#include "studio_lib/studio_app.h"
#include <Windows.h>


INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
{
	auto* app = StudioApp::create();
	app->run();
	int exit_code = app->getExitCode();
	StudioApp::destroy(*app);
	return exit_code;
}
