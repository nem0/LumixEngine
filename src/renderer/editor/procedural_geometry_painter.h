#pragma once

#include "editor/property_grid.h"

namespace Lumix {

struct ProceduralGeometryPainter final : StudioApp::GUIPlugin, StudioApp::MousePlugin {
	ProceduralGeometryPainter(StudioApp& app);

private:
	void onWindowGUI() override;
	const char* getName() const override { return "procedural_geom_painter"; }
	bool onMouseDown(UniverseView& view, int x, int y) override;
	void onMouseUp(UniverseView& view, int x, int y, os::MouseButton button) override;
	void onMouseMove(UniverseView& view, int x, int y, int rel_x, int rel_y) override;
	void onMouseWheel(float value) override;
	
	void paint(const DVec3& pos, const Universe& universe, EntityRef entity, struct ProceduralGeometry& pg, struct Renderer& renderer) const;
	bool paint(UniverseView& view, i32 x, i32 y);
	void drawCursor(WorldEditor& editor, EntityRef entity) const;
	void drawCursor(WorldEditor& editor, RenderScene& scene, EntityRef entity, const DVec3& center) const;
	
	StudioApp& m_app;
	float m_dig_depth = 1.f;
	float m_brush_size = 1.f;
	i32 m_brush_value = 0xff;
	u32 m_brush_channel = 0;
};

} // namespace Lumix