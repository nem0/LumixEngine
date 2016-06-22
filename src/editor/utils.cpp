#include "utils.h"
#include "engine/crc32.h"
#include "engine/math_utils.h"
#include "engine/path.h"
#include "engine/path_utils.h"
#include "engine/property_register.h"
#include "editor/render_interface.h"
#include "editor/world_editor.h"
#include "imgui/imgui.h"
#include "engine/universe/universe.h"


void getEntityListDisplayName(Lumix::WorldEditor& editor, char* buf, int max_size, Lumix::Entity entity)
{
	const char* name = editor.getUniverse()->getEntityName(entity);
	static const auto RENDERABLE_TYPE = Lumix::PropertyRegister::getComponentType("renderable");
	Lumix::ComponentUID renderable = editor.getComponent(entity, RENDERABLE_TYPE);
	if (renderable.isValid())
	{
		auto* render_interface = editor.getRenderInterface();
		auto path = render_interface->getRenderablePath(renderable.index);
		if (path.isValid())
		{
			char basename[Lumix::MAX_PATH_LENGTH];
			Lumix::copyString(buf, max_size, path.c_str());
			Lumix::PathUtils::getBasename(basename, Lumix::MAX_PATH_LENGTH, path.c_str());
			if (name && name[0] != '\0')
				Lumix::copyString(buf, max_size, name);
			else
				Lumix::toCString(entity.index, buf, max_size);

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
		Lumix::toCString(entity.index, buf, max_size);
	}
}

