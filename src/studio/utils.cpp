#include "utils.h"
#include "core/crc32.h"
#include "core/path_utils.h"
#include "editor/world_editor.h"
#include "renderer/render_scene.h"
#include "universe/universe.h"


void getEntityListDisplayName(Lumix::WorldEditor& editor,
	char* buf,
	int max_size,
	Lumix::Entity entity)
{
	const char* name = editor.getUniverse()->getEntityName(entity);
	static const uint32_t RENDERABLE_HASH = Lumix::crc32("renderable");
	Lumix::ComponentUID renderable = editor.getComponent(entity, RENDERABLE_HASH);
	if (renderable.isValid())
	{
		auto* scene = static_cast<Lumix::RenderScene*>(renderable.scene);
		const char* path = scene->getRenderablePath(renderable.index);
		if (path && path[0] != 0)
		{
			char basename[Lumix::MAX_PATH_LENGTH];
			Lumix::copyString(buf, max_size, path);
			Lumix::PathUtils::getBasename(basename, Lumix::MAX_PATH_LENGTH, path);
			if (name && name[0] != '\0')
				Lumix::copyString(buf, max_size, name);
			else
				Lumix::toCString(entity, buf, max_size);

			Lumix::catString(buf, max_size, " - ");
			Lumix::catString(buf, max_size, basename);
			return;
		}
	}

	if (name && name[0] != '\0')
	{
		Lumix::copyString(buf, max_size, name);
	}
	else
	{
		Lumix::toCString(entity, buf, max_size);
	}
}