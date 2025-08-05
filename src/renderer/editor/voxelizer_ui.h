#pragma once

#include "engine/math.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "renderer/voxels.h"

namespace Lumix {

struct VoxelizerUI : StudioApp::GUIPlugin {
	VoxelizerUI(StudioApp& app);
	~VoxelizerUI();
	
	void onWindowGUI() override;
	const char* getName() const override { return "voxelizer"; }
	void onSettingsLoaded() override;
	void onBeforeSettingsSaved() override;

private:
	bool isOpen() const { return m_is_open; }
	void toggleOpen() { m_is_open = !m_is_open; }
	void open(const char* path);
	void draw();
	void visualize();
	void visualizeAO();

	Voxels m_module;

	Action m_toggle_ui;
	bool m_is_open = false;
	StudioApp& m_app;
	struct Model* m_model = nullptr;
	u32 m_max_resolution = 32;
	bool m_debug_draw = true;
	float m_ao_multiplier = 2;
	struct Vertex {
		Vec3 pos;
		u32 color;
	};
	Array<Vertex> m_debug_triangles;
	u32 m_ray_count = 16;
};

} // namespace Lumix