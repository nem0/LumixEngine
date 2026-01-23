#include "tests/common.h"
#include "core/debug.h"
#include "core/log_callback.h"
#include "core/string.h"
#include <stdio.h>

void runParticleScriptTokenizerTests();
void runParticleScriptCompilerTests();
void runParticleScriptCollectorTests();

namespace Lumix {
	int test_count = 0;
	int passed_count = 0;
}

static void consoleLog(Lumix::LogLevel level, const char* message) {
	const char* prefix = "";
	switch (level) {
		case Lumix::LogLevel::INFO: prefix = "[INFO] "; break;
		case Lumix::LogLevel::WARNING: prefix = "[WARNING] "; break;
		case Lumix::LogLevel::ERROR: prefix = "[ERROR] "; break;
		default: break;
	}
	printf("%s%s\n", prefix, message);
}

int main(int argc, char* argv[]) {
	Lumix::registerLogCallback<&consoleLog>();
	Lumix::debug::init(Lumix::getGlobalAllocator());
	
	runParticleScriptTokenizerTests();
	runParticleScriptCompilerTests();
	runParticleScriptCollectorTests();
	Lumix::logInfo("=== Test Results: ", Lumix::passed_count, "/", Lumix::test_count, " passed ===");

	Lumix::unregisterLogCallback<&consoleLog>();
	return (Lumix::passed_count == Lumix::test_count) ? 0 : 1;
}
