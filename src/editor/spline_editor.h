#pragma once

#include "studio_app.h"

namespace black {

	struct SplineEditor : StudioApp::IPlugin {
		virtual void setSplinePoints(EntityRef entity, Span<const Vec3> points) = 0;
	};

	SplineEditor* createSplineEditor(StudioApp& app);

}