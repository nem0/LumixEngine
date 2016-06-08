#include "insert_mesh_command.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/iplugin.h"
#include "engine/json_serializer.h"
#include "engine/universe/universe.h"
#include "renderer/render_scene.h"


namespace Lumix
{


InsertMeshCommand::InsertMeshCommand(WorldEditor& editor)
	: m_editor(editor)
{
}


InsertMeshCommand::InsertMeshCommand(WorldEditor& editor, const Vec3& position, const Path& mesh_path)
	: m_mesh_path(mesh_path)
	, m_position(position)
	, m_editor(editor)
{
}


void InsertMeshCommand::serialize(JsonSerializer& serializer)
{
	serializer.serialize("path", m_mesh_path.c_str());
	serializer.beginArray("pos");
	serializer.serializeArrayItem(m_position.x);
	serializer.serializeArrayItem(m_position.y);
	serializer.serializeArrayItem(m_position.z);
	serializer.endArray();
}


void InsertMeshCommand::deserialize(JsonSerializer& serializer)
{
	char path[MAX_PATH_LENGTH];
	serializer.deserialize("path", path, sizeof(path), "");
	m_mesh_path = path;
	serializer.deserializeArrayBegin("pos");
	serializer.deserializeArrayItem(m_position.x, 0);
	serializer.deserializeArrayItem(m_position.y, 0);
	serializer.deserializeArrayItem(m_position.z, 0);
	serializer.deserializeArrayEnd();
}


bool InsertMeshCommand::execute()
{
	static const uint32 RENDERABLE_HASH = crc32("renderable");

	Universe* universe = m_editor.getUniverse();
	m_entity = universe->createEntity(Vec3(0, 0, 0), Quat(0, 0, 0, 1));
	universe->setPosition(m_entity, m_position);
	const Array<IScene*>& scenes = m_editor.getScenes();
	ComponentIndex cmp = -1;
	IScene* scene = nullptr;
	for (int i = 0; i < scenes.size(); ++i)
	{
		cmp = scenes[i]->createComponent(RENDERABLE_HASH, m_entity);

		if (cmp >= 0)
		{
			scene = scenes[i];
			break;
		}
	}
	if (cmp >= 0) static_cast<RenderScene*>(scene)->setRenderablePath(cmp, m_mesh_path);
	return true;
}


void InsertMeshCommand::undo()
{
	const WorldEditor::ComponentList& cmps = m_editor.getComponents(m_entity);
	for (int i = 0; i < cmps.size(); ++i)
	{
		cmps[i].scene->destroyComponent(cmps[i].index, cmps[i].type);
	}
	m_editor.getUniverse()->destroyEntity(m_entity);
	m_entity = INVALID_ENTITY;
}


uint32 InsertMeshCommand::getType()
{
	static const uint32 TYPE = crc32("insert_mesh");
	return TYPE;
}


bool InsertMeshCommand::merge(IEditorCommand&) { return false; }


} // namespace Lumix