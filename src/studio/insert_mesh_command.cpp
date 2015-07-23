#include "insert_mesh_command.h"
#include "core/crc32.h"
#include "core/json_serializer.h"
#include "engine/engine.h"
#include "graphics/render_scene.h"


static const uint32_t RENDERABLE_HASH = crc32("renderable");


InsertMeshCommand::InsertMeshCommand(Lumix::WorldEditor& editor)
	: m_editor(editor)
{

}


InsertMeshCommand::InsertMeshCommand(Lumix::WorldEditor& editor, const Lumix::Vec3& position, const Lumix::Path& mesh_path)
	: m_mesh_path(mesh_path)
	, m_position(position)
	, m_editor(editor)
{

}


void InsertMeshCommand::serialize(Lumix::JsonSerializer& serializer)
{
	serializer.serialize("path", m_mesh_path.c_str());
	serializer.serialize("pos_x", m_position.x);
	serializer.serialize("pos_y", m_position.y);
	serializer.serialize("pos_z", m_position.z);
}


void InsertMeshCommand::deserialize(Lumix::JsonSerializer& serializer)
{
	char path[LUMIX_MAX_PATH];
	serializer.deserialize("path", path, sizeof(path), "");
	m_mesh_path = path;
	serializer.deserialize("pos_x", m_position.x, 0);
	serializer.deserialize("pos_y", m_position.y, 0);
	serializer.deserialize("pos_z", m_position.z, 0);
}


void InsertMeshCommand::execute()
{
	Lumix::Engine& engine = m_editor.getEngine();
	Lumix::Universe* universe = engine.getUniverse();
	m_entity = universe->createEntity();
	universe->setPosition(m_entity, m_position);
	const Lumix::Array<Lumix::IScene*>& scenes = engine.getScenes();
	Lumix::ComponentOld cmp;
	Lumix::IScene* scene = NULL;
	for (int i = 0; i < scenes.size(); ++i)
	{
		cmp = Lumix::ComponentOld(
			m_entity,
			RENDERABLE_HASH,
			scenes[i],
			scenes[i]->createComponent(RENDERABLE_HASH, m_entity));

		if (cmp.isValid())
		{
			scene = scenes[i];
			break;
		}
	}
	if (cmp.isValid())
	{
		char rel_path[LUMIX_MAX_PATH];
		m_editor.getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(m_mesh_path.c_str()));
		Lumix::StackAllocator<LUMIX_MAX_PATH> allocator;
		static_cast<Lumix::RenderScene*>(scene)->setRenderablePath(cmp, Lumix::string(rel_path, allocator));
	}
}


void InsertMeshCommand::undo()
{
	const Lumix::WorldEditor::ComponentList& cmps = m_editor.getComponents(m_entity);
	for (int i = 0; i < cmps.size(); ++i)
	{
		cmps[i].scene->destroyComponent(cmps[i].index, cmps[i].type);
	}
	m_editor.getUniverse()->destroyEntity(m_entity);
	m_entity = Lumix::NEW_INVALID_ENTITY;
}


uint32_t InsertMeshCommand::getType()
{
	static const uint32_t type = crc32("insert_mesh");
	return type;
}


bool InsertMeshCommand::merge(IEditorCommand&)
{
	return false;
}

