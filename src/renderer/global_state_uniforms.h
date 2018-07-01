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
		Vec3 light_direction;
		float padding;
		Vec3 light_color;
		float padding2;
		float light_intensity;
		Vec3 padding3;
		float light_indirect_intensity;
		Vec3 padding4;
		Int2 framebuffer_size;
		Int2 padding5;
		Vec3 camera_pos;
		float padding6;
	} state;

private:
	ffr::BufferHandle handle = ffr::INVALID_BUFFER;
};

} // namespace Lumix

