#include "core/log.h"
#include "editor/studio_app.h"
#include "engine/component_uid.h"
#include "engine/engine.h"
#include "gui_ng/gui_ng_module.h"

using namespace Lumix;

namespace {

struct GUINGPlugin : StudioApp::IPlugin {
	GUINGPlugin(StudioApp& app) : m_app(app) {}

	const char* getName() const override { return "gui_ng"; }

	void init() override {
		// TODO: Initialize GUI NG editor plugin
	}

	bool showGizmo(struct WorldView& view, ComponentUID cmp) override {
		// TODO: Implement gizmos if needed
		return false;
	}

	~GUINGPlugin() {}

private:
	StudioApp& m_app;
};

} // anonymous namespace

LUMIX_STUDIO_ENTRY(gui_ng) {
	return LUMIX_NEW(app.getAllocator(), GUINGPlugin)(app);
}