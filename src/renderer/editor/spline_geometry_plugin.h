#pragma once

#include "editor/property_grid.h"

namespace Lumix {

struct SplineGeometryPlugin final : PropertyGrid::IPlugin {
	SplineGeometryPlugin(StudioApp& app) ;
	void onGUI(PropertyGrid& grid, ComponentUID cmp, WorldEditor& editor) override;

	StudioApp& m_app;
	float m_dig_depth = 1.f;
};

} // namespace Lumix