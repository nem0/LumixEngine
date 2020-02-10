#include "editor/studio_app.h"
#include "engine/os.h"

int main(int argc, char* argv[])
{
	auto* app = Lumix::StudioApp::create();
	Lumix::OS::setCommandLine(argc, argv);
	app->run();
	int exit_code = app->getExitCode();
	Lumix::StudioApp::destroy(*app);
	return exit_code;
}
