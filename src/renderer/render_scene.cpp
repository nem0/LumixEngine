#include "render_scene.h"

#include "core/array.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/FS/file_system.h"
#include "core/FS/ifile.h"
#include "core/json_serializer.h"
#include "core/lifo_allocator.h"
#include "core/log.h"
#include "core/math_utils.h"
#include "core/mtjd/generic_job.h"
#include "core/mtjd/job.h"
#include "core/mtjd/manager.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/timer.h"
#include "core/sphere.h"
#include "core/frustum.h"

#include "engine.h"

#include "renderer/culling_system.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/particle_system.h"
#include "renderer/pipeline.h"
#include "renderer/pose.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"

#include "universe/universe.h"


namespace Lumix
{


static const uint32 RENDERABLE_HASH = crc32("renderable");
static const uint32 POINT_LIGHT_HASH = crc32("point_light");
static const uint32 PARTICLE_EMITTER_HASH = crc32("particle_emitter");
static const uint32 PARTICLE_EMITTER_FADE_HASH = crc32("particle_emitter_fade");
static const uint32 PARTICLE_EMITTER_LINEAR_MOVEMENT_HASH =
	crc32("particle_emitter_linear_movement");
static const uint32 PARTICLE_EMITTER_RANDOM_ROTATION_HASH =
	crc32("particle_emitter_random_rotation");
static const uint32 GLOBAL_LIGHT_HASH = crc32("global_light");
static const uint32 CAMERA_HASH = crc32("camera");
static const uint32 TERRAIN_HASH = crc32("terrain");


enum class RenderSceneVersion : int32
{
	PARTICLES,
	WHOLE_LIGHTS,

	LATEST,
	INVALID = -1,
};


struct Renderable
{
	Renderable(IAllocator& allocator)
		: m_pose(allocator)
		, m_meshes(allocator)
	{
	}

	Array<RenderableMesh> m_meshes;
	int32 m_component_index;
	Pose m_pose;
	Model* m_model;
	Matrix m_matrix;
	Entity m_entity;

private:
	Renderable(const Renderable&);
	void operator=(const Renderable&);
};


struct PointLight
{
	Vec3 m_diffuse_color;
	Vec3 m_specular_color;
	float m_intensity;
	Entity m_entity;
	int m_uid;
	float m_fov;
	float m_attenuation_param;
	float m_range;
	bool m_cast_shadows;
};


struct GlobalLight
{
	int m_uid;
	Vec3 m_color;
	float m_intensity;
	Vec3 m_ambient_color;
	float m_ambient_intensity;
	Vec3 m_fog_color;
	float m_fog_density;
	float m_fog_bottom;
	float m_fog_height;
	Entity m_entity;
	Vec4 m_cascades;
};


struct Camera
{
	static const int MAX_SLOT_LENGTH = 30;

	Entity m_entity;
	float m_fov;
	float m_aspect;
	float m_near;
	float m_far;
	float m_width;
	float m_height;
	bool m_is_active;
	bool m_is_free;
	char m_slot[MAX_SLOT_LENGTH + 1];
};


class RenderSceneImpl : public RenderScene
{
private:
	typedef HashMap<int32, int> DynamicRenderableCache;

	class ModelLoadedCallback
	{
	public:
		ModelLoadedCallback(RenderSceneImpl& scene, Model* model)
			: m_scene(scene)
			, m_ref_count(0)
			, m_model(model)
		{
			m_model->onLoaded < ModelLoadedCallback,
				&ModelLoadedCallback::callback > (this);
		}

		~ModelLoadedCallback()
		{
			m_model->getObserverCb().unbind<ModelLoadedCallback, &ModelLoadedCallback::callback>(
				this);
		}

		void callback(Resource::State, Resource::State new_state)
		{
			if (new_state == Resource::State::READY)
			{
				m_scene.modelLoaded(m_model);
			}
		}

		Model* m_model;
		int m_ref_count;
		RenderSceneImpl& m_scene;
	};

public:
	RenderSceneImpl(Renderer& renderer,
		Engine& engine,
		Universe& universe,
		bool is_forward_rendered,
		IAllocator& allocator)
		: m_engine(engine)
		, m_universe(universe)
		, m_renderer(renderer)
		, m_allocator(allocator)
		, m_model_loaded_callbacks(m_allocator)
		, m_dynamic_renderable_cache(m_allocator)
		, m_renderables(m_allocator)
		, m_cameras(m_allocator)
		, m_terrains(m_allocator)
		, m_point_lights(m_allocator)
		, m_light_influenced_geometry(m_allocator)
		, m_global_lights(m_allocator)
		, m_debug_lines(m_allocator)
		, m_debug_points(m_allocator)
		, m_temporary_infos(m_allocator)
		, m_sync_point(true, m_allocator)
		, m_jobs(m_allocator)
		, m_active_global_light_uid(-1)
		, m_global_light_last_uid(-1)
		, m_point_light_last_uid(-1)
		, m_is_forward_rendered(is_forward_rendered)
		, m_renderable_created(m_allocator)
		, m_renderable_destroyed(m_allocator)
		, m_is_grass_enabled(true)
		, m_particle_emitters(m_allocator)
	{
		m_universe.entityTransformed()
			.bind<RenderSceneImpl, &RenderSceneImpl::onEntityMoved>(this);
		m_culling_system =
			CullingSystem::create(m_engine.getMTJDManager(), m_allocator);
		m_time = 0;
	}


	~RenderSceneImpl()
	{
		m_universe.entityTransformed()
			.unbind<RenderSceneImpl, &RenderSceneImpl::onEntityMoved>(this);

		for (int i = 0; i < m_model_loaded_callbacks.size(); ++i)
		{
			LUMIX_DELETE(m_allocator, m_model_loaded_callbacks[i]);
		}

		for (int i = 0; i < m_terrains.size(); ++i)
		{
			LUMIX_DELETE(m_allocator, m_terrains[i]);
		}

		for (int i = 0; i < m_particle_emitters.size(); ++i)
		{
			LUMIX_DELETE(m_allocator, m_particle_emitters[i]);
		}

		for (int i = 0; i < m_renderables.size(); ++i)
		{
			if (m_renderables[i]->m_model)
			{
				auto& manager = m_renderables[i]->m_model->getResourceManager();
				manager.get(ResourceManager::MODEL)->unload(*m_renderables[i]->m_model);
			}
			LUMIX_DELETE(m_allocator, m_renderables[i]);
		}

		CullingSystem::destroy(*m_culling_system);
	}


	Universe& getUniverse() override { return m_universe; }


	bool ownComponentType(uint32 type) const override
	{
		return type == RENDERABLE_HASH || type == POINT_LIGHT_HASH ||
			type == GLOBAL_LIGHT_HASH || type == CAMERA_HASH ||
			type == TERRAIN_HASH;
	}


	IPlugin& getPlugin() const override { return m_renderer; }


	void getRay(ComponentIndex camera,
		float x,
		float y,
		Vec3& origin,
		Vec3& dir) override
	{
		Vec3 camera_pos = m_universe.getPosition(m_cameras[camera].m_entity);
		float width = m_cameras[camera].m_width;
		float height = m_cameras[camera].m_height;
		float nx = 2 * (x / width) - 1;
		float ny = 2 * ((height - y) / height) - 1;

		float fov = m_cameras[camera].m_fov;
		float near_plane = m_cameras[camera].m_near;
		float far_plane = m_cameras[camera].m_far;
		float ratio = width / height;

		Matrix projection_matrix;
		projection_matrix.setPerspective(
			Math::degreesToRadians(fov), ratio, near_plane, far_plane);
		Matrix view_matrix = m_universe.getMatrix(m_cameras[camera].m_entity);
		view_matrix.inverse();
		Matrix inverted = (projection_matrix * view_matrix);
		inverted.inverse();
		Vec4 p0 = inverted * Vec4(nx, ny, -1, 1);
		Vec4 p1 = inverted * Vec4(nx, ny, 1, 1);
		p0.x /= p0.w;
		p0.y /= p0.w;
		p0.z /= p0.w;
		p1.x /= p1.w;
		p1.y /= p1.w;
		p1.z /= p1.w;
		origin = camera_pos;
		dir.x = p1.x - p0.x;
		dir.y = p1.y - p0.y;
		dir.z = p1.z - p0.z;
		dir.normalize();
	}


	Frustum getCameraFrustum(ComponentIndex camera) const override
	{
		Matrix mtx = m_universe.getMatrix(m_cameras[camera].m_entity);
		Frustum ret;
		ret.computePerspective(mtx.getTranslation(),
							   mtx.getZVector(),
							   mtx.getYVector(),
							   Math::degreesToRadians(m_cameras[camera].m_fov),
							   m_cameras[camera].m_width /
								   m_cameras[camera].m_height,
							   m_cameras[camera].m_near,
							   m_cameras[camera].m_far);

		return ret;
	}


	void update(float dt) override
	{
		PROFILE_FUNCTION();
		m_time += dt;
		for (int i = m_debug_lines.size() - 1; i >= 0; --i)
		{
			float life = m_debug_lines[i].m_life;
			if (life < 0)
			{
				m_debug_lines.eraseFast(i);
			}
			else
			{
				life -= dt;
				m_debug_lines[i].m_life = life;
			}
		}

		for (int i = m_debug_points.size() - 1; i >= 0; --i)
		{
			float life = m_debug_points[i].m_life;
			if (life < 0)
			{
				m_debug_points.eraseFast(i);	
			}
			else
			{
				life -= dt;
				m_debug_points[i].m_life = life;
			}
		}

		for (auto* emitter : m_particle_emitters)
		{
			if (!emitter) continue;
			
			emitter->update(dt);
		}
	}

	void serializeCameras(OutputBlob& serializer)
	{
		serializer.write((int32)m_cameras.size());
		for (int i = 0, c = m_cameras.size(); i < c; ++i)
		{
			Camera& camera = m_cameras[i];
			serializer.write(camera.m_entity);
			serializer.write(camera.m_far);
			serializer.write(camera.m_fov);
			serializer.write(camera.m_is_active);
			serializer.write(camera.m_is_free);
			serializer.write(camera.m_near);
			serializer.writeString(camera.m_slot);
		}
	}

	void serializeLights(OutputBlob& serializer)
	{
		serializer.write((int32)m_point_lights.size());
		for (int i = 0, c = m_point_lights.size(); i < c; ++i)
		{
			serializer.write(m_point_lights[i]);
		}
		serializer.write(m_point_light_last_uid);

		serializer.write((int32)m_global_lights.size());
		for (int i = 0, c = m_global_lights.size(); i < c; ++i)
		{
			serializer.write(m_global_lights[i]);
		}
		serializer.write((int32)m_global_light_last_uid);
		serializer.write((int32)m_active_global_light_uid);
	}

	void serializeRenderables(OutputBlob& serializer)
	{
		serializer.write((int32)m_renderables.size());
		for (int i = 0; i < m_renderables.size(); ++i)
		{
			serializer.write(m_renderables[i]->m_component_index);
			serializer.write(m_renderables[i]->m_entity);
			serializer.write(m_culling_system->getLayerMask(i));
			serializer.write(m_renderables[i]->m_model->getPath().getHash());
		}
	}

	void serializeTerrains(OutputBlob& serializer)
	{
		serializer.write((int32)m_terrains.size());
		for (int i = 0; i < m_terrains.size(); ++i)
		{
			if (m_terrains[i])
			{
				serializer.write(true);
				m_terrains[i]->serialize(serializer);
			}
			else
			{
				serializer.write(false);
			}
		}
	}


	void deserializeParticleEmitters(InputBlob& serializer)
	{
		int count;
		serializer.read(count);
		m_particle_emitters.resize(count);
		for(int i = 0; i < count; ++i)
		{
			bool is_emitter;
			serializer.read(is_emitter);
			ParticleEmitter* emitter = nullptr;
			if (is_emitter)
			{
				emitter = LUMIX_NEW(m_allocator, ParticleEmitter)(
					INVALID_ENTITY, m_universe, m_allocator);
				emitter->deserialize(serializer, m_engine.getResourceManager());
				m_universe.addComponent(emitter->m_entity, PARTICLE_EMITTER_HASH, this, i);
				for (auto* module : emitter->m_modules)
				{
					if (module->getType() == ParticleEmitter::AlphaModule::s_type)
					{
						m_universe.addComponent(
							emitter->m_entity, PARTICLE_EMITTER_FADE_HASH, this, i);
					}
					else if (module->getType() == ParticleEmitter::LinearMovementModule::s_type)
					{
						m_universe.addComponent(
							emitter->m_entity, PARTICLE_EMITTER_LINEAR_MOVEMENT_HASH, this, i);
					}
					else if (module->getType() == ParticleEmitter::RandomRotationModule::s_type)
					{
						m_universe.addComponent(
							emitter->m_entity, PARTICLE_EMITTER_RANDOM_ROTATION_HASH, this, i);
					}
				}
			}
			m_particle_emitters[i] = emitter;
		}
	}


	void serializeParticleEmitters(OutputBlob& serializer)
	{
		serializer.write(m_particle_emitters.size());
		for (auto* emitter : m_particle_emitters)
		{
			if (emitter)
			{
				serializer.write(true);
				emitter->serialize(serializer);
			}
			else
			{
				serializer.write(false);
			}
		}
	}


	void serialize(OutputBlob& serializer) override
	{
		serializeCameras(serializer);
		serializeRenderables(serializer);
		serializeLights(serializer);
		serializeTerrains(serializer);
		serializeParticleEmitters(serializer);
	}


	void deserializeCameras(InputBlob& serializer)
	{
		int32 size;
		serializer.read(size);
		m_cameras.resize(size);
		for (int i = 0; i < size; ++i)
		{
			Camera& camera = m_cameras[i];
			serializer.read(camera.m_entity);
			serializer.read(camera.m_far);
			serializer.read(camera.m_fov);
			serializer.read(camera.m_is_active);
			serializer.read(camera.m_is_free);
			serializer.read(camera.m_near);
			serializer.readString(camera.m_slot, sizeof(camera.m_slot));

			if (!camera.m_is_free)
			{
				m_universe.addComponent(m_cameras[i].m_entity, CAMERA_HASH, this, i);
			}
		}
	}

	void deserializeRenderables(InputBlob& serializer)
	{
		int32 size = 0;
		serializer.read(size);
		for (int i = 0; i < m_renderables.size(); ++i)
		{
			setModel(i, nullptr);
			LUMIX_DELETE(m_allocator, m_renderables[i]);
		}
		m_culling_system->clear();
		m_renderables.clear();
		m_renderables.reserve(size);
		m_dynamic_renderable_cache = DynamicRenderableCache(m_allocator);
		for (int i = 0; i < size; ++i)
		{
			m_renderables.push(LUMIX_NEW(m_allocator, Renderable)(m_allocator));
			serializer.read(m_renderables[i]->m_component_index);
			serializer.read(m_renderables[i]->m_entity);
			int64 layer_mask;
			serializer.read(layer_mask);
			m_renderables[i]->m_model = nullptr;
			m_renderables[i]->m_matrix =
				m_universe.getMatrix(m_renderables[i]->m_entity);

			uint32 path;
			serializer.read(path);
			m_culling_system->addStatic(Sphere(
				m_universe.getPosition(m_renderables[i]->m_entity), 1.0f));

			m_culling_system->setLayerMask(i, layer_mask);

			setModel(i,
				static_cast<Model*>(m_engine.getResourceManager()
				.get(ResourceManager::MODEL)
				->load(Path(path))));

			m_universe.addComponent(m_renderables[i]->m_entity,
				RENDERABLE_HASH,
				this,
				m_renderables[i]->m_component_index);
		}
	}

	void deserializeLights(InputBlob& serializer, RenderSceneVersion version)
	{
		int32 size = 0;
		serializer.read(size);
		m_point_lights.resize(size);
		m_light_influenced_geometry.clear();
		for (int i = 0; i < size; ++i)
		{
			m_light_influenced_geometry.push(Array<int>(m_allocator));
			PointLight& light = m_point_lights[i];
			if (version > RenderSceneVersion::WHOLE_LIGHTS)
			{
				serializer.read(light);
			}
			else
			{
				serializer.read(light.m_uid);
				serializer.read(light.m_diffuse_color);
				serializer.read(light.m_intensity);
				serializer.read(light.m_entity);
				serializer.read(light.m_attenuation_param);
				serializer.read(light.m_fov);
				serializer.read(light.m_specular_color);
				serializer.read(light.m_cast_shadows);
				light.m_range = 10;
			}

			m_universe.addComponent(light.m_entity, POINT_LIGHT_HASH, this, light.m_uid);
		}
		serializer.read(m_point_light_last_uid);

		serializer.read(size);
		m_global_lights.resize(size);
		for (int i = 0; i < size; ++i)
		{
			GlobalLight& light = m_global_lights[i];
			if (version > RenderSceneVersion::WHOLE_LIGHTS)
			{
				serializer.read(light);
			}
			else
			{
				serializer.read(light.m_uid);
				serializer.read(light.m_color);
				serializer.read(light.m_intensity);
				serializer.read(light.m_entity);
				serializer.read(light.m_ambient_color);
				serializer.read(light.m_ambient_intensity);
				serializer.read(light.m_fog_color);
				serializer.read(light.m_fog_density);
				serializer.read(light.m_cascades);
				serializer.read(light.m_fog_bottom);
				serializer.read(light.m_fog_height);
			}
			m_universe.addComponent(light.m_entity, GLOBAL_LIGHT_HASH, this, light.m_uid);
		}
		serializer.read(m_global_light_last_uid);
		serializer.read(m_active_global_light_uid);
	}

	void deserializeTerrains(InputBlob& serializer)
	{
		int32 size = 0;
		serializer.read(size);
		int old_size = m_terrains.size();
		for (int i = size; i < m_terrains.size(); ++i)
		{
			LUMIX_DELETE(m_allocator, m_terrains[i]);
			m_terrains[i] = nullptr;
		}
		m_terrains.resize(size);
		for (int i = old_size; i < size; ++i)
		{
			m_terrains[i] = nullptr;
		}
		for (int i = 0; i < size; ++i)
		{
			bool exists;
			serializer.read(exists);
			if (exists)
			{
				if (!m_terrains[i])
				{
					m_terrains[i] = LUMIX_NEW(m_allocator, Terrain)(
						m_renderer, INVALID_ENTITY, *this, m_allocator);
				}
				Terrain* terrain = m_terrains[i];
				terrain->deserialize(serializer, m_universe, *this, i);
			}
			else
			{
				m_terrains[i] = nullptr;
			}
		}
	}


	int getVersion() const override
	{
		return (int)RenderSceneVersion::LATEST;
	}


	void deserialize(InputBlob& serializer, int version) override
	{
		deserializeCameras(serializer);
		deserializeRenderables(serializer);
		deserializeLights(serializer, (RenderSceneVersion)version);
		deserializeTerrains(serializer);
		if (version >= 0) deserializeParticleEmitters(serializer);
	}


	void destroyRenderable(ComponentIndex component)
	{
		m_renderable_destroyed.invoke(component);
		int renderable_index = getRenderable(component);
		Entity entity = m_renderables[renderable_index]->m_entity;
		for (int i = 0; i < m_light_influenced_geometry.size(); ++i)
		{
			Array<int>& influenced_geometry = m_light_influenced_geometry[i];
			for (int j = 0; j < influenced_geometry.size(); ++j)
			{
				if (influenced_geometry[j] == renderable_index)
				{
					influenced_geometry.erase(j);
					--j;
				}
				else if (influenced_geometry[j] > renderable_index)
				{
					--influenced_geometry[j];
				}
			}
		}

		setModel(renderable_index, nullptr);
		LUMIX_DELETE(m_allocator, m_renderables[renderable_index]);
		m_renderables.erase(renderable_index);
		m_culling_system->removeStatic(renderable_index);
		m_universe.destroyComponent(entity, RENDERABLE_HASH, this, component);

		Lumix::HashMap<int32, int>::iterator
			iter = m_dynamic_renderable_cache.begin(),
			end = m_dynamic_renderable_cache.end();
		while (iter != end)
		{
			if (iter.value() > renderable_index)
			{
				--iter.value();
			}
			++iter;
		}
		m_dynamic_renderable_cache.erase(entity);
	}

	void destroyComponent(ComponentIndex component, uint32 type) override
	{
		if (type == RENDERABLE_HASH)
		{
			destroyRenderable(component);
		}
		else if (type == GLOBAL_LIGHT_HASH)
		{
			Entity entity = m_global_lights[getGlobalLightIndex(component)].m_entity;

			m_universe.destroyComponent(entity, type, this, component);

			if (component == m_active_global_light_uid)
			{
				m_active_global_light_uid = -1;
			}
			m_global_lights.eraseFast(getGlobalLightIndex(component));
		}
		else if (type == POINT_LIGHT_HASH)
		{
			int index = getPointLightIndex(component);
			Entity entity = m_point_lights[getPointLightIndex(component)].m_entity;
			m_point_lights.eraseFast(index);
			m_light_influenced_geometry.eraseFast(index);
			m_universe.destroyComponent(entity, type, this, component);
		}
		else if (type == CAMERA_HASH)
		{
			Entity entity = m_cameras[component].m_entity;
			m_cameras[component].m_is_free = true;
			m_universe.destroyComponent(entity, type, this, component);
		}
		else if (type == TERRAIN_HASH)
		{
			Entity entity = m_terrains[component]->getEntity();
			LUMIX_DELETE(m_allocator, m_terrains[component]);
			m_terrains[component] = nullptr;
			m_universe.destroyComponent(entity, type, this, component);
		}
		else if (type == PARTICLE_EMITTER_HASH)
		{
			Entity entity = m_particle_emitters[component]->m_entity;
			LUMIX_DELETE(m_allocator, m_particle_emitters[component]);
			m_particle_emitters[component] = nullptr;
			m_universe.destroyComponent(entity, type, this, component);
		}
		else if (type == PARTICLE_EMITTER_FADE_HASH)
		{
			auto* emitter = m_particle_emitters[component];
			for (auto* module : emitter->m_modules)
			{
				if (module->getType() == ParticleEmitter::AlphaModule::s_type)
				{
					LUMIX_DELETE(m_allocator, module);
					emitter->m_modules.eraseItem(module);
					m_universe.destroyComponent(emitter->m_entity, type, this, component);
					break;
				}
			}
		}
		else if (type == PARTICLE_EMITTER_LINEAR_MOVEMENT_HASH)
		{
			auto* emitter = m_particle_emitters[component];
			for (auto* module : emitter->m_modules)
			{
				if (module->getType() == ParticleEmitter::LinearMovementModule::s_type)
				{
					LUMIX_DELETE(m_allocator, module);
					emitter->m_modules.eraseItem(module);
					m_universe.destroyComponent(emitter->m_entity, type, this, component);
					break;
				}
			}
		}
		else if (type == PARTICLE_EMITTER_RANDOM_ROTATION_HASH)
		{
			auto* emitter = m_particle_emitters[component];
			for (auto* module : emitter->m_modules)
			{
				if (module->getType() == ParticleEmitter::RandomRotationModule::s_type)
				{
					LUMIX_DELETE(m_allocator, module);
					emitter->m_modules.eraseItem(module);
					m_universe.destroyComponent(emitter->m_entity, type, this, component);
					break;
				}
			}
		}
		else
		{
			ASSERT(false);
		}
	}


	Vec2 getParticleEmitterLinearMovementX(ComponentIndex cmp) override
	{
		auto& modules = m_particle_emitters[cmp]->m_modules;
		for (auto* module : modules)
		{
			if (module->getType() == ParticleEmitter::LinearMovementModule::s_type)
			{
				return static_cast<ParticleEmitter::LinearMovementModule*>(module)->m_x;
			}
		}
		return Vec2(0, 0);
	}


	void setParticleEmitterLinearMovementX(ComponentIndex cmp, const Vec2& value) override
	{
		auto& modules = m_particle_emitters[cmp]->m_modules;
		for (auto* module : modules)
		{
			if (module->getType() == ParticleEmitter::LinearMovementModule::s_type)
			{
				static_cast<ParticleEmitter::LinearMovementModule*>(module)->m_x = value; 
				break;
			}
		}
	}


	Vec2 getParticleEmitterLinearMovementY(ComponentIndex cmp) override
	{
		auto& modules = m_particle_emitters[cmp]->m_modules;
		for (auto* module : modules)
		{
			if (module->getType() == ParticleEmitter::LinearMovementModule::s_type)
			{
				return static_cast<ParticleEmitter::LinearMovementModule*>(module)->m_y;
			}
		}
		return Vec2(0, 0);
	}


	void setParticleEmitterLinearMovementY(ComponentIndex cmp, const Vec2& value) override
	{
		auto& modules = m_particle_emitters[cmp]->m_modules;
		for (auto* module : modules)
		{
			if (module->getType() == ParticleEmitter::LinearMovementModule::s_type)
			{
				static_cast<ParticleEmitter::LinearMovementModule*>(module)->m_y = value;
				break;
			}
		}
	}


	Vec2 getParticleEmitterLinearMovementZ(ComponentIndex cmp) override
	{
		auto& modules = m_particle_emitters[cmp]->m_modules;
		for (auto* module : modules)
		{
			if (module->getType() == ParticleEmitter::LinearMovementModule::s_type)
			{
				return static_cast<ParticleEmitter::LinearMovementModule*>(module)->m_z;
			}
		}
		return Vec2(0, 0);
	}


	void setParticleEmitterLinearMovementZ(ComponentIndex cmp, const Vec2& value) override
	{
		auto& modules = m_particle_emitters[cmp]->m_modules;
		for (auto* module : modules)
		{
			if (module->getType() == ParticleEmitter::LinearMovementModule::s_type)
			{
				static_cast<ParticleEmitter::LinearMovementModule*>(module)->m_z = value;
				break;
			}
		}
	}


	Vec2 getParticleEmitterInitialLife(ComponentIndex cmp) override
	{
		return m_particle_emitters[cmp]->m_initial_life;
	}


	Vec2 getParticleEmitterSpawnPeriod(ComponentIndex cmp) override
	{
		return m_particle_emitters[cmp]->m_spawn_period;
	}


	void setParticleEmitterInitialLife(ComponentIndex cmp, const Vec2& value) override
	{
		m_particle_emitters[cmp]->m_initial_life = value;
		m_particle_emitters[cmp]->m_initial_life.check();
	}


	void setParticleEmitterInitialSize(ComponentIndex cmp, const Vec2& value) override
	{
		m_particle_emitters[cmp]->m_initial_size = value;
		m_particle_emitters[cmp]->m_initial_size.check();
	}


	Vec2 getParticleEmitterInitialSize(ComponentIndex cmp) override
	{
		return m_particle_emitters[cmp]->m_initial_size;
	}


	void setParticleEmitterSpawnPeriod(ComponentIndex cmp, const Vec2& value) override
	{
		m_particle_emitters[cmp]->m_spawn_period = value;
		m_particle_emitters[cmp]->m_spawn_period.from =
			Math::maxValue(0.01f, m_particle_emitters[cmp]->m_spawn_period.from);
		m_particle_emitters[cmp]->m_spawn_period.check();
	}


	ComponentIndex createComponent(uint32 type,
		Entity entity) override
	{
		if (type == TERRAIN_HASH)
		{
			Terrain* terrain = LUMIX_NEW(m_allocator, Terrain)(
				m_renderer, entity, *this, m_allocator);
			m_terrains.push(terrain);
			m_universe.addComponent(entity, type, this, m_terrains.size() - 1);
			return m_terrains.size() - 1;
		}
		else if (type == CAMERA_HASH)
		{
			Camera& camera = m_cameras.pushEmpty();
			camera.m_is_free = false;
			camera.m_is_active = false;
			camera.m_entity = entity;
			camera.m_fov = 60;
			camera.m_width = 800;
			camera.m_height = 600;
			camera.m_aspect = 800.0f / 600.0f;
			camera.m_near = 0.1f;
			camera.m_far = 10000.0f;
			camera.m_slot[0] = '\0';
			m_universe.addComponent(entity, type, this, m_cameras.size() - 1);
			return m_cameras.size() - 1;
		}
		else if (type == RENDERABLE_HASH)
		{
			return createRenderable(entity);
		}
		else if (type == GLOBAL_LIGHT_HASH)
		{
			return createGlobalLight(entity);
		}
		else if (type == POINT_LIGHT_HASH)
		{
			return createPointLight(entity);
		}
		else if (type == PARTICLE_EMITTER_HASH)
		{
			return createParticleEmitter(entity);
		}
		else if (type == PARTICLE_EMITTER_FADE_HASH)
		{
			return createParticleEmitterFade(entity);
		}
		else if (type == PARTICLE_EMITTER_LINEAR_MOVEMENT_HASH)
		{
			return createParticleEmitterLinearMovement(entity);
		}
		else if (type == PARTICLE_EMITTER_RANDOM_ROTATION_HASH)
		{
			return createParticleEmitterRandomRotation(entity);
		}
		return INVALID_COMPONENT;
	}


	ComponentIndex createParticleEmitterRandomRotation(Entity entity)
	{
		for (int i = 0; i < m_particle_emitters.size(); ++i)
		{
			auto* emitter = m_particle_emitters[i];
			if (emitter->m_entity == entity)
			{
				auto module = LUMIX_NEW(m_allocator, ParticleEmitter::RandomRotationModule)(*emitter);
				emitter->addModule(module);
				m_universe.addComponent(entity, PARTICLE_EMITTER_RANDOM_ROTATION_HASH, this, i);
				return i;
			}
		}
		return INVALID_COMPONENT;
	}


	ComponentIndex createParticleEmitterLinearMovement(Entity entity)
	{
		for (int i = 0; i < m_particle_emitters.size(); ++i)
		{
			auto* emitter = m_particle_emitters[i];
			if (emitter->m_entity == entity)
			{
				auto module = LUMIX_NEW(m_allocator, ParticleEmitter::LinearMovementModule)(*emitter);
				emitter->addModule(module);
				m_universe.addComponent(entity, PARTICLE_EMITTER_LINEAR_MOVEMENT_HASH, this, i);
				return i;
			}
		}
		return INVALID_COMPONENT;
	}
	

	ComponentIndex createParticleEmitterFade(Entity entity)
	{
		for (int i = 0; i < m_particle_emitters.size(); ++i)
		{
			auto* emitter = m_particle_emitters[i];
			if (emitter->m_entity == entity)
			{
				auto module = LUMIX_NEW(m_allocator, ParticleEmitter::AlphaModule)(*emitter);
				emitter->addModule(module);
				m_universe.addComponent(entity, PARTICLE_EMITTER_FADE_HASH, this, i);
				return i;
			}
		}
		return INVALID_COMPONENT;
	}


	ComponentIndex createParticleEmitter(Entity entity)
	{
		int index = -1;
		for (int i = 0, c = m_particle_emitters.size(); i < c; ++i)
		{
			if (!m_particle_emitters[i])
			{
				index = i;
				break;
			}
		}

		if (index == -1)
		{
			index = m_particle_emitters.size();
			m_particle_emitters.push(nullptr);
		}

		m_particle_emitters[index] =
			LUMIX_NEW(m_allocator, ParticleEmitter)(entity, m_universe, m_allocator);

		m_universe.addComponent(entity, PARTICLE_EMITTER_HASH, this, index);

		return index;
	}


	ComponentIndex getRenderableComponent(Entity entity) override
	{
		DynamicRenderableCache::iterator iter =
			m_dynamic_renderable_cache.find(entity);
		if (!iter.isValid())
		{
			for (int i = 0, c = m_renderables.size(); i < c; ++i)
			{
				if (m_renderables[i]->m_entity == entity)
				{
					m_dynamic_renderable_cache.insert(entity, i);
					return i;
				}
			}
		}
		else
		{
			return iter.value();
		}
		return INVALID_COMPONENT;
	}


	Frustum getPointLightFrustum(ComponentIndex index) const
	{
		const PointLight& light = m_point_lights[index];
		Frustum frustum;
		frustum.computeOrtho(m_universe.getPosition(light.m_entity),
							 Vec3(1, 0, 0),
							 Vec3(0, 1, 0),
							 2.0f * light.m_range,
							 2.0f * light.m_range,
							 -light.m_range,
							 light.m_range);

		return frustum;
	}


	void onEntityMoved(Entity entity)
	{
		DynamicRenderableCache::iterator iter =
			m_dynamic_renderable_cache.find(entity);
		int renderable_index = -1;
		if (!iter.isValid())
		{
			for (int i = 0, c = m_renderables.size(); i < c; ++i)
			{
				if (m_renderables[i]->m_entity == entity)
				{
					m_dynamic_renderable_cache.insert(entity, i);
					m_renderables[i]->m_matrix = m_universe.getMatrix(entity);
					m_culling_system->updateBoundingPosition(
						m_universe.getPosition(entity), i);
					float bounding_radius =
						m_renderables[i]->m_model
							? m_renderables[i]->m_model->getBoundingRadius()
							: 1;
					m_culling_system->updateBoundingRadius(
						m_universe.getScale(entity) * bounding_radius, i);
					renderable_index = i;
					break;
				}
			}
		}
		else
		{
			m_renderables[iter.value()]->m_matrix =
				m_universe.getMatrix(entity);

			renderable_index = iter.value();

			m_culling_system->updateBoundingPosition(
				m_universe.getPosition(entity), iter.value());

			float bounding_radius = m_renderables[renderable_index]->m_model
										? m_renderables[renderable_index]
											  ->m_model->getBoundingRadius()
										: 1;
			m_culling_system->updateBoundingRadius(
				m_universe.getScale(entity) * bounding_radius, iter.value());
		}

		for (int i = 0, c = m_point_lights.size(); i < c; ++i)
		{
			if (renderable_index >= 0 && m_is_forward_rendered)
			{
				Frustum frustum = getPointLightFrustum(i);

				for (int j = 0; j < m_light_influenced_geometry[i].size(); ++j)
				{
					if (m_light_influenced_geometry[i][j] == renderable_index)
					{
						m_light_influenced_geometry[i].eraseFast(j);
						break;
					}
				}
				Renderable* r = m_renderables[renderable_index];
				Vec3 pos = m_universe.getPosition(r->m_entity);
				if (frustum.isSphereInside(pos, r->m_model->getBoundingRadius()))
				{
					m_light_influenced_geometry[i].push(renderable_index);
				}
			}
			if (m_point_lights[i].m_entity == entity)
			{
				detectLightInfluencedGeometry(i);
				break;
			}
		}
	}

	Engine& getEngine() const override { return m_engine; }


	ComponentIndex getTerrainComponent(Entity entity) override
	{
		for (int i = 0; i < m_terrains.size(); ++i)
		{
			auto* terrain = m_terrains[i];
			if (terrain && terrain->getEntity() == entity)
			{
				return i;
			}
		}
		return -1;
	}


	Vec3 getTerrainNormalAt(ComponentIndex cmp, float x, float z)
	{
		return m_terrains[cmp]->getNormal(x, z);
	}


	float getTerrainHeightAt(ComponentIndex cmp, float x, float z) override
	{
		return m_terrains[cmp]->getHeight(x, z);
	}


	void getTerrainSize(ComponentIndex cmp, float* width, float* height) override
	{
		m_terrains[cmp]->getSize(width, height);
	}


	void setTerrainMaterialPath(ComponentIndex cmp, const char* path) override
	{
		Material* material = static_cast<Material*>(
			m_engine.getResourceManager().get(ResourceManager::MATERIAL)->load(Path(path)));
		m_terrains[cmp]->setMaterial(material);
	}


	Material* getTerrainMaterial(ComponentIndex cmp) override
	{
		return m_terrains[cmp]->getMaterial();
	}


	const char* getTerrainMaterialPath(ComponentIndex cmp) override
	{
		if (m_terrains[cmp]->getMaterial())
		{
			return m_terrains[cmp]->getMaterial()->getPath().c_str();
		}
		else
		{
			return "";
		}
	}


	void setTerrainXZScale(ComponentIndex cmp, float scale) override
	{
		m_terrains[cmp]->setXZScale(scale);
	}

	float getTerrainXZScale(ComponentIndex cmp) override
	{
		return m_terrains[cmp]->getXZScale();
	}


	void setTerrainYScale(ComponentIndex cmp, float scale) override
	{
		m_terrains[cmp]->setYScale(scale);
	}

	float getTerrainYScale(ComponentIndex cmp)
	{
		return m_terrains[cmp]->getYScale();
	}


	Pose& getPose(ComponentIndex cmp) override
	{
		return m_renderables[getRenderable(cmp)]->m_pose;
	}


	Entity getRenderableEntity(ComponentIndex cmp) override
	{
		return m_renderables[getRenderable(cmp)]->m_entity;
	}


	Model* getRenderableModel(ComponentIndex cmp) override
	{
		return m_renderables[getRenderable(cmp)]->m_model;
	}


	void showRenderable(ComponentIndex cmp) override
	{
		m_culling_system->enableStatic(getRenderable(cmp));
	}


	void hideRenderable(ComponentIndex cmp) override
	{
		int renderable_index = getRenderable(cmp);
		m_culling_system->disableStatic(renderable_index);
	}


	const char* getRenderablePath(ComponentIndex cmp) override
	{
		int index = getRenderable(cmp);
		if (index >= 0 && m_renderables[index]->m_model)
		{
			return m_renderables[index]->m_model->getPath().c_str();
		}
		else
		{
			return "";
		}
	}


	void setRenderableLayer(ComponentIndex cmp,
									const int32& layer) override
	{
		m_culling_system->setLayerMask(getRenderable(cmp),
									   (int64)1 << (int64)layer);
	}


	void setRenderablePath(ComponentIndex cmp,
								   const char* path) override
	{
		int renderable_index = getRenderable(cmp);
		Renderable& r = *m_renderables[renderable_index];

		Model* model = static_cast<Model*>(m_engine.getResourceManager()
											   .get(ResourceManager::MODEL)
											   ->load(Path(path)));
		setModel(renderable_index, model);
		r.m_matrix = m_universe.getMatrix(r.m_entity);
	}



	void forceGrassUpdate(ComponentIndex cmp) override
	{
		m_terrains[cmp]->forceGrassUpdate();
	}


	void getTerrainInfos(Array<const TerrainInfo*>& infos,
								 int64 layer_mask,
								 const Vec3& camera_pos,
								 LIFOAllocator& frame_allocator) override
	{
		PROFILE_FUNCTION();
		infos.reserve(m_terrains.size());
		for (int i = 0; i < m_terrains.size(); ++i)
		{
			if (m_terrains[i] &&
				(m_terrains[i]->getLayerMask() & layer_mask) != 0)
			{
				m_terrains[i]->getInfos(infos, camera_pos, frame_allocator);
			}
		}
	}


	void getGrassInfos(const Frustum& frustum,
							   Array<GrassInfo>& infos,
							   int64 layer_mask,
							   ComponentIndex camera) override
	{
		PROFILE_FUNCTION();

		if (!m_is_grass_enabled) return;

		for (int i = 0; i < m_terrains.size(); ++i)
		{
			if (m_terrains[i] && (m_terrains[i]->getLayerMask() & layer_mask) != 0)
			{
				m_terrains[i]->getGrassInfos(frustum, infos, camera);
			}
		}
	}


	bool isGrassEnabled() const override
	{
		return m_is_grass_enabled;
	}
	

	int getGrassDistance(ComponentIndex cmp) override
	{
		return m_terrains[cmp]->getGrassDistance();
	}


	void setGrassDistance(ComponentIndex cmp, int value) override
	{
		m_terrains[cmp]->setGrassDistance(value);
	}

	
	void enableGrass(bool enabled) override
	{
		m_is_grass_enabled = enabled;
	}


	void
	setGrassDensity(ComponentIndex cmp, int index, int density) override
	{
		m_terrains[cmp]->setGrassTypeDensity(index, density);
	}


	int getGrassDensity(ComponentIndex cmp, int index) override
	{
		return m_terrains[cmp]->getGrassTypeDensity(index);
	}


	void
	setGrassGround(ComponentIndex cmp, int index, int ground) override
	{
		m_terrains[cmp]->setGrassTypeGround(index, ground);
	}


	int getGrassGround(ComponentIndex cmp, int index) override
	{
		return m_terrains[cmp]->getGrassTypeGround(index);
	}


	void
	setGrassPath(ComponentIndex cmp, int index, const char* path) override
	{
		m_terrains[cmp]->setGrassTypePath(index, Path(path));
	}


	const char* getGrassPath(ComponentIndex cmp, int index) override
	{
		return m_terrains[cmp]->getGrassTypePath(index).c_str();
	}


	int getGrassCount(ComponentIndex cmp) override
	{
		return m_terrains[cmp]->getGrassTypeCount();
	}


	void addGrass(ComponentIndex cmp, int index) override
	{
		m_terrains[cmp]->addGrassType(index);
	}


	void removeGrass(ComponentIndex cmp, int index) override
	{
		m_terrains[cmp]->removeGrassType(index);
	}


	ComponentIndex getFirstRenderable() override
	{
		if (m_renderables.empty())
		{
			return INVALID_COMPONENT;
		}
		return m_renderables[0]->m_component_index;
	}


	int getRenderable(int index)
	{
		int l = 0;
		int h = m_renderables.size() - 1;
		while (l <= h)
		{
			int m = (l + h) >> 1;
			if (m_renderables[m]->m_component_index < index)
			{
				l = m + 1;
			}
			else if (m_renderables[m]->m_component_index > index)
			{
				h = m - 1;
			}
			else
			{
				return m;
			}
		}

		return -1;
	}


	ComponentIndex getNextRenderable(ComponentIndex cmp) override
	{
		int i = getRenderable(cmp);
		if (i + 1 < m_renderables.size())
		{
			return m_renderables[i + 1]->m_component_index;
		}
		return INVALID_COMPONENT;
	}


	const CullingSystem::Results* cull(const Frustum& frustum,
									   int64 layer_mask)
	{
		PROFILE_FUNCTION();
		if (m_renderables.empty())
			return nullptr;

		m_culling_system->cullToFrustumAsync(frustum, layer_mask);
		return &m_culling_system->getResult();
	}


	void mergeTemporaryInfos(Array<const RenderableMesh*>& all_infos)
	{
		PROFILE_FUNCTION();
		all_infos.reserve(m_renderables.size() * 2);
		for (int i = 0; i < m_temporary_infos.size(); ++i)
		{
			Array<const RenderableMesh*>& subinfos = m_temporary_infos[i];
			if (!subinfos.empty())
			{
				int size = all_infos.size();
				all_infos.resize(size + subinfos.size());
				copyMemory(&all_infos[0] + size,
					   &subinfos[0],
					   sizeof(subinfos[0]) * subinfos.size());
			}
		}
	}


	void runJobs(Array<MTJD::Job*>& jobs, MTJD::Group& sync_point)
	{
		PROFILE_FUNCTION();
		for (int i = 0; i < jobs.size(); ++i)
		{
			m_engine.getMTJDManager().schedule(jobs[i]);
		}
		if (!jobs.empty())
		{
			sync_point.sync();
		}
	}


	void fillTemporaryInfos(const CullingSystem::Results& results,
							const Frustum& frustum,
							int64 layer_mask)
	{
		PROFILE_FUNCTION();
		m_jobs.clear();

		while (m_temporary_infos.size() < results.size())
		{
			m_temporary_infos.emplace(m_allocator);
		}
		while (m_temporary_infos.size() > results.size())
		{
			m_temporary_infos.pop();
		}
		for (int subresult_index = 0; subresult_index < results.size(); ++subresult_index)
		{
			Array<const RenderableMesh*>& subinfos = m_temporary_infos[subresult_index];
			subinfos.clear();
			MTJD::Job* job = MTJD::makeJob(m_engine.getMTJDManager(),
				[&subinfos, layer_mask, this, &results, subresult_index, &frustum]()
				{
					Vec3 frustum_position = frustum.getPosition();
					const CullingSystem::Subresults& subresults = results[subresult_index];
					for (int i = 0, c = subresults.size(); i < c; ++i)
					{
						const Renderable* LUMIX_RESTRICT renderable = m_renderables[subresults[i]];
						const Model* LUMIX_RESTRICT model = renderable->m_model;
						float squared_distance =
							(renderable->m_matrix.getTranslation() - frustum_position)
								.squaredLength();
						if (model && model->isReady())
						{
							LODMeshIndices lod = model->getLODMeshIndices(squared_distance);
							for (int j = lod.getFrom(), c = lod.getTo(); j <= c; ++j)
							{
								subinfos.push(&renderable->m_meshes[j]);
							}
						}
					}
				},
				m_allocator);
			job->addDependency(&m_sync_point);
			m_jobs.push(job);
		}
		runJobs(m_jobs, m_sync_point);
	}


	int getClosestPointLights(const Vec3& reference_pos,
									   ComponentIndex* lights,
									   int max_lights) override
	{

		float dists[16];
		ASSERT(max_lights <= lengthOf(dists));
		ASSERT(max_lights > 0);
		if (m_point_lights.empty()) return 0;

		int light_count = 0;
		for (auto light : m_point_lights)
		{
			Vec3 light_pos = m_universe.getPosition(light.m_entity);
			float dist_squared = (reference_pos - light_pos).squaredLength();

			dists[light_count] = dist_squared;
			lights[light_count] = light.m_uid;

			for (int i = light_count; i > 0 && dists[i - 1] > dists[i]; --i)
			{
				float tmp = dists[i];
				dists[i] = dists[i - 1];
				dists[i - 1] = tmp;

				ComponentIndex tmp2 = lights[i];
				lights[i] = lights[i - 1];
				lights[i - 1] = tmp2;
			}
			++light_count;
			if (light_count == max_lights)
			{
				break;
			}
		}

		for (int i = max_lights; i < m_point_lights.size(); ++i)
		{
			PointLight& light = m_point_lights[i];
			Vec3 light_pos = m_universe.getPosition(light.m_entity);
			float dist_squared = (reference_pos - light_pos).squaredLength();

			if (dist_squared < dists[max_lights - 1])
			{
				dists[max_lights - 1] = dist_squared;
				lights[max_lights - 1] = light.m_uid;

				for (int i = max_lights - 1; i > 0 && dists[i - 1] > dists[i];
					 --i)
				{
					float tmp = dists[i];
					dists[i] = dists[i - 1];
					dists[i - 1] = tmp;

					ComponentIndex tmp2 = lights[i];
					lights[i] = lights[i - 1];
					lights[i - 1] = tmp2;
				}
			}
		}

		return light_count;
	}


	void getPointLights(const Frustum& frustum,
								Array<ComponentIndex>& lights) override
	{
		for (int i = 0, ci = m_point_lights.size(); i < ci; ++i)
		{
			PointLight& light = m_point_lights[i];

			if (frustum.isSphereInside(m_universe.getPosition(light.m_entity),
									   light.m_range))
			{
				lights.push(light.m_uid);
			}
		}
	}


	Entity getCameraEntity(ComponentIndex camera) const override
	{
		return m_cameras[camera].m_entity;
	}


	void setLightCastShadows(ComponentIndex cmp,
									 bool cast_shadows) override
	{
		m_point_lights[getPointLightIndex(cmp)].m_cast_shadows = cast_shadows;
	}


	bool getLightCastShadows(ComponentIndex cmp) override 
	{
		return m_point_lights[getPointLightIndex(cmp)].m_cast_shadows;
	}


	void
	getPointLightInfluencedGeometry(ComponentIndex light_cmp,
									const Frustum& frustum,
									Array<const RenderableMesh*>& infos,
									int64 layer_mask) override
	{
		PROFILE_FUNCTION();

		int light_index = getPointLightIndex(light_cmp);
		for (int j = 0, cj = m_light_influenced_geometry[light_index].size();
			 j < cj;
			 ++j)
		{
			int renderable_index = m_light_influenced_geometry[light_index][j];
			Renderable* renderable = m_renderables[renderable_index];
			bool is_layer =
				(layer_mask &
				 m_culling_system->getLayerMask(renderable_index)) != 0;
			const Sphere& sphere =
				m_culling_system->getSpheres()[renderable_index];
			if (is_layer &&
				frustum.isSphereInside(sphere.m_position, sphere.m_radius))
			{
				for (int k = 0, kc = renderable->m_meshes.size(); k < kc; ++k)
				{
					infos.push(&renderable->m_meshes[k]);
				}
			}
		}
	}


	void
		getPointLightInfluencedGeometry(ComponentIndex light_cmp,
		Array<const RenderableMesh*>& infos,
		int64 layer_mask) override
	{
		PROFILE_FUNCTION();

		int light_index = getPointLightIndex(light_cmp);
		auto& geoms = m_light_influenced_geometry[light_index];
		for (int j = 0, cj = geoms.size(); j < cj; ++j)
		{
			Renderable* renderable = m_renderables[geoms[j]];
			for (int k = 0, kc = renderable->m_meshes.size(); k < kc; ++k)
			{
				infos.push(&renderable->m_meshes[k]);
			}
		}
	}


	void getRenderableEntities(const Frustum& frustum,
		Array<Entity>& entities,
		int64 layer_mask) override
	{
		PROFILE_FUNCTION();

		const CullingSystem::Results* results = cull(frustum, layer_mask);
		if (!results) return;

		for (auto& subresults : *results)
		{
			for (int renderable_index : subresults)
			{
				entities.push(m_renderables[renderable_index]->m_entity);
			}
		}
	}


	void getRenderableInfos(const Frustum& frustum,
									Array<const RenderableMesh*>& meshes,
									int64 layer_mask) override
	{
		PROFILE_FUNCTION();

		const CullingSystem::Results* results = cull(frustum, layer_mask);
		if (!results) return;

		fillTemporaryInfos(*results, frustum, layer_mask);
		mergeTemporaryInfos(meshes);
	}


	void setCameraSlot(ComponentIndex camera, const char* slot) override
	{
		copyString(m_cameras[camera].m_slot, Camera::MAX_SLOT_LENGTH, slot);
	}

	const char* getCameraSlot(ComponentIndex camera) override
	{
		return m_cameras[camera].m_slot;
	}

	float getCameraFOV(ComponentIndex camera) override
	{
		return m_cameras[camera].m_fov;
	}

	void setCameraFOV(ComponentIndex camera, float fov) override
	{
		m_cameras[camera].m_fov = fov;
	}

	void setCameraNearPlane(ComponentIndex camera,
									float near_plane) override
	{
		m_cameras[camera].m_near = near_plane;
	}

	float getCameraNearPlane(ComponentIndex camera) override
	{
		return m_cameras[camera].m_near;
	}

	void setCameraFarPlane(ComponentIndex camera,
								   float far_plane) override
	{
		m_cameras[camera].m_far = far_plane;
	}

	float getCameraFarPlane(ComponentIndex camera) override
	{
		return m_cameras[camera].m_far;
	}

	float getCameraWidth(ComponentIndex camera) override
	{
		return m_cameras[camera].m_width;
	}


	float getCameraHeight(ComponentIndex camera) override
	{
		return m_cameras[camera].m_height;
	}


	void setCameraSize(ComponentIndex camera, int w, int h) override
	{
		m_cameras[camera].m_width = (float)w;
		m_cameras[camera].m_height = (float)h;
		m_cameras[camera].m_aspect = w / (float)h;
	}


	const Array<DebugLine>& getDebugLines() const override
	{
		return m_debug_lines;
	}


	const Array<DebugPoint>& getDebugPoints() const override
	{
		return m_debug_points;
	}


	void addDebugSphere(const Vec3& center,
		float radius,
		uint32 color,
		float life) override
	{
		static const int COLS = 36;
		static const int ROWS = COLS >> 1;
		static const float STEP = (Math::PI / 180.0f) * 360.0f / COLS;
		int p2 = COLS >> 1;
		int r2 = ROWS >> 1;
		float prev_ci = 1;
		float prev_si = 0;
		for (int y = -r2; y < r2; ++y)
		{
			float cy = cos(y * STEP);
			float cy1 = cos((y + 1) * STEP);
			float sy = sin(y * STEP);
			float sy1 = sin((y + 1) * STEP);

			for (int i = -p2; i < p2; ++i)
			{
				float ci = cos(i * STEP);
				float si = sin(i * STEP);
				addDebugLine(Vec3(center.x + radius * ci * cy,
								  center.y + radius * sy,
								  center.z + radius * si * cy),
							 Vec3(center.x + radius * ci * cy1,
								  center.y + radius * sy1,
								  center.z + radius * si * cy1),
							 color,
							 life);
				addDebugLine(Vec3(center.x + radius * ci * cy,
								  center.y + radius * sy,
								  center.z + radius * si * cy),
							 Vec3(center.x + radius * prev_ci * cy,
								  center.y + radius * sy,
								  center.z + radius * prev_si * cy),
							 color,
							 life);
				addDebugLine(Vec3(center.x + radius * prev_ci * cy1,
								  center.y + radius * sy1,
								  center.z + radius * prev_si * cy1),
							 Vec3(center.x + radius * ci * cy1,
								  center.y + radius * sy1,
								  center.z + radius * si * cy1),
							 color,
							 life);
				prev_ci = ci;
				prev_si = si;
			}
		}
	}


	void addDebugHalfSphere(const Vec3& center, float radius, bool top, uint32 color, float life)
	{
		static const int COLS = 36;
		static const int ROWS = COLS >> 1;
		static const float STEP = (Math::PI / 180.0f) * 360.0f / COLS;
		int p2 = COLS >> 1;
		int yfrom = top ? 0 : -(ROWS >> 1);
		int yto = top ? ROWS >> 1 : 0;
		for (int y = yfrom; y < yto; ++y)
		{
			float cy = cos(y * STEP);
			float cy1 = cos((y + 1) * STEP);
			float sy = sin(y * STEP);
			float sy1 = sin((y + 1) * STEP);
			float prev_ci = cos((-p2 - 1) * STEP);
			float prev_si = sin((-p2 - 1) * STEP);

			for (int i = -p2; i < p2; ++i)
			{
				float ci = cos(i * STEP);
				float si = sin(i * STEP);
				addDebugLine(Vec3(center.x + radius * ci * cy,
					center.y + radius * sy,
					center.z + radius * si * cy),
					Vec3(center.x + radius * ci * cy1,
					center.y + radius * sy1,
					center.z + radius * si * cy1),
					color,
					life);
				addDebugLine(Vec3(center.x + radius * ci * cy,
					center.y + radius * sy,
					center.z + radius * si * cy),
					Vec3(center.x + radius * prev_ci * cy,
					center.y + radius * sy,
					center.z + radius * prev_si * cy),
					color,
					life);
				addDebugLine(Vec3(center.x + radius * prev_ci * cy1,
					center.y + radius * sy1,
					center.z + radius * prev_si * cy1),
					Vec3(center.x + radius * ci * cy1,
					center.y + radius * sy1,
					center.z + radius * si * cy1),
					color,
					life);
				prev_ci = ci;
				prev_si = si;
			}
		}
	}



	void addDebugCapsule(const Vec3& position,
		float height,
		float radius,
		uint32 color,
		float life) override
	{
		addDebugHalfSphere(position + Vec3(0, radius, 0), radius, false, color, life);
		addDebugHalfSphere(position + Vec3(0, radius + height, 0), radius, true, color, life);

		Vec3 z_vec(0, 0, 1.0f);
		Vec3 x_vec(1.0f, 0, 0);
		z_vec.normalize();
		x_vec.normalize();
		Vec3 bottom = position + Vec3(0, radius, 0);
		Vec3 top = bottom + Vec3(0, height, 0);
		for (int i = 1; i <= 32; ++i)
		{
			float a = i / 32.0f * 2 * Math::PI;
			float x = cosf(a) * radius;
			float z = sinf(a) * radius;
			addDebugLine(bottom + x_vec * x + z_vec * z,
				top + x_vec * x + z_vec * z,
				color,
				life);
		}
	}


	void addDebugCylinder(const Vec3& position,
								  const Vec3& up,
								  float radius,
								  uint32 color,
								  float life) override
	{
		Vec3 z_vec(-up.y, up.x, 0);
		Vec3 x_vec = crossProduct(up, z_vec);
		float prevx = radius;
		float prevz = 0;
		z_vec.normalize();
		x_vec.normalize();
		Vec3 top = position + up;
		for (int i = 1; i <= 32; ++i)
		{
			float a = i / 32.0f * 2 * Math::PI;
			float x = cosf(a) * radius;
			float z = sinf(a) * radius;
			addDebugLine(position + x_vec * x + z_vec * z,
						 position + x_vec * prevx + z_vec * prevz,
						 color,
						 life);
			addDebugLine(top + x_vec * x + z_vec * z,
						 top + x_vec * prevx + z_vec * prevz,
						 color,
						 life);
			addDebugLine(position + x_vec * x + z_vec * z,
						 top + x_vec * x + z_vec * z,
						 color,
						 life);
			prevx = x;
			prevz = z;
		}
	}


	void addDebugCube(const Vec3& pos,
		const Vec3& dir,
		const Vec3& up,
		const Vec3& right,
		uint32 color,
		float life) override
	{
		addDebugLine(pos + dir + up + right, pos + dir + up - right, color, life);
		addDebugLine(pos - dir + up + right, pos - dir + up - right, color, life);
		addDebugLine(pos + dir + up + right, pos - dir + up + right, color, life);
		addDebugLine(pos + dir + up - right, pos - dir + up - right, color, life);

		addDebugLine(pos + dir - up + right, pos + dir - up - right, color, life);
		addDebugLine(pos - dir - up + right, pos - dir - up - right, color, life);
		addDebugLine(pos + dir - up + right, pos - dir - up + right, color, life);
		addDebugLine(pos + dir - up - right, pos - dir - up - right, color, life);

		addDebugLine(pos + dir + up + right, pos + dir - up + right, color, life);
		addDebugLine(pos + dir + up - right, pos + dir - up - right, color, life);
		addDebugLine(pos - dir + up + right, pos - dir - up + right, color, life);
		addDebugLine(pos - dir + up - right, pos - dir - up - right, color, life);

	}


	void addDebugCube(const Vec3& min,
							  const Vec3& max,
							  uint32 color,
							  float life) override
	{
		Vec3 a = min;
		Vec3 b = min;
		b.x = max.x;
		addDebugLine(a, b, color, life);
		a.set(b.x, b.y, max.z);
		addDebugLine(a, b, color, life);
		b.set(min.x, a.y, a.z);
		addDebugLine(a, b, color, life);
		a.set(b.x, b.y, min.z);
		addDebugLine(a, b, color, life);

		a = min;
		a.y = max.y;
		b = a;
		b.x = max.x;
		addDebugLine(a, b, color, life);
		a.set(b.x, b.y, max.z);
		addDebugLine(a, b, color, life);
		b.set(min.x, a.y, a.z);
		addDebugLine(a, b, color, life);
		a.set(b.x, b.y, min.z);
		addDebugLine(a, b, color, life);

		a = min;
		b = a;
		b.y = max.y;
		addDebugLine(a, b, color, life);
		a.x = max.x;
		b.x = max.x;
		addDebugLine(a, b, color, life);
		a.z = max.z;
		b.z = max.z;
		addDebugLine(a, b, color, life);
		a.x = min.x;
		b.x = min.x;
		addDebugLine(a, b, color, life);
	}


	void addDebugFrustum(const Frustum& frustum, uint32 color, float life) override
	{
		addDebugFrustum(frustum.getPosition(),
						frustum.getDirection(),
						frustum.getUp(),
						frustum.getFOV(),
						frustum.getRatio(),
						frustum.getNearDistance(),
						frustum.getFarDistance(),
						color,
						life);
	}


	void addDebugFrustum(const Vec3& position,
								 const Vec3& direction,
								 const Vec3& up,
								 float fov,
								 float ratio,
								 float near_distance,
								 float far_distance,
								 uint32 color,
								 float life) override
	{
		Vec3 points[8];
		Vec3 near_center = position + direction * near_distance;
		Vec3 far_center = position + direction * far_distance;
		Vec3 right = crossProduct(direction, up);
		float scale = (float)tan(Math::degreesToRadians(fov * 0.5f));
		Vec3 up_near = up * near_distance * scale;
		Vec3 right_near = right * (near_distance * scale * ratio);

		points[0] = near_center + up_near + right_near;
		points[1] = near_center + up_near - right_near;
		points[2] = near_center - up_near - right_near;
		points[3] = near_center - up_near + right_near;

		Vec3 up_far = up  * far_distance * scale;
		Vec3 right_far = right * (far_distance * scale * ratio);

		points[4] = far_center + up_far + right_far;
		points[5] = far_center + up_far - right_far;
		points[6] = far_center - up_far - right_far;
		points[7] = far_center - up_far + right_far;

		addDebugLine(points[0], points[1], color, life);
		addDebugLine(points[1], points[2], color, life);
		addDebugLine(points[2], points[3], color, life);
		addDebugLine(points[3], points[0], color, life);

		addDebugLine(points[4], points[5], color, life);
		addDebugLine(points[5], points[6], color, life);
		addDebugLine(points[6], points[7], color, life);
		addDebugLine(points[7], points[4], color, life);

		addDebugLine(points[0], points[4], color, life);
		addDebugLine(points[1], points[5], color, life);
		addDebugLine(points[2], points[6], color, life);
		addDebugLine(points[3], points[7], color, life);
	}

	void addDebugCircle(const Vec3& center,
		const Vec3& up,
		float radius,
		uint32 color,
		float life) override
	{
		Vec3 z_vec(-up.y, up.x, 0);
		Vec3 x_vec = crossProduct(up, z_vec);
		float prevx = radius;
		float prevz = 0;
		z_vec.normalize();
		x_vec.normalize();
		for (int i = 1; i <= 64; ++i)
		{
			float a = i / 64.0f * 2 * Math::PI;
			float x = cosf(a) * radius;
			float z = sinf(a) * radius;
			addDebugLine(center + x_vec * x + z_vec * z,
				center + x_vec * prevx + z_vec * prevz,
				color,
				life);
			prevx = x;
			prevz = z;
		}
	}

	void addDebugCross(const Vec3& center,
							   float size,
							   uint32 color,
							   float life) override
	{
		addDebugLine(
			center, Vec3(center.x - size, center.y, center.z), color, life);
		addDebugLine(
			center, Vec3(center.x + size, center.y, center.z), color, life);
		addDebugLine(
			center, Vec3(center.x, center.y - size, center.z), color, life);
		addDebugLine(
			center, Vec3(center.x, center.y + size, center.z), color, life);
		addDebugLine(
			center, Vec3(center.x, center.y, center.z - size), color, life);
		addDebugLine(
			center, Vec3(center.x, center.y, center.z + size), color, life);
	}


	void addDebugPoint(const Vec3& pos, uint32 color, float life) override
	{
		DebugPoint& point = m_debug_points.pushEmpty();
		point.m_pos = pos;
		point.m_color = ARGBToABGR(color);
		point.m_life = life;
	}


	static uint32 ARGBToABGR(uint32 color)
	{
		return ((color & 0xff) << 16) | (color & 0xff00) | ((color & 0xff0000) >> 16) |
			   (color & 0xff000000);
	}


	void addDebugLine(const Vec3& from, const Vec3& to, uint32 color, float life) override
	{
		DebugLine& line = m_debug_lines.pushEmpty();
		line.m_from = from;
		line.m_to = to;
		line.m_color = ARGBToABGR(color);
		line.m_life = life;
	}


	RayCastModelHit castRayTerrain(ComponentIndex terrain,
		const Vec3& origin,
		const Vec3& dir) override
	{
		RayCastModelHit hit;
		hit.m_is_hit = false;
		if (m_terrains[terrain])
		{
			hit = m_terrains[terrain]->castRay(origin, dir);
			hit.m_component = terrain;
			hit.m_component_type = TERRAIN_HASH;
			hit.m_entity = m_terrains[terrain]->getEntity();
		}
		return hit;
	}


	RayCastModelHit castRay(const Vec3& origin,
									const Vec3& dir,
									ComponentIndex ignored_renderable) override
	{
		RayCastModelHit hit;
		hit.m_is_hit = false;
		Universe& universe = getUniverse();
		int ignore_index = getRenderable(ignored_renderable);
		for (int i = 0; i < m_renderables.size(); ++i)
		{
			if (ignore_index != i && m_renderables[i]->m_model)
			{
				const Vec3& pos = m_renderables[i]->m_matrix.getTranslation();
				float radius = m_renderables[i]->m_model->getBoundingRadius();
				float scale = universe.getScale(m_renderables[i]->m_entity);
				Vec3 intersection;
				if (dotProduct(pos - origin, pos - origin) < radius * radius ||
					Math::getRaySphereIntersection(
						origin, dir, pos, radius * scale, intersection))
				{
					RayCastModelHit new_hit =
						m_renderables[i]->m_model->castRay(
							origin, dir, m_renderables[i]->m_matrix);
					if (new_hit.m_is_hit &&
						(!hit.m_is_hit || new_hit.m_t < hit.m_t))
					{
						new_hit.m_component = i;
						new_hit.m_entity = m_renderables[i]->m_entity;
						new_hit.m_component_type = RENDERABLE_HASH;
						hit = new_hit;
						hit.m_is_hit = true;
					}
				}
			}
		}
		for (int i = 0; i < m_terrains.size(); ++i)
		{
			if (m_terrains[i])
			{
				RayCastModelHit terrain_hit =
					m_terrains[i]->castRay(origin, dir);
				if (terrain_hit.m_is_hit &&
					(!hit.m_is_hit || terrain_hit.m_t < hit.m_t))
				{
					terrain_hit.m_component = i;
					terrain_hit.m_component_type = TERRAIN_HASH;
					terrain_hit.m_entity = m_terrains[i]->getEntity();
					hit = terrain_hit;
				}
			}
		}
		return hit;
	}


	int getPointLightIndex(ComponentIndex cmp) const
	{
		for (int i = 0; i < m_point_lights.size(); ++i)
		{
			if (m_point_lights[i].m_uid == cmp)
			{
				return i;
			}
		}
		return -1;
	}


	Vec4 getShadowmapCascades(ComponentIndex cmp) override
	{
		return m_global_lights[getGlobalLightIndex(cmp)].m_cascades;
	}


	void setShadowmapCascades(ComponentIndex cmp,
									  const Vec4& value) override
	{
		Vec4 valid_value = value;
		valid_value.x = Math::maxValue(valid_value.x, 0.02f);
		valid_value.y = Math::maxValue(valid_value.x + 0.01f, valid_value.y);
		valid_value.z = Math::maxValue(valid_value.y + 0.01f, valid_value.z);
		valid_value.w = Math::maxValue(valid_value.z + 0.01f, valid_value.w);

		m_global_lights[getGlobalLightIndex(cmp)].m_cascades = valid_value;
	}


	int getGlobalLightIndex(int uid) const
	{
		for (int i = 0; i < m_global_lights.size(); ++i)
		{
			if (m_global_lights[i].m_uid == uid)
			{
				return i;
			}
		}
		return -1;
	}


	void setFogDensity(ComponentIndex cmp, float density) override
	{
		m_global_lights[getGlobalLightIndex(cmp)].m_fog_density = density;
	}

	void setFogColor(ComponentIndex cmp, const Vec3& color) override
	{
		m_global_lights[getGlobalLightIndex(cmp)].m_fog_color = color;
	}

	float getFogDensity(ComponentIndex cmp) override
	{
		return m_global_lights[getGlobalLightIndex(cmp)].m_fog_density;
	}

	float getFogBottom(ComponentIndex cmp) override
	{
		return m_global_lights[getGlobalLightIndex(cmp)].m_fog_bottom;
	}

	void setFogBottom(ComponentIndex cmp, float bottom) override
	{
		m_global_lights[getGlobalLightIndex(cmp)].m_fog_bottom = bottom;
	}

	float getFogHeight(ComponentIndex cmp) override
	{
		return m_global_lights[getGlobalLightIndex(cmp)].m_fog_height;
	}

	void setFogHeight(ComponentIndex cmp, float height) override
	{
		m_global_lights[getGlobalLightIndex(cmp)].m_fog_height = height;
	}

	Vec3 getFogColor(ComponentIndex cmp) override
	{
		return m_global_lights[getGlobalLightIndex(cmp)].m_fog_color;
	}

	float getLightAttenuation(ComponentIndex cmp) override
	{
		return m_point_lights[getPointLightIndex(cmp)].m_attenuation_param;
	}

	void setLightAttenuation(ComponentIndex cmp,
									 float attenuation) override
	{
		int index = getPointLightIndex(cmp);
		m_point_lights[index].m_attenuation_param = attenuation;
	}

	float getLightRange(ComponentIndex cmp) override
	{
		return m_point_lights[getPointLightIndex(cmp)].m_range;
	}

	void setLightRange(ComponentIndex cmp, float value) override
	{
		m_point_lights[getPointLightIndex(cmp)].m_range = value;
	}

	void setPointLightIntensity(ComponentIndex cmp,
										float intensity) override
	{
		m_point_lights[getPointLightIndex(cmp)].m_intensity = intensity;
	}

	void setGlobalLightIntensity(ComponentIndex cmp,
										 float intensity) override
	{
		m_global_lights[getGlobalLightIndex(cmp)].m_intensity = intensity;
	}

	void setPointLightColor(ComponentIndex cmp,
									const Vec3& color) override
	{
		m_point_lights[getPointLightIndex(cmp)].m_diffuse_color = color;
	}

	void setGlobalLightColor(ComponentIndex cmp,
									 const Vec3& color) override
	{
		m_global_lights[getGlobalLightIndex(cmp)].m_color = color;
	}

	void setLightAmbientIntensity(ComponentIndex cmp,
										  float intensity) override
	{
		m_global_lights[getGlobalLightIndex(cmp)].m_ambient_intensity =
			intensity;
	}

	void setLightAmbientColor(ComponentIndex cmp,
									  const Vec3& color) override
	{
		m_global_lights[getGlobalLightIndex(cmp)].m_ambient_color = color;
	}

	float getPointLightIntensity(ComponentIndex cmp) override
	{
		return m_point_lights[getPointLightIndex(cmp)].m_intensity;
	}

	float getGlobalLightIntensity(ComponentIndex cmp) override
	{
		return m_global_lights[getGlobalLightIndex(cmp)].m_intensity;
	}

	Vec3 getPointLightColor(ComponentIndex cmp) override
	{
		return m_point_lights[getPointLightIndex(cmp)].m_diffuse_color;
	}

	void setPointLightSpecularColor(ComponentIndex cmp,
											const Vec3& color) override
	{
		m_point_lights[getPointLightIndex(cmp)].m_specular_color = color;
	}

	Vec3 getPointLightSpecularColor(ComponentIndex cmp) override
	{
		return m_point_lights[getPointLightIndex(cmp)].m_specular_color;
	}

	Vec3 getGlobalLightColor(ComponentIndex cmp) override
	{
		return m_global_lights[getGlobalLightIndex(cmp)].m_color;
	}

	float getLightAmbientIntensity(ComponentIndex cmp) override
	{
		return m_global_lights[getGlobalLightIndex(cmp)].m_ambient_intensity;
	}

	Vec3 getLightAmbientColor(ComponentIndex cmp) override
	{
		return m_global_lights[getGlobalLightIndex(cmp)].m_ambient_color;
	}

	void setActiveGlobalLight(ComponentIndex cmp) override
	{
		ASSERT(cmp == GLOBAL_LIGHT_HASH);
		m_active_global_light_uid = cmp;
	}

	ComponentIndex getActiveGlobalLight() override
	{
		return m_active_global_light_uid;
	};


	Entity getPointLightEntity(ComponentIndex cmp) const override
	{
		return m_point_lights[getPointLightIndex(cmp)].m_entity;
	}


	Entity getGlobalLightEntity(ComponentIndex cmp) const override
	{
		return m_global_lights[getGlobalLightIndex(cmp)].m_entity;
	}


	ComponentIndex getCameraInSlot(const char* slot) override
	{
		for (int i = 0, c = m_cameras.size(); i < c; ++i)
		{
			if (!m_cameras[i].m_is_free &&
				strcmp(m_cameras[i].m_slot, slot) == 0)
			{
				return i;
			}
		}
		return INVALID_COMPONENT;
	}

	float getTime() const override { return m_time; }


private:
	void modelLoaded(Model* model, int renderable_index)
	{
		float bounding_radius =
			m_renderables[renderable_index]->m_model->getBoundingRadius();
		float scale =
			m_universe.getScale(m_renderables[renderable_index]->m_entity);
		m_culling_system->updateBoundingRadius(bounding_radius * scale,
											   renderable_index);
		m_renderables[renderable_index]->m_meshes.clear();
		m_renderables[renderable_index]->m_pose.resize(model->getBoneCount());
		model->getPose(m_renderables[renderable_index]->m_pose);
		for (int j = 0; j < model->getMeshCount(); ++j)
		{
			RenderableMesh& info =
				m_renderables[renderable_index]->m_meshes.pushEmpty();
			info.m_mesh = &model->getMesh(j);
			info.m_pose = &m_renderables[renderable_index]->m_pose;
			info.m_matrix = &m_renderables[renderable_index]->m_matrix;
			info.m_model = model;
		}

		for (int i = 0; i < m_point_lights.size(); ++i)
		{
			PointLight& light = m_point_lights[i];
			Renderable* renderable = m_renderables[renderable_index];
			Vec3 t = renderable->m_matrix.getTranslation();
			float r = renderable->m_model->getBoundingRadius();
			if ((t - m_universe.getPosition(light.m_entity)).squaredLength() <
				(r + light.m_range) * (r + light.m_range))
			{
				m_light_influenced_geometry[i].push(renderable_index);
			}
		}
	}

	void modelLoaded(Model* model)
	{
		for (int i = 0; i < m_renderables.size(); ++i)
		{
			if (m_renderables[i]->m_model == model)
			{
				modelLoaded(model, i);
			}
		}
	}


	ModelLoadedCallback* getModelLoadedCallback(Model* model)
	{
		for (int i = 0; i < m_model_loaded_callbacks.size(); ++i)
		{
			if (m_model_loaded_callbacks[i]->m_model == model)
			{
				return m_model_loaded_callbacks[i];
			}
		}
		ModelLoadedCallback* new_callback =
			LUMIX_NEW(m_allocator, ModelLoadedCallback)(*this, model);
		m_model_loaded_callbacks.push(new_callback);
		return new_callback;
	}


	void setModel(int renderable_index, Model* model)
	{
		Model* old_model = m_renderables[renderable_index]->m_model;
		if (model == old_model && old_model)
		{
			old_model->getResourceManager().get(ResourceManager::MODEL)->unload(*old_model);
			return;
		}
		if (old_model)
		{
			ModelLoadedCallback* callback = getModelLoadedCallback(old_model);
			--callback->m_ref_count;
			old_model->getResourceManager().get(ResourceManager::MODEL)->unload(*old_model);
		}
		m_renderables[renderable_index]->m_model = model;
		m_renderables[renderable_index]->m_meshes.clear();
		if (model)
		{
			ModelLoadedCallback* callback = getModelLoadedCallback(model);
			++callback->m_ref_count;

			if (model->isReady())
			{
				modelLoaded(model, renderable_index);
			}
		}
	}

	IAllocator& getAllocator() override { return m_allocator; }


	void detectLightInfluencedGeometry(int light_index)
	{
		if (!m_is_forward_rendered) return;
		
		Frustum frustum = getPointLightFrustum(light_index);
		m_culling_system->cullToFrustum(frustum, 0xffffFFFF);
		const CullingSystem::Results& results =
			m_culling_system->getResult();
		Array<int>& influenced_geometry =
			m_light_influenced_geometry[light_index];
		influenced_geometry.clear();
		for (int i = 0; i < results.size(); ++i)
		{
			const CullingSystem::Subresults& subresult = results[i];
			influenced_geometry.reserve(influenced_geometry.size() +
										subresult.size());
			for (int j = 0, c = subresult.size(); j < c; ++j)
			{
				influenced_geometry.push(subresult[j]);
			}
		}
	}


	DelegateList<void(ComponentIndex)>& renderableCreated() override
	{
		return m_renderable_created;
	}


	DelegateList<void(ComponentIndex)>& renderableDestroyed() override
	{
		return m_renderable_destroyed;
	}


	float getLightFOV(ComponentIndex cmp) override
	{
		return m_point_lights[getPointLightIndex(cmp)].m_fov;
	}


	void setLightFOV(ComponentIndex cmp, float fov) override
	{
		m_point_lights[getPointLightIndex(cmp)].m_fov = fov;
	}


	ComponentIndex createGlobalLight(Entity entity)
	{
		GlobalLight& light = m_global_lights.pushEmpty();
		light.m_entity = entity;
		light.m_color.set(1, 1, 1);
		light.m_intensity = 0;
		light.m_ambient_color.set(1, 1, 1);
		light.m_ambient_intensity = 1;
		light.m_fog_color.set(1, 1, 1);
		light.m_fog_density = 0;
		light.m_uid = ++m_global_light_last_uid;
		light.m_cascades.set(3, 8, 100, 300);
		light.m_fog_bottom = 0.0f;
		light.m_fog_height = 10.0f;

		if (m_global_lights.size() == 1)
		{
			m_active_global_light_uid = light.m_uid;
		}

		m_universe.addComponent(entity, GLOBAL_LIGHT_HASH, this, light.m_uid);
		return light.m_uid;
	}


	ComponentIndex createPointLight(Entity entity)
	{
		PointLight& light = m_point_lights.pushEmpty();
		m_light_influenced_geometry.push(Array<int>(m_allocator));
		light.m_entity = entity;
		light.m_diffuse_color.set(1, 1, 1);
		light.m_intensity = 1;
		light.m_uid = ++m_point_light_last_uid;
		light.m_fov = 999;
		light.m_specular_color.set(1, 1, 1);
		light.m_cast_shadows = false;
		light.m_attenuation_param = 2;
		light.m_range = 10;

		m_universe.addComponent(entity, POINT_LIGHT_HASH, this, light.m_uid);

		detectLightInfluencedGeometry(m_point_lights.size() - 1);

		return light.m_uid;
	}


	ComponentIndex createRenderable(Entity entity)
	{
		int new_index = m_renderables.empty() ? 0 : m_renderables.back()->m_component_index + 1;
		Renderable& r = *LUMIX_NEW(m_allocator, Renderable)(m_allocator);
		m_renderables.push(&r);
		r.m_entity = entity;
		r.m_model = nullptr;
		r.m_component_index = new_index;
		r.m_matrix = m_universe.getMatrix(entity);
		m_universe.addComponent(entity, RENDERABLE_HASH, this, r.m_component_index);
		m_culling_system->addStatic(Sphere(m_universe.getPosition(entity), 1.0f));
		m_renderable_created.invoke(r.m_component_index);
		return r.m_component_index;
	}


	void setParticleEmitterMaterialPath(ComponentIndex cmp, const char* path) override
	{
		if (!m_particle_emitters[cmp]) return;

		auto* manager = m_engine.getResourceManager().get(ResourceManager::MATERIAL);
		Material* material = static_cast<Material*>(manager->load(Path(path)));
		m_particle_emitters[cmp]->setMaterial(material);
	}


	const char* getParticleEmitterMaterialPath(ComponentIndex cmp) override
	{
		ParticleEmitter* emitter = m_particle_emitters[cmp];
		if (!emitter) return "";
		if (!emitter->getMaterial()) return "";

		return emitter->getMaterial()->getPath().c_str();
	}


	ParticleEmitter& getParticleEmitter(ComponentIndex cmp) const override
	{
		return *m_particle_emitters[cmp];
	}


	const Array<ParticleEmitter*>& getParticleEmitters() const override
	{
		return m_particle_emitters;
	}


private:
	IAllocator& m_allocator;
	Array<ModelLoadedCallback*> m_model_loaded_callbacks;

	Array<Renderable*> m_renderables;

	int m_point_light_last_uid;
	Array<PointLight> m_point_lights;
	Array<Array<int>> m_light_influenced_geometry;
	int m_active_global_light_uid;
	int m_global_light_last_uid;
	Array<GlobalLight> m_global_lights;

	Array<Camera> m_cameras;

	Array<Terrain*> m_terrains;
	Universe& m_universe;
	Renderer& m_renderer;
	Engine& m_engine;
	Array<DebugLine> m_debug_lines;
	Array<DebugPoint> m_debug_points;
	CullingSystem* m_culling_system;
	DynamicRenderableCache m_dynamic_renderable_cache;
	Array<ParticleEmitter*> m_particle_emitters;
	Array<Array<const RenderableMesh*>> m_temporary_infos;
	MTJD::Group m_sync_point;
	Array<MTJD::Job*> m_jobs;
	float m_time;
	bool m_is_forward_rendered;
	bool m_is_grass_enabled;
	DelegateList<void(ComponentIndex)> m_renderable_created;
	DelegateList<void(ComponentIndex)> m_renderable_destroyed;
};


RenderScene* RenderScene::createInstance(Renderer& renderer,
										 Engine& engine,
										 Universe& universe,
										 bool is_forward_rendered,
										 IAllocator& allocator)
{
	return LUMIX_NEW(allocator, RenderSceneImpl)(
		renderer, engine, universe, is_forward_rendered, allocator);
}


void RenderScene::destroyInstance(RenderScene* scene)
{
	LUMIX_DELETE(scene->getAllocator(), static_cast<RenderSceneImpl*>(scene));
}
}