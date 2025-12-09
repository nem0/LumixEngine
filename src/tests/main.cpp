#include "tests/common.h"
#include "core/debug.h"
#include "core/log_callback.h"
#include <stdio.h>

void runParticleScriptTokenizerTests();
void runParticleScriptCompilerTests();

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
	
	Lumix::unregisterLogCallback<&consoleLog>();
	return 0;
}
