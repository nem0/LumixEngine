#pragma once


#include "engine/array.h"
#include "editor/studio_app.h"
#include "editor/utils.h"


namespace Lumix {


struct LUMIX_EDITOR_API PropertyGrid : StudioApp::GUIPlugin {
	friend struct GridUIVisitor;
	struct IPlugin {
		virtual ~IPlugin() {}
		virtual void update() {}
		virtual void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, WorldEditor& editor) = 0;
	};

	explicit PropertyGrid(StudioApp& app);
	~PropertyGrid();

	void addPlugin(IPlugin& plugin) { m_plugins.push(&plugin); }
	void removePlugin(IPlugin& plugin) { m_plugins.eraseItem(&plugin); }

private:
	void onWindowGUI() override;
	void onSettingsLoaded() override;
	void onBeforeSettingsSaved() override;
	const char* getName() const override { return "property_grid"; }
	void showComponentProperties(const Array<EntityRef>& entities, ComponentType cmp_type, WorldEditor& editor);
	void showCoreProperties(const Array<EntityRef>& entities, WorldEditor& editor) const;
	void toggleUI() { m_is_open = !m_is_open; }
	bool isOpen() const { return m_is_open; }

	StudioApp& m_app;
	Array<IPlugin*> m_plugins;
	EntityPtr m_deferred_select;
	
	bool m_is_open = false;
	char m_component_filter[32];
	Action m_toggle_ui;
};


} // namespace Lumix