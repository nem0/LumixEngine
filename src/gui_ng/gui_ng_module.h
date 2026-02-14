#pragma once

#include "engine/plugin.h"

namespace Lumix {

//@ module GUINGModule gui_ng "GUI NG"
struct GUINGModule : IModule {
	static UniquePtr<GUINGModule> createInstance(struct GUINGSystem& system, World& world, struct IAllocator& allocator);
	static void reflect();

	// TODO: Add GUI NG specific functions here
};

} // namespace Lumix