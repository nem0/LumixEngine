#include "engine/core.h"
#include "engine/reflection.h"
#include "editor/property_grid.h"
#include "editor/world_editor.h"
#include "signal_editor.h"

namespace Lumix {

static const ComponentType SIGNAL_TYPE = reflection::getComponentType("signal");

struct SignalEditorImpl : SignalEditor {
	SignalEditorImpl(StudioApp& app) : m_app(app) {}
	
	~SignalEditorImpl() {
		m_app.getPropertyGrid().removePlugin(*this);
	}

	void init() override {
		m_app.getPropertyGrid().addPlugin(*this);
	}

	const char* getName() const override { return "signal_editor"; }

	bool showGizmo(struct WorldView& view, struct ComponentUID cmp) override { return false; }

	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, const TextFilter& filter, WorldEditor& editor) override {
		if (cmp_type != SIGNAL_TYPE) return;
		if (filter.isActive()) return;
		if (entities.length() != 1) return;

		// TODO undo/redo
		CoreModule* core = (CoreModule*)editor.getWorld()->getModule("core");
		Signal& signal = core->getSignal(entities[0]);

		reflection::Module* module = reflection::getFirstModule();
		ImGuiEx::Label("Event");
		if (ImGui::BeginCombo("##evt", signal.event ? signal.event->name : "Not set")) {
			while (module) {
				for (reflection::EventBase* event : module->events) {
					if (ImGui::Selectable(event->name)) {
						signal.event_module = module;
						signal.event = event;
					}
				}
				module = module->next;
			}
			ImGui::EndCombo();
		}

		module = reflection::getFirstModule();
		ImGuiEx::Label("Function");
		if (ImGui::BeginCombo("##fn", signal.function ? signal.function->name : "Not set")) {
			while (module) {
				for (reflection::FunctionBase* fn : module->functions) {
					StaticString<MAX_PATH> tmp(module->name, "::", fn->name);
					if (ImGui::Selectable(tmp)) {
						signal.function_module = module;
						signal.function = fn;
					}
				}
				module = module->next;
			}
			ImGui::EndCombo();
		}
	}

	StudioApp& m_app;
};

SignalEditor* createSignalEditor(StudioApp& app) {
	return LUMIX_NEW(app.getAllocator(), SignalEditorImpl)(app);
}

}