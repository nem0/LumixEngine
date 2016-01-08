#include "studio_lib/studio_app.h"
#include <Windows.h>


INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
{
	auto* app = StudioApp::create();
	app->run();
	StudioApp::destroy(*app);
	return 0;
}
