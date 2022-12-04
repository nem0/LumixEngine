#pragma once

#define LUMIX_NO_CUSTOM_CRT
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
	void visualize(u32 mip);

	Voxels m_scene;

	Action m_toggle_ui;
	bool m_is_open = false;
	StudioApp& m_app;
	struct Model* m_model = nullptr;
	u32 m_max_resolution = 32;
	bool m_debug_draw = true;
	Array<Vec3> m_debug_triangles;
};

} // namespace Lumix