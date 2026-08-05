#pragma once

#include "mir.h"
#include "compiler.h"

// Build one checked function in memory form. This is deliberately independent
// of bytecode emission; the old compiler remains the reference backend.
MirFunction* mirBuildFunction(ls_arena& arena, FunctionExpression* function, ls_string_view name = {});
MirFunction* mirBuildGlobalInit(ls_arena& arena, ls_module* module);
MirModule* mirBuildModule(ls_arena& arena, ls_module* module);
ls_bytecode* mirCompileModuleBytecode(MirModule* module, ls_host* host);
