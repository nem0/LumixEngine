#include "global_state_uniforms.h"

namespace Lumix {


void GlobalStateUniforms::create()
{
	handle = ffr::createBuffer(sizeof(state), &state); 
	ffr::bindUniformBuffer(0, handle, 0, sizeof(state));
}


void GlobalStateUniforms::destroy()
{
	ffr::destroy(handle);
	handle = ffr::INVALID_BUFFER;
}


void GlobalStateUniforms::update()
{
	ffr::update(handle, &state, 0, sizeof(state));
}


} // namespace Lumix