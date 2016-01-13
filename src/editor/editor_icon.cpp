#include "editor_icon.h"
#include "core/crc32.h"
#include "core/math_utils.h"
#include "core/matrix.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "engine.h"
#include "render_interface.h"
#include "world_editor.h"
#include <cmath>


namespace Lumix
{


static struct
{
	const char* name;
	RenderInterface::ModelHandle model;
} ICONS[EditorIcon::COUNT] = {
	{ "models/editor/phy_controller_icon.msh", -1 },
	{ "models/editor/phy_box_icon.msh", -1 },
	{ "models/editor/camera_icon.msh", -1 },
	{ "models/editor/directional_light_icon.msh", -1 },
	{ "models/editor/terrain_icon.msh", -1 },
	{ "models/editor/icon.msh", -1 } };


void EditorIcon::loadIcons(WorldEditor& editor)
{
	for (int i = 0; i < lengthOf(ICONS); ++i)
	{
		ICONS[i].model = editor.getRenderInterface()->loadModel(Path(ICONS[i].name));
	}
}


void EditorIcon::unloadIcons(WorldEditor& editor)
{
	for (int i = 0; i < lengthOf(ICONS); ++i)
	{
		editor.getRenderInterface()->unloadModel(ICONS[i].model);
	}
}


EditorIcon::EditorIcon(WorldEditor& editor, RenderScene& scene, Entity entity)
{
	m_scale = 1;
	m_scene = &scene;
	m_entity = entity;
	m_is_visible = true;
	const WorldEditor::ComponentList& cmps = editor.getComponents(entity);
	m_type = ENTITY;
	for (int i = 0; i < cmps.size(); ++i)
	{
		if (cmps[i].type == crc32("physical_controller"))
		{
			m_type = PHYSICAL_CONTROLLER;
			break;
		}
		else if (cmps[i].type == crc32("box_rigid_actor"))
		{
			m_type = PHYSICAL_BOX;
			break;
		}
		else if (cmps[i].type == crc32("camera"))
		{
			m_type = CAMERA;
			break;
		}
		else if (cmps[i].type == crc32("global_light") || cmps[i].type == crc32("point_light"))
		{
			m_type = LIGHT;
			break;
		}
		else if (cmps[i].type == crc32("terrain"))
		{
			m_type = TERRAIN;
			break;
		}
	}
	m_model = ICONS[m_type].model;
}


EditorIcon::~EditorIcon()
{
}


void EditorIcon::show()
{
	m_is_visible = true;
}


void EditorIcon::hide()
{
	m_is_visible = false;
}


float EditorIcon::hit(WorldEditor& editor, const Vec3& origin, const Vec3& dir) const
{
	if (m_is_visible)
	{
		Matrix m = m_matrix;
		m.multiply3x3(m_scale);
		return editor.getRenderInterface()->castRay(m_model, origin, dir, m);
	}
	else
	{
		return -1;
	}
}


void EditorIcon::render(WorldEditor& editor)
{
	static const float MIN_SCALE_FACTOR = 10;
	static const float MAX_SCALE_FACTOR = 60;
	if (m_is_visible)
	{
		ComponentIndex camera = editor.getEditCamera().index;
		if (camera < 0) return;
		const Universe& universe = *editor.getUniverse();
		Lumix::Matrix mtx = universe.getMatrix(editor.getEditCamera().entity);

		float fov = editor.getRenderInterface()->getCameraFOV(camera);
		Vec3 position = universe.getPosition(m_entity);
		float distance = (position - mtx.getTranslation()).length();

		float scaleFactor = MIN_SCALE_FACTOR + distance;
		scaleFactor = Math::clamp(scaleFactor, MIN_SCALE_FACTOR, MAX_SCALE_FACTOR);

		float scale = tan(Math::degreesToRadians(fov) * 0.5f) * distance / scaleFactor;

		mtx.setTranslation(position);
		Matrix scale_mtx = Matrix::IDENTITY;
		m_matrix = mtx;
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale > 0 ? scale : 1;
		mtx = mtx * scale_mtx;
		m_scale = scale;

		editor.getRenderInterface()->renderModel(m_model, mtx);
	}
}


} // !namespace Lumix
