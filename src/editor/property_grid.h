#pragma once


#include "engine/array.h"


namespace Lumix
{


struct ArrayDescriptorBase;
struct ComponentUID;
struct IEnumPropertyDescriptor;
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
		virtual void onGUI(PropertyGrid& grid, ComponentUID cmp) = 0;
	};

public:
	explicit PropertyGrid(StudioApp& app);
	~PropertyGrid();

	void addPlugin(IPlugin& plugin) { m_plugins.push(&plugin); }
	void removePlugin(IPlugin& plugin) { m_plugins.eraseItem(&plugin); }
	void onGUI();
	bool entityInput(const char* label, const char* str_id, EntityPtr& entity);

public:
	bool m_is_open;

private:
	void showComponentProperties(const Array<EntityRef>& entities, ComponentType cmp_type);
	void showCoreProperties(const Array<EntityRef>& entities) const;

private:
	StudioApp& m_app;
	WorldEditor& m_editor;
	Array<IPlugin*> m_plugins;
	EntityPtr m_deferred_select;
	
	char m_component_filter[32];
};


} // namespace Lumix