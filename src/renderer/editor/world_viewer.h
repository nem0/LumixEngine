#pragma once

#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/os.h"
#include "engine/world.h"
#include "renderer/pipeline.h"

namespace Lumix {

LUMIX_RENDERER_API struct WorldViewer {
	WorldViewer(struct StudioApp& app);
	~WorldViewer();

	void gui();
	void resetCamera(const Model& model);
	void drawSkeleton();

	StudioApp& m_app;
	World* m_world;
	UniquePtr<Pipeline> m_pipeline;
	EntityPtr m_mesh;
	float m_camera_speed = 1;
	bool m_is_mouse_captured = false;
	bool m_focus_mesh = false;
	os::Point m_captured_mouse_pos;
	Viewport m_viewport;
};

}