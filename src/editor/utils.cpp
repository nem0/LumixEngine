#include "utils.h"
#include "engine/math_utils.h"
#include "engine/path.h"
#include "engine/path_utils.h"
#include "engine/property_register.h"
#include "editor/render_interface.h"
#include "editor/world_editor.h"
#include "imgui/imgui.h"
#include "engine/universe/universe.h"


namespace Lumix
{


void getEntityListDisplayName(WorldEditor& editor, char* buf, int max_size, Entity entity)
{
	if (!entity.isValid())
	{
		*buf = '\0';
		return;
	}
	const char* name = editor.getUniverse()->getEntityName(entity);
	static const auto MODEL_INSTANCE_TYPE = PropertyRegister::getComponentType("renderable");
	ComponentHandle model_instance = editor.getUniverse()->getComponent(entity, MODEL_INSTANCE_TYPE).handle;
	if (model_instance.isValid())
	{
		auto* render_interface = editor.getRenderInterface();
		auto path = render_interface->getModelInstancePath(model_instance);
		if (path.isValid())
		{
			char basename[MAX_PATH_LENGTH];
			copyString(buf, max_size, path.c_str());
			PathUtils::getBasename(basename, MAX_PATH_LENGTH, path.c_str());
			if (name && name[0] != '\0')
				copyString(buf, max_size, name);
			else
				toCString(entity.index, buf, max_size);

			catString(buf, max_size, " - ");
			catString(buf, max_size, basename);
			return;
		}
	}

	if (name && name[0] != '\0')
	{
		copyString(buf, max_size, name);
	}
	else
	{
		toCString(entity.index, buf, max_size);
	}
}


} // namespace Lumix