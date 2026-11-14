#include "core/os.h"
#include "core/debug.h"
#include "core/default_allocator.h"
#include "editor/studio_app.h"

int main(int argc, char* argv[])
{
	Lumix::DefaultAllocator allocator;
	Lumix::debug::Allocator debug_allocator(allocator);
	auto* app = Lumix::StudioApp::create(debug_allocator);
	app->run();
	const int exit_code = app->getExitCode();
	Lumix::StudioApp::destroy(*app);
	return exit_code;
}
