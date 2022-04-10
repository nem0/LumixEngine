#pragma once


#include "engine/array.h"


namespace Lumix
{


struct ArrayDescriptorBase;
struct PropertyDescriptorBase;
struct WorldEditor;
struct StudioApp;


struct LUMIX_EDITOR_API PropertyGrid
{
friend struct GridUIVisitor;
public:
	struct IPlugin
	{
		virtual ~IPlugin() {}
		virtual void update() {}
		virtual void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, WorldEditor& editor) = 0;
	};

public:
	explicit PropertyGrid(StudioApp& app);
	~PropertyGrid();

	void addPlugin(IPlugin& plugin) { m_plugins.push(&plugin); }
	void removePlugin(IPlugin& plugin) { m_plugins.eraseItem(&plugin); }
	void onGUI();

public:
	bool m_is_open;

private:
	void showComponentProperties(const Array<EntityRef>& entities, ComponentType cmp_type, WorldEditor& editor);
	void showCoreProperties(const Array<EntityRef>& entities, WorldEditor& editor) const;

private:
	StudioApp& m_app;
	Array<IPlugin*> m_plugins;
	EntityPtr m_deferred_select;
	
	char m_component_filter[32];
};


} // namespace Lumix