#include "core/log.h"
#include "core/string.h"

using namespace Lumix;

void runLumScriptTokenizerTests();
void runLumScriptCompilerTests();
void runLumScriptRuntimeTests();

void runLumScriptTests() {
	logInfo("=== Running LumScript Tests ===");
	runLumScriptTokenizerTests();
	runLumScriptCompilerTests();
	runLumScriptRuntimeTests();
}
