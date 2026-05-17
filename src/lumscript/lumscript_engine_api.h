#pragma once

#include "core/string.h"

namespace Lumix {

struct World;

namespace LumScript {

struct Module;

bool resolveEngineImport(Module& module, World* world, StringView path, StringView alias);

} // namespace LumScript

} // namespace Lumix
