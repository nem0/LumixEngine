#pragma once


#include "engine/plugin.h"

namespace Lumix {

struct JoltModule : IModule {
	static UniquePtr<JoltModule> create(struct JoltSystem& system, World& world, struct Engine& engine, struct IAllocator& allocator);
	static void reflect();
};

} // namespace Lumix
