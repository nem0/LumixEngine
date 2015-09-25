#pragma once
#include "lumix.h"


namespace Lumix
{
	class WorldEditor;
}


class HierarchyUI
{
public:
	HierarchyUI() { m_is_opened = false; }
	void onGui();
	void setWorldEditor(Lumix::WorldEditor& editor) { m_editor = &editor; }

public:
	bool m_is_opened;

private:
	void showHierarchy(Lumix::Entity entity, bool has_parent);

private:
	Lumix::WorldEditor* m_editor;
};