#include "editor_icon.h"
#include "core/crc32.h"
#include "core/math_utils.h"
#include "core/matrix.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "engine.h"
#include "render_interface.h"
#include "universe/universe.h"
#include "world_editor.h"
#include <cmath>


namespace Lumix
{


enum class IconType
{
	PHYSICAL_CONTROLLER,
	PHYSICAL_BOX,
	CAMERA,
	LIGHT,
	TERRAIN,
	ENTITY,

	COUNT
};


const char* ICONS[(int)IconType::COUNT] =
{
	"models/editor/phy_controller_icon.msh",
	"models/editor/phy_box_icon.msh",
	"models/editor/camera_icon.msh",
	"models/editor/directional_light_icon.msh",
	"models/editor/terrain_icon.msh",
	"models/editor/icon.msh"
};


struct EditorIconsImpl : public EditorIcons
{
	EditorIconsImpl(WorldEditor& editor)
		: m_editor(editor)
		, m_icons(editor.getAllocator())
	{
		m_icons.reserve(200);
		for(int i = 0; i < lengthOf(ICONS); ++i)
		{
			m_models[i] = editor.getRenderInterface()->loadModel(Path(ICONS[i]));
		}
		editor.universeDestroyed().bind<EditorIconsImpl, &EditorIconsImpl::clear>(this);
		editor.universeCreated().bind<EditorIconsImpl, &EditorIconsImpl::onUniverseCreated>(this);
		if (m_editor.getUniverse()) onUniverseCreated();
	}


	~EditorIconsImpl()
	{
		m_editor.universeDestroyed().unbind<EditorIconsImpl, &EditorIconsImpl::clear>(this);
		m_editor.universeCreated().unbind<EditorIconsImpl, &EditorIconsImpl::onUniverseCreated>(this);
		for(auto& model : m_models)
		{
			m_editor.getRenderInterface()->unloadModel(model);
		}

		if(m_editor.getUniverse())
		{
			auto& universe = *m_editor.getUniverse();
			universe.entityCreated().unbind<EditorIconsImpl, &EditorIconsImpl::onEntityCreated>(this);
			universe.entityDestroyed().unbind<EditorIconsImpl, &EditorIconsImpl::destroyIcon>(this);
			universe.componentAdded().unbind<EditorIconsImpl, &EditorIconsImpl::refreshIcon>(this);
			universe.componentDestroyed().unbind<EditorIconsImpl, &EditorIconsImpl::refreshIcon>(this);
		}
	}


	void onUniverseCreated()
	{
		auto& universe = *m_editor.getUniverse();
		universe.entityCreated().bind<EditorIconsImpl, &EditorIconsImpl::onEntityCreated>(this);
		universe.entityDestroyed().bind<EditorIconsImpl, &EditorIconsImpl::destroyIcon>(this);
		universe.componentAdded().bind<EditorIconsImpl, &EditorIconsImpl::refreshIcon>(this);
		universe.componentDestroyed().bind<EditorIconsImpl, &EditorIconsImpl::refreshIcon>(this);
	}


	void onEntityCreated(Entity entity)
	{
		createIcon(entity);
	}


	void destroyIcon(Entity entity)
	{
		for(int i = 0, c = m_icons.size(); i < c; ++i)
		{
			if(m_icons[i].entity == entity)
			{
				m_icons.eraseFast(i);
				return;
			}
		}
	}


	void refreshIcon(const ComponentUID& cmp)
	{
		destroyIcon(cmp.entity);
		createIcon(cmp.entity);
	}


	void createIcon(Entity entity)
	{
		if (entity == 0) return;
		if (m_editor.getEditCamera().entity == entity) return;

		static const uint32 RENDERABLE_HASH = crc32("renderable");
		static const uint32 RENDERER_HASH = crc32("renderer");
		static const uint32 PHYSICAL_CONTROLLER_HASH = crc32("physical_controller");
		static const uint32 BOX_RIGID_ACTOR_HASH = crc32("box_rigid_actor");
		static const uint32 CAMERA_HASH = crc32("camera");
		static const uint32 GLOBAL_LIGHT_HASH = crc32("global_light");
		static const uint32 POINT_LIGHT_HASH = crc32("point_light");
		static const uint32 TERRAIN_HASH = crc32("terrain");

		const WorldEditor::ComponentList& cmps = m_editor.getComponents(entity);

		for(auto& cmp : cmps)
		{
			if (cmp.type == RENDERABLE_HASH) return;
		}

		auto& icon = m_icons.emplace();
		icon.entity = entity;
		icon.type = IconType::ENTITY;
		for(auto& cmp : cmps)
		{
			if(cmp.type == PHYSICAL_CONTROLLER_HASH)
			{
				icon.type = IconType::PHYSICAL_CONTROLLER;
				break;
			}
			if(cmp.type == BOX_RIGID_ACTOR_HASH)
			{
				icon.type = IconType::PHYSICAL_BOX;
				break;
			}
			if(cmp.type == CAMERA_HASH)
			{
				icon.type = IconType::CAMERA;
				break;
			}
			if(cmp.type == GLOBAL_LIGHT_HASH || cmp.type == POINT_LIGHT_HASH)
			{
				icon.type = IconType::LIGHT;
				break;
			}
			if(cmp.type == TERRAIN_HASH)
			{
				icon.type = IconType::TERRAIN;
				break;
			}
		}
	}


	void clear() override
	{
		m_icons.clear();
	}


	Hit raycast(const Vec3& origin, const Vec3& dir) override
	{
		Hit hit;
		hit.t = -1;
		hit.entity = INVALID_ENTITY;

		auto* render_interface = m_editor.getRenderInterface();
		if(!render_interface) return hit;

		const auto& universe = *m_editor.getUniverse();
		ComponentIndex camera = m_editor.getEditCamera().index;
		if(camera < 0) return hit;
		Matrix mtx = universe.getMatrix(m_editor.getEditCamera().entity);
		Vec3 camera_pos = mtx.getTranslation();
		float fov = m_editor.getRenderInterface()->getCameraFOV(camera);

		for(auto& icon : m_icons)
		{
			Vec3 position = universe.getPosition(icon.entity);
			float distance = (position - camera_pos).length();

			mtx.setTranslation(position);
			Matrix tmp = mtx;
			tmp.multiply3x3(icon.scale > 0 ? icon.scale : 1);

			float t = m_editor.getRenderInterface()->castRay(m_models[(int)icon.type], origin, dir, tmp);
			if(t >= 0 && (t < hit.t || hit.t < 0))
			{
				hit.t = t;
				hit.entity = icon.entity;
			}
		}

		return hit;
	}


	void render() override
	{
		static const float MIN_SCALE_FACTOR = 10;
		static const float MAX_SCALE_FACTOR = 60;

		auto* render_interface = m_editor.getRenderInterface();
		if(!render_interface) return;

		const auto& universe = *m_editor.getUniverse();
		ComponentIndex camera = m_editor.getEditCamera().index;
		if(camera < 0) return;
		Matrix mtx = universe.getMatrix(m_editor.getEditCamera().entity);
		Vec3 camera_pos = mtx.getTranslation();
		float fov = m_editor.getRenderInterface()->getCameraFOV(camera);

		for(auto& icon : m_icons)
		{
			Vec3 position = universe.getPosition(icon.entity);
			float distance = (position - camera_pos).length();
			float scaleFactor = MIN_SCALE_FACTOR + distance;
			scaleFactor = Math::clamp(scaleFactor, MIN_SCALE_FACTOR, MAX_SCALE_FACTOR);

			icon.scale = tan(Math::degreesToRadians(fov) * 0.5f) * distance / scaleFactor;
			mtx.setTranslation(position);
			Matrix scaled_mtx = mtx;
			scaled_mtx.multiply3x3(icon.scale > 0 ? icon.scale : 1);

			render_interface->renderModel(m_models[(int)icon.type], scaled_mtx);
		}
	}

	struct Icon
	{
		Entity entity;
		IconType type;
		float scale;
	};

	Array<Icon> m_icons;
	RenderInterface::ModelHandle m_models[(int)IconType::COUNT];
	WorldEditor& m_editor;
};


EditorIcons* EditorIcons::create(WorldEditor& editor)
{
	return LUMIX_NEW(editor.getAllocator(), EditorIconsImpl)(editor);
}


void EditorIcons::destroy(EditorIcons& icons)
{
	auto& i = static_cast<EditorIconsImpl&>(icons);
	LUMIX_DELETE(i.m_editor.getAllocator(), &icons);
}


} // !namespace Lumix
