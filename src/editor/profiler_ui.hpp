#pragma once

#include "editor/studio_app.hpp"

namespace Lumix {

template <typename T> struct UniquePtr;

UniquePtr<StudioApp::GUIPlugin> createProfilerUI(StudioApp& app);

}
