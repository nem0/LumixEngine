#pragma once


#include "engine/array.h"
#include "engine/universe/component.h"


namespace Lumix
{


class ArrayDescriptorBase;
struct IEnumPropertyDescriptor;
class PropertyDescriptorBase;
struct ISampledFunctionDescriptor;
class WorldEditor;
class StudioApp;


class LUMIX_EDITOR_API PropertyGrid
{
public:
	struct IPlugin
	{
		virtual ~IPlugin() {}
		virtual void onGUI(PropertyGrid& grid, ComponentUID cmp) = 0;
	};

public:
	PropertyGrid(StudioApp& app);
	~PropertyGrid();

	void addPlugin(IPlugin& plugin) { m_plugins.push(&plugin); }
	void removePlugin(IPlugin& plugin) { m_plugins.eraseItem(&plugin); }
	void onGUI();
	bool entityInput(const char* label, const char* str_id, Entity& entity) const;

public:
	bool m_is_opened;

private:
	void showProperty(PropertyDescriptorBase& desc,
		int index,
		const Array<Entity>& entities,
		ComponentType cmp_type);
	void showArrayProperty(const Array<Entity>& entities,
		ComponentType cmp_type,
		ArrayDescriptorBase& desc);
	void showSampledFunctionProperty(const Array<Entity>& entities,
		ComponentType cmp_type,
		ISampledFunctionDescriptor& desc);
	void showEnumProperty(const Array<Entity>& entities,
		ComponentType cmp_type,
		int index,
		IEnumPropertyDescriptor& desc);
	void showEntityProperty(const Array<Entity>& entities,
		ComponentType cmp_type,
		int index,
		PropertyDescriptorBase& desc);
	void showComponentProperties(const Array<Entity>& entities, ComponentType cmp_type);
	void showCoreProperties(const Array<Entity>& entities);

private:
	StudioApp& m_app;
	WorldEditor& m_editor;
	Array<IPlugin*> m_plugins;
	
	char m_component_filter[32];
	char m_entity_filter[32];
	Entity m_deferred_select = INVALID_ENTITY;

	float m_particle_emitter_timescale;
	bool m_particle_emitter_updating;
};


} // namespace Lumix