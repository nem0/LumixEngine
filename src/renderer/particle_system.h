#pragma once


#include "lumix.h"


namespace Lumix
{


class IAllocator;
class Universe;


namespace Particles
{
	typedef int EmitterHandle;

	void init(IAllocator& allocator);
	void shutdown();

	EmitterHandle createEmitter(Entity entity, Universe& universe);
	void destroyEmitter(EmitterHandle emitter);

	void update(EmitterHandle emitter, float time_delta);
	void render(EmitterHandle emitter);
};


} // namespace Lumix
