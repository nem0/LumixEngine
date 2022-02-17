#pragma once

#include "../gpu/gpu.h"
#include <imgui/imgui.h>

namespace Lumix {
	struct IImGuiRenderer {
		virtual void render(gpu::TextureHandle rt, Vec2 rt_size, ImDrawData* dd, Vec2 scale) = 0;
	};
};