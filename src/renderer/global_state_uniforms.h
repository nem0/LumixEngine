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

	struct State {
		Matrix shadow_view_projection;
		Matrix shadowmap_matrices[4];
		Matrix camera_projection;
		Matrix camera_view;
		Matrix camera_view_projection;
		Matrix camera_inv_view_projection;
		Vec4 camera_pos;
		Vec4 light_direction;
		Vec3 light_color;
		float light_intensity;
		float light_indirect_intensity;
		Int2 framebuffer_size;
	} state;

private:
	ffr::BufferHandle handle = ffr::INVALID_BUFFER;
};

} // namespace Lumix

