#include "editor_icon.h"
#include "core/crc32.h"
#include "core/math_utils.h"
#include "core/matrix.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "engine/engine.h"
#include "graphics/geometry.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/pipeline.h"
#include "graphics/render_scene.h"
#include "world_editor.h"

namespace Lumix
{


static const char* ICON_NAMES[EditorIcon::COUNT] = {
	"models/editor/phy_controller_icon.msh",
	"models/editor/phy_box_icon.msh",
	"models/editor/camera_icon.msh",
	"models/editor/directional_light_icon.msh",
	"models/editor/terrain_icon.msh",
	"models/editor/icon.msh"
};


static Model* icon_models[EditorIcon::COUNT];


bool EditorIcon::loadIcons(Engine& engine)
{
	bool status = true;
	for (int i = 0; i < sizeof(ICON_NAMES) / sizeof(ICON_NAMES[0]); ++i)
	{
		icon_models[i] = static_cast<Model*>(engine.getResourceManager().get(ResourceManager::MODEL)->load(Path(ICON_NAMES[i])));
		status = status && icon_models[i];
	}
	return status;
}


void EditorIcon::unloadIcons()
{
	for (int i = 0; i < lengthOf(icon_models); ++i)
	{
		icon_models[i]->getResourceManager().get(ResourceManager::MODEL)->unload(*icon_models[i]);
	}
}


EditorIcon::EditorIcon(Engine& engine, RenderScene& scene, const Entity& entity)
{
	m_scale = 1;
	m_scene = &scene;
	m_entity = entity;
	m_is_visible = true;
	const WorldEditor::ComponentList& cmps = engine.getWorldEditor()->getComponents(entity);
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
	m_model = static_cast<Model*>(engine.getResourceManager().get(ResourceManager::MODEL)->load(Path(ICON_NAMES[m_type])));
}


EditorIcon::~EditorIcon()
{
	m_model->getResourceManager().get(ResourceManager::MODEL)->unload(*m_model);
}


void EditorIcon::show()
{
	m_is_visible = true;
}


void EditorIcon::hide()
{
	m_is_visible = false;
}



float EditorIcon::hit(const Vec3& origin, const Vec3& dir) const
{
	if (m_is_visible)
	{
		RayCastModelHit hit = m_model->castRay(origin, dir, m_matrix, m_scale);
		return hit.m_is_hit ? hit.m_t : -1;
	}
	else
	{
		return -1;
	}
}


void EditorIcon::render(PipelineInstance& pipeline)
{
	if (m_is_visible)
	{
		Component camera = m_scene->getCameraInSlot("editor");
		Lumix::Matrix mtx = camera.entity.getMatrix();

		float fov = static_cast<RenderScene*>(camera.scene)->getCameraFOV(camera);
		float scale = tan(fov * Math::PI / 180 * 0.5f) * (m_entity.getPosition() - mtx.getTranslation()).length() / 20;

		mtx.setTranslation(m_entity.getPosition());
		Matrix scale_mtx = Matrix::IDENTITY;
		m_matrix = mtx;
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale > 0 ? scale : 1;
		mtx = mtx * scale_mtx;
		m_scale = scale;

		if (m_model->isReady())
		{
			pipeline.renderModel(*m_model, mtx);
		}
	}
}


} // !namespace Lumix
