#include "tests/common.h"
#include "core/debug.h"
#include "core/log_callback.h"
#include "core/string.h"
#include <stdio.h>

void runParticleScriptTokenizerTests();
void runParticleScriptCompilerTests();
void runParticleScriptCollectorTests();

namespace black {
	int test_count = 0;
	int passed_count = 0;
}

static void consoleLog(black.h::LogLevel level, const char* message) {
	const char* prefix = "";
	switch (level) {
		case black.h::LogLevel::INFO: prefix = "[INFO] "; break;
		case black.h::LogLevel::WARNING: prefix = "[WARNING] "; break;
		case black.h::LogLevel::ERROR: prefix = "[ERROR] "; break;
		default: break;
	}
	printf("%s%s\n", prefix, message);
}

int main(int argc, char* argv[]) {
	black.h::registerLogCallback<&consoleLog>();
	black.h::debug::init(black.h::getGlobalAllocator());
	
	runParticleScriptTokenizerTests();
	runParticleScriptCompilerTests();
	runParticleScriptCollectorTests();
	black.h::logInfo("=== Test Results: ", black.h::passed_count, "/", black.h::test_count, " passed ===");

	black.h::unregisterLogCallback<&consoleLog>();
	return (black.h::passed_count == black.h::test_count) ? 0 : 1;
}
