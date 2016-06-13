#pragma once


#include "engine/array.h"
#include "engine/hash_map.h"
#include "engine/universe/component.h"


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
	struct IAddComponentPlugin
	{
		virtual ~IAddComponentPlugin() {}
		virtual void onGUI() = 0;
		virtual const char* getLabel() const = 0;
	};

	struct IPlugin
	{
		virtual ~IPlugin() {}
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
	void registerComponent(const char* id, const char* label);
	void registerComponent(const char* id, const char* label, IAddComponentPlugin& plugin);
	void registerComponentWithResource(const char* id,
		const char* label,
		Lumix::uint32 resource_type,
		const char* property_name);

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
	void addPlugin(IAddComponentPlugin& plugin);

private:
	Lumix::WorldEditor& m_editor;
	AssetBrowser& m_asset_browser;
	Lumix::Array<IPlugin*> m_plugins;
	Lumix::Array<IAddComponentPlugin*> m_add_cmp_plugins;
	char m_component_filter[32];
	Lumix::HashMap<Lumix::uint32, Lumix::string> m_component_labels;

	float m_particle_emitter_timescale;
	bool m_particle_emitter_updating;
};