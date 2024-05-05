#include "editor_icon.hpp"
#include "editor/world_editor.hpp"
#include "core/crt.hpp"
#include "engine/engine.hpp"
#include "core/geometry.hpp"
#include "core/math.hpp"
#include "core/os.hpp"
#include "engine/resource_manager.hpp"
#include "engine/world.hpp"
#include "renderer/model.hpp"
#include "renderer/render_module.hpp"


namespace Lumix
{


static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
static const ComponentType PHYSICAL_CONTROLLER_TYPE = reflection::getComponentType("physical_controller");
static const ComponentType CAMERA_TYPE = reflection::getComponentType("camera");
static const ComponentType ENVIRONMENT_TYPE = reflection::getComponentType("environment");
static const ComponentType POINT_LIGHT_TYPE = reflection::getComponentType("point_light");
static const ComponentType TERRAIN_TYPE = reflection::getComponentType("terrain");
static const ComponentType CURVE_DECAL_TYPE = reflection::getComponentType("curve_decal");


const char* ICONS[(int)EditorIcons::IconType::COUNT] =
{
	"phy_controller_icon",
	"camera_icon",
	"directional_light_icon",
	"terrain_icon",
	"icon",
	"curve_decal"
};


static const float ORTHO_SIZE_SCALE = 1 / 20.0f;


struct EditorIconsImpl final : EditorIcons
{
	explicit EditorIconsImpl(WorldEditor& editor, RenderModule& module)
		: m_editor(editor)
		, m_icons(editor.getAllocator())
		, m_module(module)
	{
		m_icons.reserve(200);

		World& world = module.getWorld();
		world.entityDestroyed().bind<&EditorIconsImpl::destroyIcon>(this);
		world.componentAdded().bind<&EditorIconsImpl::refreshIcon>(this);
		world.componentDestroyed().bind<&EditorIconsImpl::refreshIcon>(this);

		Engine& engine = m_editor.getEngine();
		FileSystem& fs = engine.getFileSystem();
		ResourceManagerHub& rm = engine.getResourceManager();
		for (u32 i = 0; i < lengthOf(ICONS); ++i)
		{
			const Path path_3d("editor/models/", ICONS[i], "_3d.fbx");
			m_is_3d[i] = fs.fileExists(path_3d);
			if (m_is_3d[i])
			{
				m_models[i] = rm.load<Model>(path_3d);
			}
			else
			{
				const Path path("editor/models/", ICONS[i], ".fbx");
				m_models[i] = rm.load<Model>(path);
			}
		}
	}


	~EditorIconsImpl()
	{
		for (auto& model : m_models) model->decRefCount();

		World& world = m_module.getWorld();
		world.entityDestroyed().bind<&EditorIconsImpl::destroyIcon>(this);
		world.componentAdded().bind<&EditorIconsImpl::refreshIcon>(this);
		world.componentDestroyed().bind<&EditorIconsImpl::refreshIcon>(this);
	}


	void destroyIcon(EntityRef entity)
	{
		if (!m_editor.isLoading()) {
			m_icons.erase(entity);
		}
	}

	void refresh() override {
		m_icons.clear();
		World& world = m_module.getWorld();
		for (EntityPtr e = world.getFirstEntity(); e.isValid(); e = world.getNextEntity((EntityRef)e)) {
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
		World& world = *m_editor.getWorld();
		
		const u64 mask = world.getComponentsMask(entity);
		if (mask & ((u64)1 << (u64)MODEL_INSTANCE_TYPE.index)) return;

		auto& icon = m_icons.insert(entity);
		icon.entity = entity;
		icon.type = IconType::ENTITY;
		for (ComponentUID cmp = world.getFirstComponent(entity); cmp.isValid(); cmp = world.getNextComponent(cmp))
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
			if(cmp.type == CURVE_DECAL_TYPE)
			{
				icon.type = IconType::CURVE_DECAL;
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
			const RayCastModelHit hit = m_models[(int)icon.type]->castRay(rel_origin / icon_tr.scale, rel_dir, nullptr, INVALID_ENTITY, nullptr);
			if (hit.is_hit && hit.t >= 0 && (hit.t < res.t || res.t < 0)) {
				res.t = hit.t;
				res.entity = icon.entity;
			}
		}

		return res;
	}


	Transform getIconTransform(const Icon& icon, const Quat& camera_rot, bool is_ortho, float ortho_size)
	{
		Transform ret = m_editor.getWorld()->getTransform(icon.entity);
		if (!m_is_3d[(int)icon.type]) {
			ret.rot = camera_rot;
		}
		if (is_ortho) {
			ret.scale = Vec3(ortho_size * ORTHO_SIZE_SCALE);
		}
		else {
			ret.scale = Vec3(icon.scale > 0 ? icon.scale : 1);
		}
		return ret;
	}

	void computeScales() override {

		static const float MIN_SCALE_FACTOR = 10;
		static const float MAX_SCALE_FACTOR = 60;

		const World& world = *m_editor.getWorld();
		const Viewport& vp = m_editor.getView().getViewport();

		for(auto& icon : m_icons) {
			const DVec3 position = world.getPosition(icon.entity);
			const float distance = (float)length(position - vp.pos);
			float scale_factor = MIN_SCALE_FACTOR + distance;
			scale_factor = clamp(scale_factor, MIN_SCALE_FACTOR, MAX_SCALE_FACTOR);
			icon.scale = tanf(vp.fov * 0.5f) * distance / scale_factor;
		}
	}

	Matrix getIconMatrix(const Icon& icon, const Matrix& camera_matrix, const DVec3& vp_pos, bool is_ortho, float ortho_size) const override
	{
		Matrix ret;
		if (m_is_3d[(int)icon.type])
		{
			ret = m_editor.getWorld()->getRelativeMatrix(icon.entity, vp_pos);
		}
		else
		{
			ret = camera_matrix;
			ret.setTranslation(Vec3(m_editor.getWorld()->getPosition(icon.entity) - vp_pos));
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
	
	const Model* getModel(IconType type) const override { return m_models[(i32)type]; }

	const HashMap<EntityRef, Icon>& getIcons() const override { return m_icons; }

	HashMap<EntityRef, Icon> m_icons;
	Model* m_models[(int)IconType::COUNT];
	bool m_is_3d[(int)IconType::COUNT];
	WorldEditor& m_editor;
	RenderModule& m_module;
};


UniquePtr<EditorIcons> EditorIcons::create(WorldEditor& editor, RenderModule& module)
{
	return UniquePtr<EditorIconsImpl>::create(editor.getAllocator(), editor, module);
}


} // namespace Lumix
