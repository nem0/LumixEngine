#pragma once

struct ls_module;

namespace Lumix {

struct StringView;
struct World;

namespace LumScript {

struct Module;

bool resolveEngineImport(ls_module& module, World* world, StringView path, StringView alias);

} // namespace LumScript

} // namespace Lumix
