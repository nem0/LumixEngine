#include "hierarchy_ui.h"
#include "core/crc32.h"
#include "core/json_serializer.h"
#include "editor/ieditor_command.h"
#include "editor/world_editor.h"
#include "ocornut-imgui/imgui.h"
#include "universe/hierarchy.h"
#include "utils.h"


class SetParentEditorCommand : public Lumix::IEditorCommand
{
public:
	SetParentEditorCommand(Lumix::WorldEditor& editor,
		Lumix::Hierarchy& hierarchy,
		Lumix::Entity child,
		Lumix::Entity parent)
		: m_new_parent(parent)
		, m_child(child)
		, m_old_parent(hierarchy.getParent(child))
		, m_hierarchy(hierarchy)
		, m_editor(editor)
	{
	}


	virtual void serialize(Lumix::JsonSerializer& serializer)
	{
		serializer.serialize("parent", m_new_parent);
		serializer.serialize("child", m_child);
	}


	virtual void deserialize(Lumix::JsonSerializer& serializer)
	{
		serializer.deserialize("parent", m_new_parent, 0);
		serializer.deserialize("child", m_child, 0);
		m_old_parent = m_hierarchy.getParent(m_child);
	}


	virtual bool execute() override
	{
		m_hierarchy.setParent(m_child, m_new_parent);
		return true;
	}


	virtual void undo() override
	{
		m_hierarchy.setParent(m_child, m_old_parent);
	}


	virtual bool merge(IEditorCommand&) override { return false; }


	virtual Lumix::uint32 getType() override
	{
		static const Lumix::uint32 hash = Lumix::crc32("set_entity_parent");
		return hash;
	}


private:
	Lumix::Entity m_child;
	Lumix::Entity m_new_parent;
	Lumix::Entity m_old_parent;
	Lumix::Hierarchy& m_hierarchy;
	Lumix::WorldEditor& m_editor;
};


void HierarchyUI::onGUI()
{
	if (!m_is_opened) return;

	if (!ImGui::Begin("Hierarchy", &m_is_opened))
	{
		ImGui::End();
		return;
	}

	auto* hierarchy = m_editor->getHierarchy();

	if (m_editor->getSelectedEntities().size() == 2)
	{
		if (ImGui::Button("Connect selected entities"))
		{
			auto* command =
				m_editor->getAllocator().newObject<SetParentEditorCommand>(*m_editor,
				*hierarchy,
				m_editor->getSelectedEntities()[0],
				m_editor->getSelectedEntities()[1]);
			m_editor->executeCommand(command);
		}
	}
	else
	{
		ImGui::Text("Select two entities to connect them");
	}

	ImGui::Separator();

	if (ImGui::BeginChild("hierarchy_view"))
	{
		const auto& all_children = hierarchy->getAllChildren();
		for (auto i = all_children.begin(), e = all_children.end(); i != e; ++i)
		{
			if ((*i.value()).empty()) continue;
			if (hierarchy->getParent(i.key()) < 0) showHierarchy(i.key(), false);
		}
	}
	ImGui::EndChild();

	ImGui::End();
}



void HierarchyUI::showHierarchy(Lumix::Entity entity, bool has_parent)
{
	auto* hierarchy = m_editor->getHierarchy();
	char name[50];
	getEntityListDisplayName(*m_editor, name, sizeof(name), entity);
	ImGui::BulletText(name);
	if (has_parent)
	{
		ImGui::SameLine();

		if (ImGui::Button(StringBuilder<50>("Remove##r") << entity))
		{
			auto* command = m_editor->getAllocator().newObject<SetParentEditorCommand>(
				*m_editor, *hierarchy, entity, -1);
			m_editor->executeCommand(command);
		}
	}
	auto* children = hierarchy->getChildren(entity);

	if (!children || children->empty()) return;

	ImGui::Indent();
	for (auto c : *children)
	{
		showHierarchy(c.m_entity, true);
	}
	ImGui::Unindent();
}