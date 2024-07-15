#pragma once

#include "property_grid.h"
#include "studio_app.h"

namespace Lumix {

struct SignalEditor : PropertyGrid::IPlugin, StudioApp::IPlugin {
};

SignalEditor* createSignalEditor(StudioApp& app);

}
