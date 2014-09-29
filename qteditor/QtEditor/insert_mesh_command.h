#pragma once


#include "core/path.h"
#include "editor/ieditor_command.h"
#include "editor/world_editor.h"


class InsertMeshCommand : public Lumix::IEditorCommand
{
	public:
		InsertMeshCommand(Lumix::WorldEditor& editor, const Lumix::Vec3& position, const Lumix::Path& mesh_path);

		virtual void execute() override;
		virtual void undo() override;
		virtual uint32_t getType() override;
		virtual bool merge(IEditorCommand&);
		const Lumix::Entity& getEntity() const { return m_entity; }

	private:
		Lumix::Vec3 m_position;
		Lumix::Path m_mesh_path;
		Lumix::Entity m_entity;
		Lumix::WorldEditor& m_editor;
};