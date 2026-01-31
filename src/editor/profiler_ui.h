#pragma once

#include "editor/studio_app.h"

namespace black {

template <typename T> struct UniquePtr;

struct ProfilerUI : StudioApp::GUIPlugin {
	virtual void snapshot() = 0;
};

UniquePtr<ProfilerUI> createProfilerUI(StudioApp& app);

}
