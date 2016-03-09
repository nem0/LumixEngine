#pragma once


#include "core/array.h"
#include "universe/component.h"


namespace Lumix
{
class IArrayDescriptor;
class IEnumPropertyDescriptor;
class IPropertyDescriptor;
class ISampledFunctionDescriptor;
class WorldEditor;
}


struct Action;
class AssetBrowser;


class LUMIX_EDITOR_API PropertyGrid
{
public:
	struct IPlugin
	{
		virtual void onGUI(PropertyGrid& grid, Lumix::ComponentUID cmp) = 0;
	};

public:
	PropertyGrid(Lumix::WorldEditor& editor,
		AssetBrowser& asset_browser,
		Lumix::Array<Action*>& actions);
	~PropertyGrid();

	void addPlugin(IPlugin& plugin) { m_plugins.push(&plugin); }
	void removePlugin(IPlugin& plugin) { m_plugins.eraseItem(&plugin); }
	void onGUI();
	bool entityInput(const char* label, const char* str_id, Lumix::Entity& entity) const;

public:
	bool m_is_opened;

private:
	void showProperty(Lumix::IPropertyDescriptor& desc, int index, Lumix::ComponentUID cmp);
	void showArrayProperty(Lumix::ComponentUID cmp, Lumix::IArrayDescriptor& desc);
	void showSampledFunctionProperty(Lumix::ComponentUID cmp, Lumix::ISampledFunctionDescriptor& desc);
	void showEnumProperty(Lumix::ComponentUID cmp, int index, Lumix::IEnumPropertyDescriptor& desc);
	void showEntityProperty(Lumix::ComponentUID cmp, int index, Lumix::IPropertyDescriptor& desc);
	void showComponentProperties(Lumix::ComponentUID cmp);
	void showCoreProperties(Lumix::Entity entity);
	const char* getComponentTypeName(Lumix::ComponentUID cmp) const;

private:
	Lumix::WorldEditor& m_editor;
	AssetBrowser& m_asset_browser;
	Lumix::Array<IPlugin*> m_plugins;
	char m_filter[128];

	float m_particle_emitter_timescale;
	bool m_particle_emitter_updating;
};