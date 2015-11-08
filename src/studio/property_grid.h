#pragma once


#include "core/array.h"
#include "universe/component.h"


namespace Lumix
{
	class IArrayDescriptor;
	class IPropertyDescriptor;
	class WorldEditor;
}


struct Action;
class AssetBrowser;


class PropertyGrid
{
public:
	PropertyGrid(Lumix::WorldEditor& editor,
		AssetBrowser& asset_browser,
		Lumix::Array<Action*>& actions);
	~PropertyGrid();

	void onGUI();

public:
	bool m_is_opened;

private:
	void onLuaScriptGui(Lumix::ComponentUID cmp);
	void showProperty(Lumix::IPropertyDescriptor& desc, int index, Lumix::ComponentUID cmp);
	void showArrayProperty(Lumix::ComponentUID cmp, Lumix::IArrayDescriptor& desc);
	void showComponentProperties(Lumix::ComponentUID cmp);
	void showCoreProperties(Lumix::Entity entity);
	const char* getComponentTypeName(Lumix::ComponentUID cmp) const;
	bool entityInput(const char* label, const char* str_id, Lumix::Entity& entity) const;

private:
	Lumix::WorldEditor& m_editor;
	AssetBrowser& m_asset_browser;
	class TerrainEditor* m_terrain_editor;
	char m_filter[128];
};