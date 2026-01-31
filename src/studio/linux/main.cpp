#include "editor/studio_app.h"
#include "core/os.h"

int main(int argc, char* argv[])
{
	black.h::os::setCommandLine(argc, argv);
	auto* app = black.h::StudioApp::create();
	app->run();
	int exit_code = app->getExitCode();
	black.h::StudioApp::destroy(*app);
	return exit_code;
}
