#pragma once


#include "editor/ieditor_command.h"
#include "engine/path.h"
#include "engine/vec.h"


namespace Lumix
{


class WorldEditor;


struct InsertMeshCommand : public IEditorCommand
{
	Vec3 m_position;
	Path m_mesh_path;
	Entity m_entity;
	WorldEditor& m_editor;

	explicit InsertMeshCommand(WorldEditor& editor);
	InsertMeshCommand(WorldEditor& editor, const Vec3& position, const Path& mesh_path);

	void serialize(JsonSerializer& serializer) override;
	void deserialize(JsonSerializer& serializer) override;
	bool execute() override;
	void undo() override;
	uint32 getType() override;
	bool merge(IEditorCommand&) override;
};


} // namespace Lumix