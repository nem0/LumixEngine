#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/math_utils.h"
#include "engine/universe/universe.h"
#include "renderer/render_scene.h"


using namespace Lumix;


namespace
{


static const uint32 NAVMESH_AGENT_HASH = crc32("navmesh_agent");


} // anonymous


LUMIX_STUDIO_ENTRY(navigation)
{
	app.registerComponent("navmesh_agent", "Navmesh Agent");
}

