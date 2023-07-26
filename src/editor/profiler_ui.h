#pragma once

#include "editor/studio_app.h"

namespace Lumix {

template <typename T> struct UniquePtr;

UniquePtr<StudioApp::GUIPlugin> createProfilerUI(StudioApp& app);

}
