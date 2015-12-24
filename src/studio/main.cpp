#include "studio_lib/studio_app.h"
#include <Windows.h>


int studioMain()
{
	auto* app = StudioApp::create();
	app->run();
	StudioApp::destroy(*app);
	return 0;
}


INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
{
	int studioMain();
	return studioMain();
}
