#pragma once

#include "engine/engine.h"
#include "core/geometry.h"
#include "core/os.h"
#include "engine/world.h"
#include "renderer/pipeline.h"

namespace Lumix {

struct LUMIX_RENDERER_API WorldViewer {
	WorldViewer(struct StudioApp& app);
	~WorldViewer();

	void gui();
	void resetCamera();
	void resetCamera(const Model& model);
	void drawSkeleton(i32 selected_bone);
	void drawMeshTransform();
	void setModelPath(const Path& path);
	void setAnimatorPath(const Path& path);

	StudioApp& m_app;
	World* m_world;
	UniquePtr<Pipeline> m_pipeline;
	EntityPtr m_mesh;
	float m_camera_speed = 1;
	bool m_is_mouse_captured = false;
	bool m_follow_mesh = true;
	os::Point m_captured_mouse_pos;
	Viewport m_viewport;
};

}