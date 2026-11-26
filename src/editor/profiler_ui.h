#pragma once

#include "editor/studio_app.h"

namespace Lumix {

template <typename T> struct UniquePtr;

struct ProfilerUI : StudioApp::GUIPlugin {
	// an extra tab of the profiler window, e.g. GPU hardware counters of the renderer
	struct Tab {
		virtual ~Tab() {}
		virtual const char* getName() const = 0;
		virtual void onGUI() = 0;
	};

	virtual void snapshot() = 0;
	virtual void addTab(Tab& tab) = 0;
	virtual void removeTab(Tab& tab) = 0;
};

UniquePtr<ProfilerUI> createProfilerUI(StudioApp& app);

}
