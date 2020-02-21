#include "editor_icon.h"
#include "engine/crt.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/universe.h"
#include "render_interface.h"
#include "world_editor.h"


namespace Lumix
{


static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("model_instance");
static const ComponentType PHYSICAL_CONTROLLER_TYPE = Reflection::getComponentType("physical_controller");
static const ComponentType CAMERA_TYPE = Reflection::getComponentType("camera");
static const ComponentType ENVIRONMENT_TYPE = Reflection::getComponentType("environment");
static const ComponentType POINT_LIGHT_TYPE = Reflection::getComponentType("point_light");
static const ComponentType TERRAIN_TYPE = Reflection::getComponentType("terrain");


enum class IconType
{
	PHYSICAL_CONTROLLER,
	CAMERA,
	LIGHT,
	TERRAIN,
	ENTITY,

	COUNT
};


const char* ICONS[(int)IconType::COUNT] =
{
	"phy_controller_icon",
	"camera_icon",
	"directional_light_icon",
	"terrain_icon",
	"icon"
};


static const float ORTHO_SIZE_SCALE = 1 / 20.0f;


struct EditorIconsImpl final : EditorIcons
{
	struct Icon
	{
		EntityRef entity;
		IconType type;
		float scale;
	};


	explicit EditorIconsImpl(WorldEditor& editor)
		: m_editor(editor)
		, m_icons(editor.getAllocator())
	{
		m_render_interface = nullptr;
		m_icons.reserve(200);
		editor.universeDestroyed().bind<&EditorIconsImpl::clear>(this);
		editor.universeCreated().bind<&EditorIconsImpl::onUniverseCreated>(this);
		if (m_editor.getUniverse()) onUniverseCreated();
	}


	~EditorIconsImpl()
	{
		m_editor.universeDestroyed().unbind<&EditorIconsImpl::clear>(this);
		m_editor.universeCreated().unbind<&EditorIconsImpl::onUniverseCreated>(this);
		setRenderInterface(nullptr);

		if(m_editor.getUniverse())
		{
			auto& universe = *m_editor.getUniverse();
			universe.entityDestroyed().unbind<&EditorIconsImpl::destroyIcon>(this);
			universe.componentAdded().unbind<&EditorIconsImpl::refreshIcon>(this);
			universe.componentDestroyed().unbind<&EditorIconsImpl::refreshIcon>(this);
		}
	}


	void onUniverseCreated()
	{
		auto& universe = *m_editor.getUniverse();
		universe.entityDestroyed().bind<&EditorIconsImpl::destroyIcon>(this);
		universe.componentAdded().bind<&EditorIconsImpl::refreshIcon>(this);
		universe.componentDestroyed().bind<&EditorIconsImpl::refreshIcon>(this);
	}


	void destroyIcon(EntityRef entity)
	{
		for(int i = 0, c = m_icons.size(); i < c; ++i)
		{
			if(m_icons[i].entity == entity)
			{
				m_icons.swapAndPop(i);
				return;
			}
		}
	}


	void refreshIcon(const ComponentUID& cmp)
	{
		ASSERT(cmp.isValid());
		destroyIcon((EntityRef)cmp.entity);
		createIcon((EntityRef)cmp.entity);
	}


	void createIcon(EntityRef entity)
	{
		Universe& universe = *m_editor.getUniverse();
		
		if (universe.getComponent(entity, MODEL_INSTANCE_TYPE).isValid()) return;

		auto& icon = m_icons.emplace();
		icon.entity = entity;
		icon.type = IconType::ENTITY;
		for (ComponentUID cmp = universe.getFirstComponent(entity); cmp.isValid(); cmp = universe.getNextComponent(cmp))
		{
			if(cmp.type == PHYSICAL_CONTROLLER_TYPE)
			{
				icon.type = IconType::PHYSICAL_CONTROLLER;
				break;
			}
			if(cmp.type == CAMERA_TYPE)
			{
				icon.type = IconType::CAMERA;
				break;
			}
			if(cmp.type == ENVIRONMENT_TYPE || cmp.type == POINT_LIGHT_TYPE)
			{
				icon.type = IconType::LIGHT;
				break;
			}
			if(cmp.type == TERRAIN_TYPE)
			{
				icon.type = IconType::TERRAIN;
				break;
			}
		}
	}



	void refresh() override
	{
		clear();
		auto& universe = *m_editor.getUniverse();
		for (EntityPtr entity = universe.getFirstEntity(); entity.isValid(); entity = universe.getNextEntity((EntityRef)entity)) {
			if (universe.getFirstComponent((EntityRef)entity).isValid()) {
				createIcon((EntityRef)entity);
			}
		}
	}


	void clear() override
	{
		m_icons.clear();
	}


	Hit raycast(const DVec3& origin, const Vec3& dir) override
	{
		Hit hit;
		hit.t = -1;
		hit.entity = INVALID_ENTITY;

		const Viewport& vp = m_editor.getView().getViewport();

		for(auto& icon : m_icons) {
			const Transform icon_tr = getIconTransform(icon, vp.rot, vp.is_ortho, vp.ortho_size);
			
			const Vec3 rel_origin = icon_tr.rot.conjugated() * (origin - icon_tr.pos).toFloat();
			const Vec3 rel_dir = icon_tr.rot.conjugated() * dir;
			RenderInterface* ri = m_editor.getRenderInterface();
			const float t = ri->castRay(m_models[(int)icon.type], rel_origin, rel_dir, nullptr);
			if (t >= 0 && (t < hit.t || hit.t < 0)) {
				hit.t = t;
				hit.entity = icon.entity;
			}
		}

		return hit;
	}

	void setRenderInterface(RenderInterface* render_interface) override
	{
		if (m_render_interface)
		{
			for (auto& model : m_models)
			{
				m_render_interface->unloadModel(model);
			}
		}
		m_render_interface = render_interface;
		FileSystem& fs = m_editor.getEngine().getFileSystem();
		if (m_render_interface)
		{
			for (u32 i = 0; i < lengthOf(ICONS); ++i)
			{
				StaticString<MAX_PATH_LENGTH> tmp("editor/models/", ICONS[i], "_3d.fbx");
				m_is_3d[i] = fs.fileExists(tmp);
				if (m_is_3d[i])
				{
					Path path(tmp);
					m_models[i] = m_render_interface->loadModel(path);
				}
				else
				{
					tmp.data[0] = '\0';
					tmp << "editor/models/" << ICONS[i] << ".fbx";
					Path path(tmp);
					m_models[i] = m_render_interface->loadModel(path);
				}
			}
		}
	}


	Transform getIconTransform(const Icon& icon, const Quat& camera_rot, bool is_ortho, float ortho_size)
	{
		Transform ret = m_editor.getUniverse()->getTransform(icon.entity);
		if (!m_is_3d[(int)icon.type]) {
			ret.rot = camera_rot;
		}
		if (is_ortho) {
			ret.scale = ortho_size * ORTHO_SIZE_SCALE;
		}
		else {
			ret.scale = icon.scale > 0 ? icon.scale : 1;
		}
		return ret;
	}


	Matrix getIconMatrix(const Icon& icon, const Matrix& camera_matrix, const DVec3& vp_pos, bool is_ortho, float ortho_size) const
	{
		Matrix ret;
		if (m_is_3d[(int)icon.type])
		{
			ret = m_editor.getUniverse()->getRelativeMatrix(icon.entity, vp_pos);
		}
		else
		{
			ret = camera_matrix;
			ret.setTranslation((m_editor.getUniverse()->getPosition(icon.entity) - vp_pos).toFloat());
		}
		if (is_ortho)
		{
			ret.multiply3x3(ortho_size * ORTHO_SIZE_SCALE);
		}
		else
		{
			ret.multiply3x3(icon.scale > 0 ? icon.scale : 1);
		}
		return ret;
	}


	void getRenderData(Array<RenderData>* data) override
	{
		static const float MIN_SCALE_FACTOR = 10;
		static const float MAX_SCALE_FACTOR = 60;

		const Universe& universe = *m_editor.getUniverse();
		const Viewport& vp = m_editor.getView().getViewport();
		Matrix camera_mtx({0, 0, 0}, vp.rot);

		for(auto& icon : m_icons) {
			const DVec3 position = universe.getPosition(icon.entity);
			const float distance = (position - vp.pos).toFloat().length();
			float scale_factor = MIN_SCALE_FACTOR + distance;
			scale_factor = clamp(scale_factor, MIN_SCALE_FACTOR, MAX_SCALE_FACTOR);
			icon.scale = tanf(vp.fov * 0.5f) * distance / scale_factor;
			
			Matrix icon_mtx = getIconMatrix(icon, camera_mtx, vp.pos, vp.is_ortho, vp.ortho_size);
			data->push({icon_mtx, m_models[(int)icon.type]});
		}
	}

	Array<Icon> m_icons;
	RenderInterface::ModelHandle m_models[(int)IconType::COUNT];
	bool m_is_3d[(int)IconType::COUNT];
	WorldEditor& m_editor;
	RenderInterface* m_render_interface;
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


} // namespace Lumix
