#include "editor_icon.h"
#include "editor/world_editor.h"
#include "engine/crt.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/universe.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"


namespace Lumix
{


static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
static const ComponentType PHYSICAL_CONTROLLER_TYPE = reflection::getComponentType("physical_controller");
static const ComponentType CAMERA_TYPE = reflection::getComponentType("camera");
static const ComponentType ENVIRONMENT_TYPE = reflection::getComponentType("environment");
static const ComponentType POINT_LIGHT_TYPE = reflection::getComponentType("point_light");
static const ComponentType TERRAIN_TYPE = reflection::getComponentType("terrain");


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


	explicit EditorIconsImpl(WorldEditor& editor, RenderScene& scene)
		: m_editor(editor)
		, m_icons(editor.getAllocator())
		, m_scene(scene)
	{
		m_icons.reserve(200);

		auto& universe = scene.getUniverse();
		universe.entityDestroyed().bind<&EditorIconsImpl::destroyIcon>(this);
		universe.componentAdded().bind<&EditorIconsImpl::refreshIcon>(this);
		universe.componentDestroyed().bind<&EditorIconsImpl::refreshIcon>(this);

		Engine& engine = m_editor.getEngine();
		FileSystem& fs = engine.getFileSystem();
		ResourceManagerHub& rm = engine.getResourceManager();
		for (u32 i = 0; i < lengthOf(ICONS); ++i)
		{
			StaticString<LUMIX_MAX_PATH> tmp("editor/models/", ICONS[i], "_3d.fbx");
			m_is_3d[i] = fs.fileExists(tmp);
			if (m_is_3d[i])
			{
				Path path(tmp);
				m_models[i] = rm.load<Model>(path);
			}
			else
			{
				tmp.data[0] = '\0';
				tmp << "editor/models/" << ICONS[i] << ".fbx";
				Path path(tmp);
				m_models[i] = rm.load<Model>(path);
			}
		}
	}


	~EditorIconsImpl()
	{
		for (auto& model : m_models) model->decRefCount();

		auto& universe = m_scene.getUniverse();
		universe.entityDestroyed().bind<&EditorIconsImpl::destroyIcon>(this);
		universe.componentAdded().bind<&EditorIconsImpl::refreshIcon>(this);
		universe.componentDestroyed().bind<&EditorIconsImpl::refreshIcon>(this);
	}


	void destroyIcon(EntityRef entity)
	{
		if (!m_editor.isLoading()) {
			m_icons.erase(entity);
		}
	}

	void refresh() override {
		m_icons.clear();
		Universe& universe = m_scene.getUniverse();
		for (EntityPtr e = universe.getFirstEntity(); e.isValid(); e = universe.getNextEntity((EntityRef)e)) {
			createIcon((EntityRef)e);
		}
	}

	void refreshIcon(const ComponentUID& cmp)
	{
		if (!m_editor.isLoading()) {
			ASSERT(cmp.isValid());
			destroyIcon((EntityRef)cmp.entity);
			createIcon((EntityRef)cmp.entity);
		}
	}


	void createIcon(EntityRef entity)
	{
		Universe& universe = *m_editor.getUniverse();
		
		const u64 mask = universe.getComponentsMask(entity);
		if (mask & ((u64)1 << (u64)MODEL_INSTANCE_TYPE.index)) return;

		auto& icon = m_icons.insert(entity);
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



	Hit raycast(const DVec3& origin, const Vec3& dir) override
	{
		Hit res;
		res.t = -1;
		res.entity = INVALID_ENTITY;

		const Viewport& vp = m_editor.getView().getViewport();

		for(auto& icon : m_icons) {
			const Transform icon_tr = getIconTransform(icon, vp.rot, vp.is_ortho, vp.ortho_size);
			
			const Vec3 rel_origin = icon_tr.rot.conjugated() * Vec3(origin - icon_tr.pos);
			const Vec3 rel_dir = icon_tr.rot.conjugated() * dir;
			const RayCastModelHit hit = m_models[(int)icon.type]->castRay(rel_origin / icon_tr.scale, rel_dir, nullptr);
			if (hit.is_hit && hit.t >= 0 && (hit.t < res.t || res.t < 0)) {
				res.t = hit.t;
				res.entity = icon.entity;
			}
		}

		return res;
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
			ret.setTranslation(Vec3(m_editor.getUniverse()->getPosition(icon.entity) - vp_pos));
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
			const float distance = (float)length(position - vp.pos);
			float scale_factor = MIN_SCALE_FACTOR + distance;
			scale_factor = clamp(scale_factor, MIN_SCALE_FACTOR, MAX_SCALE_FACTOR);
			icon.scale = tanf(vp.fov * 0.5f) * distance / scale_factor;
			
			Matrix icon_mtx = getIconMatrix(icon, camera_mtx, vp.pos, vp.is_ortho, vp.ortho_size);
			data->push({icon_mtx, m_models[(int)icon.type]});
		}
	}

	HashMap<EntityRef, Icon> m_icons;
	Model* m_models[(int)IconType::COUNT];
	bool m_is_3d[(int)IconType::COUNT];
	WorldEditor& m_editor;
	RenderScene& m_scene;
};


UniquePtr<EditorIcons> EditorIcons::create(WorldEditor& editor, RenderScene& scene)
{
	return UniquePtr<EditorIconsImpl>::create(editor.getAllocator(), editor, scene);
}


} // namespace Lumix
