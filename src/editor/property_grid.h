#pragma once


#include "engine/array.h"
#include "engine/universe/component.h"


namespace Lumix
{
	class IArrayDescriptor;
	class IEnumPropertyDescriptor;
	class IPropertyDescriptor;
	class ISampledFunctionDescriptor;
	class WorldEditor;
}


class StudioApp;


class LUMIX_EDITOR_API PropertyGrid
{
public:
	struct IPlugin
	{
		virtual ~IPlugin() {}
		virtual void onGUI(PropertyGrid& grid, Lumix::ComponentUID cmp) = 0;
	};

public:
	PropertyGrid(StudioApp& app);
	~PropertyGrid();

	void addPlugin(IPlugin& plugin) { m_plugins.push(&plugin); }
	void removePlugin(IPlugin& plugin) { m_plugins.eraseItem(&plugin); }
	void onGUI();
	bool entityInput(const char* label, const char* str_id, Lumix::Entity& entity) const;

public:
	bool m_is_opened;

private:
	void showProperty(Lumix::IPropertyDescriptor& desc,
		int index,
		const Lumix::Array<Lumix::Entity>& entities,
		Lumix::ComponentType cmp_type);
	void showArrayProperty(const Lumix::Array<Lumix::Entity>& entities,
		Lumix::ComponentType cmp_type,
		Lumix::IArrayDescriptor& desc);
	void showSampledFunctionProperty(const Lumix::Array<Lumix::Entity>& entities,
		Lumix::ComponentType cmp_type,
		Lumix::ISampledFunctionDescriptor& desc);
	void showEnumProperty(const Lumix::Array<Lumix::Entity>& entities,
		Lumix::ComponentType cmp_type,
		int index,
		Lumix::IEnumPropertyDescriptor& desc);
	void showEntityProperty(const Lumix::Array<Lumix::Entity>& entities,
		Lumix::ComponentType cmp_type,
		int index,
		Lumix::IPropertyDescriptor& desc);
	void showComponentProperties(const Lumix::Array<Lumix::Entity>& entities, Lumix::ComponentType cmp_type);
	void showCoreProperties(const Lumix::Array<Lumix::Entity>& entities);

private:
	StudioApp& m_app;
	Lumix::WorldEditor& m_editor;
	Lumix::Array<IPlugin*> m_plugins;
	
	char m_component_filter[32];

	float m_particle_emitter_timescale;
	bool m_particle_emitter_updating;
};