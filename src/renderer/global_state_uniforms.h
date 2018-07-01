#pragma once


#include "engine/matrix.h"
#include "ffr/ffr.h"


namespace Lumix
{

class GlobalStateUniforms
{
public:
	void create();
	void destroy();
	void update();

	struct {
		Matrix camera_projection;
		Matrix camera_view;
		Vec4 camera_pos;
		Vec4 light_direction;
		Vec4 light_color;
		float light_intensity;
		float light_indirect_intensity;
		Int2 framebuffer_size;
	} state;

private:
	ffr::BufferHandle handle = ffr::INVALID_BUFFER;
};

} // namespace Lumix

