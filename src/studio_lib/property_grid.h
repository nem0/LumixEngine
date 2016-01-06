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


class LUMIX_STUDIO_LIB_API PropertyGrid
{
public:
	struct Plugin
	{
		virtual void onGUI(PropertyGrid& grid, Lumix::ComponentUID cmp) = 0;
	};

public:
	PropertyGrid(Lumix::WorldEditor& editor,
		AssetBrowser& asset_browser,
		Lumix::Array<Action*>& actions);
	~PropertyGrid();

	void addPlugin(Plugin& plugin) { m_plugins.push(&plugin); }
	void removePlugin(Plugin& plugin) { m_plugins.eraseItem(&plugin); }
	void onGUI();
	bool entityInput(const char* label, const char* str_id, Lumix::Entity& entity) const;

public:
	bool m_is_opened;

private:
	void onParticleEmitterGUI(Lumix::ComponentUID cmp);
	void onAmbientSoundGUI(Lumix::ComponentUID cmp);
	void onLuaScriptGUI(Lumix::ComponentUID cmp);
	void showProperty(Lumix::IPropertyDescriptor& desc, int index, Lumix::ComponentUID cmp);
	void showArrayProperty(Lumix::ComponentUID cmp, Lumix::IArrayDescriptor& desc);
	void showSampledFunctionProperty(Lumix::ComponentUID cmp, Lumix::ISampledFunctionDescriptor& desc);
	void showEnumProperty(Lumix::ComponentUID cmp, int index, Lumix::IEnumPropertyDescriptor& desc);
	void showComponentProperties(Lumix::ComponentUID cmp);
	void showCoreProperties(Lumix::Entity entity);
	const char* getComponentTypeName(Lumix::ComponentUID cmp) const;

private:
	Lumix::WorldEditor& m_editor;
	AssetBrowser& m_asset_browser;
	Lumix::Array<Plugin*> m_plugins;
	class TerrainEditor* m_terrain_editor;
	char m_filter[128];

	float m_particle_emitter_timescale;
	bool m_particle_emitter_updating;
};