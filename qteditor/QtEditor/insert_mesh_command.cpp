#include "insert_mesh_command.h"
#include "core/crc32.h"
#include "engine/engine.h"
#include "graphics/render_scene.h"


static const uint32_t RENDERABLE_HASH = crc32("renderable");


InsertMeshCommand::InsertMeshCommand(Lumix::WorldEditor& editor, const Lumix::Vec3& position, const Lumix::Path& mesh_path)
	: m_mesh_path(mesh_path)
	, m_position(position)
	, m_editor(editor)
{

}


void InsertMeshCommand::execute()
{
	Lumix::Engine& engine = m_editor.getEngine();
	m_entity = engine.getUniverse()->createEntity();
	m_entity.setPosition(m_position);
	const Lumix::Array<Lumix::IScene*>& scenes = engine.getScenes();
	Lumix::Component cmp;
	Lumix::IScene* scene = NULL;
	for (int i = 0; i < scenes.size(); ++i)
	{
		cmp = scenes[i]->createComponent(RENDERABLE_HASH, m_entity);
		if (cmp.isValid())
		{
			scene = scenes[i];
			break;
		}
	}
	if (cmp.isValid())
	{
		char rel_path[LUMIX_MAX_PATH];
		m_editor.getRelativePath(rel_path, LUMIX_MAX_PATH, m_mesh_path.c_str());
		static_cast<Lumix::RenderScene*>(scene)->setRenderablePath(cmp, Lumix::string(rel_path));
	}
}


void InsertMeshCommand::undo()
{
	const Lumix::Entity::ComponentList& cmps = m_entity.getComponents();
	for (int i = 0; i < cmps.size(); ++i)
	{
		cmps[i].scene->destroyComponent(cmps[i]);
	}
	m_editor.getEngine().getUniverse()->destroyEntity(m_entity);
	m_entity = Lumix::Entity::INVALID;
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

