#include "render_scene.h"

#include "core/array.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/FS/file_system.h"
#include "core/FS/ifile.h"
#include "core/geometry.h"
#include "core/json_serializer.h"
#include "core/lifo_allocator.h"
#include "core/log.h"
#include "core/lua_wrapper.h"
#include "core/math_utils.h"
#include "core/mtjd/generic_job.h"
#include "core/mtjd/job.h"
#include "core/mtjd/manager.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/timer.h"

#include "engine.h"

#include "lua_script/lua_script_system.h"

#include "renderer/culling_system.h"
#include "renderer/material.h"
#include "renderer/material_manager.h"
#include "renderer/model.h"
#include "renderer/particle_system.h"
#include "renderer/pipeline.h"
#include "renderer/pose.h"
#include "renderer/ray_cast_model_hit.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"

#include "universe/universe.h"
#include <cmath>


namespace Lumix
{


static const uint32 RENDERABLE_HASH = crc32("renderable");
static const uint32 RENDER_PARAMS_HASH = crc32("render_params");
static const uint32 POINT_LIGHT_HASH = crc32("point_light");
static const uint32 PARTICLE_EMITTER_HASH = crc32("particle_emitter");
static const uint32 PARTICLE_EMITTER_FADE_HASH = crc32("particle_emitter_fade");
static const uint32 PARTICLE_EMITTER_FORCE_HASH = crc32("particle_emitter_force");
static const uint32 PARTICLE_EMITTER_ATTRACTOR_HASH = crc32("particle_emitter_attractor");
static const uint32 PARTICLE_EMITTER_LINEAR_MOVEMENT_HASH =
	crc32("particle_emitter_linear_movement");
static const uint32 PARTICLE_EMITTER_SPAWN_SHAPE_HASH = crc32("particle_emitter_spawn_shape");
static const uint32 PARTICLE_EMITTER_PLANE_HASH = crc32("particle_emitter_plane");
static const uint32 PARTICLE_EMITTER_RANDOM_ROTATION_HASH =
	crc32("particle_emitter_random_rotation");
static const uint32 PARTICLE_EMITTER_SIZE_HASH = crc32("particle_emitter_size");
static const uint32 GLOBAL_LIGHT_HASH = crc32("global_light");
static const uint32 CAMERA_HASH = crc32("camera");
static const uint32 TERRAIN_HASH = crc32("terrain");


enum class RenderSceneVersion : int32
{
	PARTICLES,
	WHOLE_LIGHTS,
	PARTICLE_EMITTERS_SPAWN_COUNT,
	PARTICLES_FORCE_MODULE,
	PARTICLES_SAVE_SIZE_ALPHA,
	RENDERABLE_MATERIALS,
	GLOBAL_LIGHT_SPECULAR,
	SPECULAR_INTENSITY,
	RENDER_PARAMS,
	RENDER_PARAMS_REMOVED,

	LATEST,
	INVALID = -1,
};


struct PointLight
{
	Vec3 m_diffuse_color;
	Vec3 m_specular_color;
	float m_diffuse_intensity;
	float m_specular_intensity;
	Entity m_entity;
	int m_uid;
	float m_fov;
	float m_attenuation_param;
	float m_range;
	bool m_cast_shadows;
};


struct GlobalLight
{
	ComponentIndex m_uid;
	Vec3 m_diffuse_color;
	float m_specular_intensity;
	Vec3 m_specular;
	float m_diffuse_intensity;
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
	class ModelLoadedCallback
	{
	public:
		ModelLoadedCallback(RenderSceneImpl& scene, Model* model)
			: m_scene(scene)
			, m_ref_count(0)
			, m_model(model)
		{
			m_model->getObserverCb().bind<ModelLoadedCallback, &ModelLoadedCallback::callback>(
				this);
		}

		~ModelLoadedCallback()
		{
			m_model->getObserverCb().unbind<ModelLoadedCallback, &ModelLoadedCallback::callback>(
				this);
		}

		void callback(Resource::State old_state, Resource::State new_state)
		{
			if (new_state == Resource::State::READY)
			{
				m_scene.modelLoaded(m_model);
			}
			else if (old_state == Resource::State::READY && new_state != Resource::State::READY)
			{
				m_scene.modelUnloaded(m_model);
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
		, m_is_game_running(false)
		, m_particle_emitters(m_allocator)
		, m_point_lights_map(m_allocator)
	{
		m_universe.entityTransformed()
			.bind<RenderSceneImpl, &RenderSceneImpl::onEntityMoved>(this);
		m_culling_system =
			CullingSystem::create(m_engine.getMTJDManager(), m_allocator);
		m_time = 0;
		m_renderables.reserve(5000);
	}


	~RenderSceneImpl()
	{
		auto& rm = m_engine.getResourceManager();
		auto* material_manager = static_cast<MaterialManager*>(rm.get(ResourceManager::MATERIAL));

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

		for (auto& i : m_renderables)
		{
			if (i.entity != INVALID_ENTITY && i.model)
			{
				auto& manager = i.model->getResourceManager();
				freeCustomMeshes(i, material_manager);
				manager.get(ResourceManager::MODEL)->unload(*i.model);
				LUMIX_DELETE(m_allocator, i.pose);
			}
		}

		CullingSystem::destroy(*m_culling_system);
	}


	void resetParticleEmitter(ComponentIndex cmp) override
	{
		m_particle_emitters[cmp]->reset();
	}


	ParticleEmitter* getParticleEmitter(ComponentIndex cmp)
	{
		return m_particle_emitters[cmp];
	}


	void updateEmitter(ComponentIndex cmp, float time_delta) override
	{
		m_particle_emitters[cmp]->update(time_delta);
	}


	Universe& getUniverse() override { return m_universe; }


	bool ownComponentType(uint32 type) const override
	{
		return type == RENDERABLE_HASH || type == POINT_LIGHT_HASH ||
			type == GLOBAL_LIGHT_HASH || type == CAMERA_HASH ||
			type == TERRAIN_HASH;
	}


	ComponentIndex getComponent(Entity entity, uint32 type) override
	{
		if (type == RENDERABLE_HASH)
		{
			if (entity >= m_renderables.size()) return INVALID_COMPONENT;
			return m_renderables[entity].entity != INVALID_ENTITY ? entity : INVALID_COMPONENT;
		}
		if (type == POINT_LIGHT_HASH)
		{
			for (auto& i : m_point_lights)
			{
				if (i.m_entity == entity) return i.m_uid;
			}
			return INVALID_COMPONENT;
		}
		if (type == GLOBAL_LIGHT_HASH)
		{
			for (auto& i : m_global_lights)
			{
				if (i.m_entity == entity) return i.m_uid;
			}
			return INVALID_COMPONENT;
		}
		if (type == CAMERA_HASH)
		{
			for (int i = 0; i < m_cameras.size(); ++i)
			{
				if (!m_cameras[i].m_is_free && m_cameras[i].m_entity == entity) return i;
			}
			return INVALID_COMPONENT;
		}
		if (type == TERRAIN_HASH)
		{
			for (int i = 0; i < m_terrains.size(); ++i)
			{
				if (m_terrains[i] && m_terrains[i]->getEntity() == entity) return i;
			}
			return INVALID_COMPONENT;
		}
		return INVALID_COMPONENT;
	}


	IPlugin& getPlugin() const override { return m_renderer; }


	Int2 getParticleEmitterSpawnCount(ComponentIndex cmp) override
	{
		Int2 ret;
		ret.x = m_particle_emitters[cmp]->m_spawn_count.from;
		ret.y = m_particle_emitters[cmp]->m_spawn_count.to;
		return ret;
	}


	void setParticleEmitterSpawnCount(ComponentIndex cmp, const Int2& value) override
	{
		m_particle_emitters[cmp]->m_spawn_count.from = value.x;
		m_particle_emitters[cmp]->m_spawn_count.to = Math::maximum(value.x, value.y);
	}



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


	void startGame() override
	{
		m_is_game_running = true;
	}


	void stopGame() override
	{
		m_is_game_running = false;
	}


	void update(float dt, bool paused) override
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

		if (m_is_game_running && !paused)
		{
			for (auto* emitter : m_particle_emitters)
			{
				if (!emitter) continue;

				emitter->update(dt);
			}
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
		for (auto& r : m_renderables)
		{
			serializer.write(r.entity);
			if(r.entity != INVALID_ENTITY)
			{
				serializer.write(r.layer_mask);
				serializer.write(r.model ? r.model->getPath().getHash() : 0);
				bool has_changed_materials = r.model && r.model->isReady() && r.meshes != &r.model->getMesh(0);
				serializer.write(has_changed_materials ? r.mesh_count : 0);
				if (has_changed_materials)
				{
					for (int i = 0; i < r.mesh_count; ++i)
					{
						serializer.writeString(r.meshes[i].material->getPath().c_str());
					}
				}
			}
			
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


	void deserializeParticleEmitters(InputBlob& serializer, int version)
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
				emitter->deserialize(serializer,
					m_engine.getResourceManager(),
					version > (int)RenderSceneVersion::PARTICLE_EMITTERS_SPAWN_COUNT);
				m_universe.addComponent(emitter->m_entity, PARTICLE_EMITTER_HASH, this, i);
				for (auto* module : emitter->m_modules)
				{
					if (module->getType() == ParticleEmitter::AlphaModule::s_type)
					{
						m_universe.addComponent(
							emitter->m_entity, PARTICLE_EMITTER_FADE_HASH, this, i);
					}
					else if (module->getType() == ParticleEmitter::ForceModule::s_type)
					{
						m_universe.addComponent(
							emitter->m_entity, PARTICLE_EMITTER_FORCE_HASH, this, i);
					}
					else if (module->getType() == ParticleEmitter::SpawnShapeModule::s_type)
					{
						m_universe.addComponent(
							emitter->m_entity, PARTICLE_EMITTER_SPAWN_SHAPE_HASH, this, i);
					}
					else if (module->getType() == ParticleEmitter::AttractorModule::s_type)
					{
						m_universe.addComponent(
							emitter->m_entity, PARTICLE_EMITTER_ATTRACTOR_HASH, this, i);
					}
					else if (module->getType() == ParticleEmitter::LinearMovementModule::s_type)
					{
						m_universe.addComponent(
							emitter->m_entity, PARTICLE_EMITTER_LINEAR_MOVEMENT_HASH, this, i);
					}
					else if (module->getType() == ParticleEmitter::PlaneModule::s_type)
					{
						m_universe.addComponent(
							emitter->m_entity, PARTICLE_EMITTER_PLANE_HASH, this, i);
					}
					else if (module->getType() == ParticleEmitter::RandomRotationModule::s_type)
					{
						m_universe.addComponent(
							emitter->m_entity, PARTICLE_EMITTER_RANDOM_ROTATION_HASH, this, i);
					}
					else if (module->getType() == ParticleEmitter::SizeModule::s_type)
					{
						m_universe.addComponent(
							emitter->m_entity, PARTICLE_EMITTER_SIZE_HASH, this, i);
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

	void deserializeRenderParams(InputBlob& serializer)
	{
		int dummy;
		serializer.read(dummy);
		int count;
		serializer.read(count);
		char tmp[32];
		bool any = false;
		for(int i = 0; i < count; ++i)
		{
			any = true;
			serializer.readString(tmp, lengthOf(tmp));
			float value;
			serializer.read(value);
		}
		serializer.read(count);
		for(int i = 0; i < count; ++i)
		{
			any = true;
			serializer.readString(tmp, lengthOf(tmp));
			Vec4 value;
			serializer.read(value);
		}
		if(any)
		{
			g_log_warning.log("Renderer") << "Render params are deprecated";
		}
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

	void deserializeRenderables(InputBlob& serializer, RenderSceneVersion version)
	{
		int32 size = 0;
		serializer.read(size);
		for (int i = 0; i < m_renderables.size(); ++i)
		{
			if (m_renderables[i].entity != INVALID_ENTITY)
			{
				setModel(i, nullptr);
			}
		}
		m_culling_system->clear();
		m_renderables.clear();
		m_renderables.reserve(size);
		for (int i = 0; i < size; ++i)
		{
			auto& r = m_renderables.emplace();
			serializer.read(r.entity);
			ASSERT(r.entity == i || r.entity == INVALID_ENTITY);
			r.model = nullptr;
			r.pose = nullptr;
			r.custom_meshes = false;
			r.meshes = nullptr;
			r.mesh_count = 0;

			if(r.entity != INVALID_ENTITY)
			{
				serializer.read(r.layer_mask);
				r.matrix = m_universe.getMatrix(r.entity);

				uint32 path;
				serializer.read(path);

				if (path != 0)
				{
					auto* model = static_cast<Model*>(m_engine.getResourceManager()
														  .get(ResourceManager::MODEL)
														  ->load(Path(path)));
					setModel(r.entity, model);
				}

				if (version > RenderSceneVersion::RENDERABLE_MATERIALS)
				{
					int material_count;
					serializer.read(material_count);
					if (material_count > 0)
					{
						allocateCustomMeshes(r, material_count);
						for (int j = 0; j < material_count; ++j)
						{
							char path[MAX_PATH_LENGTH];
							serializer.readString(path, lengthOf(path));
							Material* material = static_cast<Material*>(
								m_engine.getResourceManager().get(ResourceManager::MATERIAL)->load(Path(path)));
							r.meshes[j].material = material;
						}
					}
				}

				m_universe.addComponent(r.entity, RENDERABLE_HASH, this, r.entity);
			}
		}
	}

	void deserializeLights(InputBlob& serializer, RenderSceneVersion version)
	{
		int32 size = 0;
		serializer.read(size);
		m_point_lights_map.clear();
		m_point_lights.resize(size);
		m_light_influenced_geometry.clear();
		for (int i = 0; i < size; ++i)
		{
			m_light_influenced_geometry.push(Array<int>(m_allocator));
			PointLight& light = m_point_lights[i];
			if (version > RenderSceneVersion::SPECULAR_INTENSITY)
			{
				serializer.read(light);
			}
			else
			{
				serializer.read(light.m_diffuse_color);
				serializer.read(light.m_specular_color);
				serializer.read(light.m_diffuse_intensity);
				serializer.read(light.m_entity);
				serializer.read(light.m_uid);
				serializer.read(light.m_fov);
				serializer.read(light.m_attenuation_param);
				serializer.read(light.m_range);
				serializer.read(light.m_cast_shadows);
				uint8 padding;
				for(int j = 0; j < 3; ++j) serializer.read(padding);
				light.m_specular_intensity = 1;
			}
			m_point_lights_map.insert(light.m_uid, i);

			m_universe.addComponent(light.m_entity, POINT_LIGHT_HASH, this, light.m_uid);
		}
		serializer.read(m_point_light_last_uid);

		serializer.read(size);
		m_global_lights.resize(size);
		for (int i = 0; i < size; ++i)
		{
			GlobalLight& light = m_global_lights[i];
			light.m_specular.set(0, 0, 0);
			if (version > RenderSceneVersion::SPECULAR_INTENSITY)
			{
				serializer.read(light);
			}
			else
			{
				serializer.read(light.m_uid);
				serializer.read(light.m_diffuse_color);
				serializer.read(light.m_specular);
				serializer.read(light.m_diffuse_intensity);
				serializer.read(light.m_ambient_color);
				serializer.read(light.m_ambient_intensity);
				serializer.read(light.m_fog_color);
				serializer.read(light.m_fog_density);
				serializer.read(light.m_fog_bottom);
				serializer.read(light.m_fog_height);
				serializer.read(light.m_entity);
				serializer.read(light.m_cascades);
				light.m_specular_intensity = 1;
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
		deserializeRenderables(serializer, (RenderSceneVersion)version);
		deserializeLights(serializer, (RenderSceneVersion)version);
		deserializeTerrains(serializer);
		if (version >= 0) deserializeParticleEmitters(serializer, version);
		if (version >= (int)RenderSceneVersion::RENDER_PARAMS &&
			version < (int)RenderSceneVersion::RENDER_PARAMS_REMOVED)
		{
			deserializeRenderParams(serializer);
		}
	}


	void destroyRenderable(ComponentIndex component)
	{
		m_renderable_destroyed.invoke(component);
		for (int i = 0; i < m_light_influenced_geometry.size(); ++i)
		{
			Array<int>& influenced_geometry = m_light_influenced_geometry[i];
			for (int j = 0; j < influenced_geometry.size(); ++j)
			{
				if (influenced_geometry[j] == component)
				{
					influenced_geometry.erase(j);
					break;
				}
			}
		}

		setModel(component, nullptr);
		Entity entity = m_renderables[component].entity;
		LUMIX_DELETE(m_allocator, m_renderables[component].pose);
		m_renderables[component].pose = nullptr;
		m_renderables[component].entity = INVALID_ENTITY;
		m_universe.destroyComponent(entity, RENDERABLE_HASH, this, component);
	}


	void destroyGlobalLight(ComponentIndex component)
	{
		Entity entity = m_global_lights[getGlobalLightIndex(component)].m_entity;

		m_universe.destroyComponent(entity, GLOBAL_LIGHT_HASH, this, component);

		if (component == m_active_global_light_uid)
		{
			m_active_global_light_uid = -1;
		}
		m_global_lights.eraseFast(getGlobalLightIndex(component));
	}


	void destroyPointLight(ComponentIndex component)
	{
		int index = getPointLightIndex(component);
		Entity entity = m_point_lights[getPointLightIndex(component)].m_entity;
		m_point_lights.eraseFast(index);
		m_point_lights_map.erase(component);
		m_light_influenced_geometry.eraseFast(index);
		m_universe.destroyComponent(entity, POINT_LIGHT_HASH, this, component);
	}


	void destroyCamera(ComponentIndex component)
	{
		Entity entity = m_cameras[component].m_entity;
		m_cameras[component].m_is_free = true;
		m_universe.destroyComponent(entity, CAMERA_HASH, this, component);
	}


	void destroyTerrain(ComponentIndex component)
	{
		Entity entity = m_terrains[component]->getEntity();
		LUMIX_DELETE(m_allocator, m_terrains[component]);
		m_terrains[component] = nullptr;
		m_universe.destroyComponent(entity, TERRAIN_HASH, this, component);
	}


	void destroyParticleEmitter(ComponentIndex component)
	{
		Entity entity = m_particle_emitters[component]->m_entity;
		LUMIX_DELETE(m_allocator, m_particle_emitters[component]);
		m_particle_emitters[component] = nullptr;
		m_universe.destroyComponent(entity, PARTICLE_EMITTER_HASH, this, component);
	}


	void destroyParticleEmitterFade(ComponentIndex component)
	{
		auto* emitter = m_particle_emitters[component];
		for (auto* module : emitter->m_modules)
		{
			if (module->getType() == ParticleEmitter::AlphaModule::s_type)
			{
				LUMIX_DELETE(m_allocator, module);
				emitter->m_modules.eraseItem(module);
				m_universe.destroyComponent(emitter->m_entity, PARTICLE_EMITTER_FADE_HASH, this, component);
				break;
			}
		}
	}


	void destroyParticleEmitterForce(ComponentIndex component)
	{
		auto* emitter = m_particle_emitters[component];
		for(auto* module : emitter->m_modules)
		{
			if(module->getType() == ParticleEmitter::ForceModule::s_type)
			{
				LUMIX_DELETE(m_allocator, module);
				emitter->m_modules.eraseItem(module);
				m_universe.destroyComponent(emitter->m_entity, PARTICLE_EMITTER_FORCE_HASH, this, component);
				break;
			}
		}
	}


	void destroyParticleEmitterAttractor(ComponentIndex component)
	{
		auto* emitter = m_particle_emitters[component];
		for(auto* module : emitter->m_modules)
		{
			if(module->getType() == ParticleEmitter::AttractorModule::s_type)
			{
				LUMIX_DELETE(m_allocator, module);
				emitter->m_modules.eraseItem(module);
				m_universe.destroyComponent(emitter->m_entity, PARTICLE_EMITTER_ATTRACTOR_HASH, this, component);
				break;
			}
		}
	}


	void destroyParticleEmitterSize(ComponentIndex component)
	{
		auto* emitter = m_particle_emitters[component];
		for (auto* module : emitter->m_modules)
		{
			if (module->getType() == ParticleEmitter::SizeModule::s_type)
			{
				LUMIX_DELETE(m_allocator, module);
				emitter->m_modules.eraseItem(module);
				m_universe.destroyComponent(emitter->m_entity, PARTICLE_EMITTER_SIZE_HASH, this, component);
				break;
			}
		}
	}


	float getParticleEmitterPlaneBounce(ComponentIndex cmp) override
	{
		auto* emitter = m_particle_emitters[cmp];
		for(auto* module : emitter->m_modules)
		{
			if(module->getType() == ParticleEmitter::PlaneModule::s_type)
			{
				return static_cast<ParticleEmitter::PlaneModule*>(module)->m_bounce;
			}
		}
		return 0;
	}


	void setParticleEmitterPlaneBounce(ComponentIndex cmp, float value) override
	{
		auto* emitter = m_particle_emitters[cmp];
		for(auto* module : emitter->m_modules)
		{
			if(module->getType() == ParticleEmitter::PlaneModule::s_type)
			{
				static_cast<ParticleEmitter::PlaneModule*>(module)->m_bounce = value;
				break;
			}
		}
	}


	float getParticleEmitterAttractorForce(ComponentIndex cmp) override
	{
		auto* emitter = m_particle_emitters[cmp];
		for(auto* module : emitter->m_modules)
		{
			if(module->getType() == ParticleEmitter::AttractorModule::s_type)
			{
				return static_cast<ParticleEmitter::AttractorModule*>(module)->m_force;
			}
		}
		return 0;
	}


	void setParticleEmitterAttractorForce(ComponentIndex cmp, float value) override
	{
		auto* emitter = m_particle_emitters[cmp];
		for(auto* module : emitter->m_modules)
		{
			if(module->getType() == ParticleEmitter::AttractorModule::s_type)
			{
				static_cast<ParticleEmitter::AttractorModule*>(module)->m_force = value;
				break;
			}
		}
	}


	void destroyParticleEmitterPlane(ComponentIndex component)
	{
		auto* emitter = m_particle_emitters[component];
		for (auto* module : emitter->m_modules)
		{
			if (module->getType() == ParticleEmitter::PlaneModule::s_type)
			{
				LUMIX_DELETE(m_allocator, module);
				emitter->m_modules.eraseItem(module);
				m_universe.destroyComponent(
					emitter->m_entity, PARTICLE_EMITTER_PLANE_HASH, this, component);
				break;
			}
		}
	}


	void destroyParticleEmitterLinearMovement(ComponentIndex component)
	{
		auto* emitter = m_particle_emitters[component];
		for (auto* module : emitter->m_modules)
		{
			if (module->getType() == ParticleEmitter::LinearMovementModule::s_type)
			{
				LUMIX_DELETE(m_allocator, module);
				emitter->m_modules.eraseItem(module);
				m_universe.destroyComponent(
					emitter->m_entity, PARTICLE_EMITTER_LINEAR_MOVEMENT_HASH, this, component);
				break;
			}
		}
	}


	void destroyParticleEmitterSpawnShape(ComponentIndex component)
	{
		auto* emitter = m_particle_emitters[component];
		for (auto* module : emitter->m_modules)
		{
			if (module->getType() == ParticleEmitter::SpawnShapeModule::s_type)
			{
				LUMIX_DELETE(m_allocator, module);
				emitter->m_modules.eraseItem(module);
				m_universe.destroyComponent(
					emitter->m_entity, PARTICLE_EMITTER_SPAWN_SHAPE_HASH, this, component);
				break;
			}
		}
	}


	void destroyParticleEmitterRandomRotation(ComponentIndex component)
	{
		auto* emitter = m_particle_emitters[component];
		for (auto* module : emitter->m_modules)
		{
			if (module->getType() == ParticleEmitter::RandomRotationModule::s_type)
			{
				LUMIX_DELETE(m_allocator, module);
				emitter->m_modules.eraseItem(module);
				m_universe.destroyComponent(
					emitter->m_entity, PARTICLE_EMITTER_RANDOM_ROTATION_HASH, this, component);
				break;
			}
		}
	}


	void destroyComponent(ComponentIndex component, uint32 type) override;


	void setParticleEmitterAlpha(ComponentIndex cmp, const Vec2* values, int count) override
	{
		ASSERT(count > 0);
		ASSERT(values[1].x < 0.001f);
		ASSERT(values[count - 2].x > 0.999f);

		auto* alpha_module = getEmitterModule<ParticleEmitter::AlphaModule>(cmp);
		if (!alpha_module) return;

		alpha_module->m_values.resize(count);
		for (int i = 0; i < count; ++i)
		{
			alpha_module->m_values[i] = values[i];
		}
		alpha_module->sample();
	}


	void setParticleEmitterAcceleration(ComponentIndex cmp, const Vec3& value) override
	{
		auto* module = getEmitterModule<ParticleEmitter::ForceModule>(cmp);
		if (module) module->m_acceleration = value;
	}


	Vec3 getParticleEmitterAcceleration(ComponentIndex cmp) override
	{
		auto* module = getEmitterModule<ParticleEmitter::ForceModule>(cmp);
		return module ? module->m_acceleration : Vec3();
	}


	int getParticleEmitterSizeCount(ComponentIndex cmp) override
	{
		auto* module = getEmitterModule<ParticleEmitter::SizeModule>(cmp);
		return module ? module->m_values.size() : 0; 
	}


	const Vec2* getParticleEmitterSize(ComponentIndex cmp) override
	{
		auto* module = getEmitterModule<ParticleEmitter::SizeModule>(cmp);
		return module ? &module->m_values[0] : nullptr;
	}


	void setParticleEmitterSize(ComponentIndex cmp, const Vec2* values, int count) override
	{
		ASSERT(count > 0);
		ASSERT(values[0].x < 0.001f);
		ASSERT(values[count-1].x > 0.999f);

		auto* module = getEmitterModule<ParticleEmitter::SizeModule>(cmp);
		if (!module) return;

		auto size_module = static_cast<ParticleEmitter::SizeModule*>(module);
		size_module->m_values.resize(count);
		for (int i = 0; i < count; ++i)
		{
			size_module->m_values[i] = values[i];
		}
		size_module->sample();
	}


	template <typename T>
	T* getEmitterModule(ComponentIndex cmp) const
	{
		auto& modules = m_particle_emitters[cmp]->m_modules;
		for (auto* module : modules)
		{
			if (module->getType() == T::s_type)
			{
				return static_cast<T*>(module);
			}
		}
		return nullptr;
	}


	int getParticleEmitterAlphaCount(ComponentIndex cmp) override 
	{
		auto* module = getEmitterModule<ParticleEmitter::AlphaModule>(cmp);
		return module ? module->m_values.size() : 0;
	}


	const Vec2* getParticleEmitterAlpha(ComponentIndex cmp) override
	{
		auto* module = getEmitterModule<ParticleEmitter::AlphaModule>(cmp);
		return module ? &module->m_values[0] : 0;
	}


	Vec2 getParticleEmitterLinearMovementX(ComponentIndex cmp) override
	{
		auto* module = getEmitterModule<ParticleEmitter::LinearMovementModule>(cmp);
		return module ? Vec2(module->m_x.from, module->m_x.to) : Vec2(0, 0);
	}


	void setParticleEmitterLinearMovementX(ComponentIndex cmp, const Vec2& value) override
	{
		auto* module = getEmitterModule<ParticleEmitter::LinearMovementModule>(cmp);
		if (module)
		{
			module->m_x = value;
			module->m_x.check();
		}
	}


	Vec2 getParticleEmitterLinearMovementY(ComponentIndex cmp) override
	{
		auto* module = getEmitterModule<ParticleEmitter::LinearMovementModule>(cmp);
		return module ? Vec2(module->m_y.from, module->m_y.to) : Vec2(0, 0);
	}


	void setParticleEmitterLinearMovementY(ComponentIndex cmp, const Vec2& value) override
	{
		auto* module = getEmitterModule<ParticleEmitter::LinearMovementModule>(cmp);
		if (module)
		{
			module->m_y = value;
			module->m_y.check();
		}
	}


	Vec2 getParticleEmitterLinearMovementZ(ComponentIndex cmp) override
	{
		auto* module = getEmitterModule<ParticleEmitter::LinearMovementModule>(cmp);
		return module ? Vec2(module->m_z.from, module->m_z.to) : Vec2(0, 0);
	}


	void setParticleEmitterLinearMovementZ(ComponentIndex cmp, const Vec2& value) override
	{
		auto* module = getEmitterModule<ParticleEmitter::LinearMovementModule>(cmp);
		if (module)
		{
			module->m_z = value;
			module->m_z.check();
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
		m_particle_emitters[cmp]->m_initial_life.checkZero();
	}


	void setParticleEmitterInitialSize(ComponentIndex cmp, const Vec2& value) override
	{
		m_particle_emitters[cmp]->m_initial_size = value;
		m_particle_emitters[cmp]->m_initial_size.checkZero();
	}


	Vec2 getParticleEmitterInitialSize(ComponentIndex cmp) override
	{
		return m_particle_emitters[cmp]->m_initial_size;
	}


	void setParticleEmitterSpawnPeriod(ComponentIndex cmp, const Vec2& value) override
	{
		m_particle_emitters[cmp]->m_spawn_period = value;
		m_particle_emitters[cmp]->m_spawn_period.from =
			Math::maximum(0.01f, m_particle_emitters[cmp]->m_spawn_period.from);
		m_particle_emitters[cmp]->m_spawn_period.checkZero();
	}


	ComponentIndex createCamera(Entity entity)
	{
		Camera& camera = m_cameras.emplace();
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
		m_universe.addComponent(entity, CAMERA_HASH, this, m_cameras.size() - 1);
		return m_cameras.size() - 1;
	}


	ComponentIndex createTerrain(Entity entity)
	{
		Terrain* terrain = LUMIX_NEW(m_allocator, Terrain)(
			m_renderer, entity, *this, m_allocator);
		m_terrains.push(terrain);
		m_universe.addComponent(entity, TERRAIN_HASH, this, m_terrains.size() - 1);
		return m_terrains.size() - 1;
	}


	ComponentIndex createComponent(uint32 type, Entity entity) override;


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


	ComponentIndex createParticleEmitterPlane(Entity entity)
	{
		for (int i = 0; i < m_particle_emitters.size(); ++i)
		{
			auto* emitter = m_particle_emitters[i];
			if (emitter->m_entity == entity)
			{
				auto module = LUMIX_NEW(m_allocator, ParticleEmitter::PlaneModule)(*emitter);
				emitter->addModule(module);
				m_universe.addComponent(entity, PARTICLE_EMITTER_PLANE_HASH, this, i);
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
	

	ComponentIndex createParticleEmitterSpawnShape(Entity entity)
	{
		for (int i = 0; i < m_particle_emitters.size(); ++i)
		{
			auto* emitter = m_particle_emitters[i];
			if (emitter->m_entity == entity)
			{
				auto module = LUMIX_NEW(m_allocator, ParticleEmitter::SpawnShapeModule)(*emitter);
				emitter->addModule(module);
				m_universe.addComponent(entity, PARTICLE_EMITTER_SPAWN_SHAPE_HASH, this, i);
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


	ComponentIndex createParticleEmitterForce(Entity entity)
	{
		for (int i = 0; i < m_particle_emitters.size(); ++i)
		{
			auto* emitter = m_particle_emitters[i];
			if (emitter->m_entity == entity)
			{
				auto module = LUMIX_NEW(m_allocator, ParticleEmitter::ForceModule)(*emitter);
				emitter->addModule(module);
				m_universe.addComponent(entity, PARTICLE_EMITTER_FORCE_HASH, this, i);
				return i;
			}
		}
		return INVALID_COMPONENT;
	}


	ComponentIndex createParticleEmitterAttractor(Entity entity)
	{
		for(int i = 0; i < m_particle_emitters.size(); ++i)
		{
			auto* emitter = m_particle_emitters[i];
			if(emitter->m_entity == entity)
			{
				auto module = LUMIX_NEW(m_allocator, ParticleEmitter::AttractorModule)(*emitter);
				emitter->addModule(module);
				m_universe.addComponent(entity, PARTICLE_EMITTER_ATTRACTOR_HASH, this, i);
				return i;
			}
		}
		return INVALID_COMPONENT;
	}



	ComponentIndex createParticleEmitterSize(Entity entity)
	{
		for (int i = 0; i < m_particle_emitters.size(); ++i)
		{
			auto* emitter = m_particle_emitters[i];
			if (emitter->m_entity == entity)
			{
				auto module = LUMIX_NEW(m_allocator, ParticleEmitter::SizeModule)(*emitter);
				emitter->addModule(module);
				m_universe.addComponent(entity, PARTICLE_EMITTER_SIZE_HASH, this, i);
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


	Renderable* getRenderables() override
	{
		return &m_renderables[0];
	}


	Renderable* getRenderable(ComponentIndex cmp) override
	{
		return &m_renderables[cmp];
	}


	ComponentIndex getRenderableComponent(Entity entity) override
	{
		ComponentIndex cmp = (ComponentIndex)entity;
		if (cmp >= m_renderables.size()) return INVALID_COMPONENT;
		if (m_renderables[cmp].entity == INVALID_ENTITY) return INVALID_COMPONENT;
		return cmp;
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
		ComponentIndex cmp = (ComponentIndex)entity;

		if (cmp < m_renderables.size() && m_renderables[cmp].entity != INVALID_ENTITY &&
			m_renderables[cmp].model && m_renderables[cmp].model->isReady())
		{
			Renderable& r = m_renderables[cmp];
			r.matrix = m_universe.getMatrix(entity);
			m_culling_system->updateBoundingPosition(m_universe.getPosition(entity), cmp);
			if (r.model && r.model->isReady())
			{
				float radius = m_universe.getScale(entity) * r.model->getBoundingRadius();
				m_culling_system->updateBoundingRadius(radius, cmp);
			}

			if(m_is_forward_rendered)
			{
				float bounding_radius = r.model ? r.model->getBoundingRadius() : 1;
				for (int light_idx = 0, c = m_point_lights.size(); light_idx < c; ++light_idx)
				{
					for (int j = 0, c2 = m_light_influenced_geometry[light_idx].size(); j < c2; ++j)
					{
						if(m_light_influenced_geometry[light_idx][j] == cmp)
						{
							m_light_influenced_geometry[light_idx].eraseFast(j);
							break;
						}
					}

					Vec3 pos = m_universe.getPosition(r.entity);
					Frustum frustum = getPointLightFrustum(light_idx);
					if(frustum.isSphereInside(pos, bounding_radius))
					{
						m_light_influenced_geometry[light_idx].push(cmp);
					}
				}
			}
		}

		for (int i = 0, c = m_point_lights.size(); i < c; ++i)
		{
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


	void setTerrainMaterialPath(ComponentIndex cmp, const Path& path) override
	{
		if (path.isValid())
		{
			Material* material = static_cast<Material*>(
				m_engine.getResourceManager().get(ResourceManager::MATERIAL)->load(path));
			m_terrains[cmp]->setMaterial(material);
		}
		else
		{
			m_terrains[cmp]->setMaterial(nullptr);
		}
	}


	Material* getTerrainMaterial(ComponentIndex cmp) override
	{
		return m_terrains[cmp]->getMaterial();
	}


	Path getTerrainMaterialPath(ComponentIndex cmp) override
	{
		if (m_terrains[cmp]->getMaterial())
		{
			return m_terrains[cmp]->getMaterial()->getPath();
		}
		else
		{
			return Path("");
		}
	}


	void setTerrainXZScale(ComponentIndex cmp, float scale) override
	{
		m_terrains[cmp]->setXZScale(scale);
	}

	float getTerrainXZScale(ComponentIndex cmp) override { return m_terrains[cmp]->getXZScale(); }


	void setTerrainYScale(ComponentIndex cmp, float scale) override
	{
		m_terrains[cmp]->setYScale(scale);
	}

	float getTerrainYScale(ComponentIndex cmp) { return m_terrains[cmp]->getYScale(); }


	Pose* getPose(ComponentIndex cmp) override { return m_renderables[cmp].pose; }


	Entity getRenderableEntity(ComponentIndex cmp) override { return m_renderables[cmp].entity; }


	Model* getRenderableModel(ComponentIndex cmp) override { return m_renderables[cmp].model; }


	void showRenderable(ComponentIndex cmp) override
	{
		if (!m_renderables[cmp].model || !m_renderables[cmp].model->isReady()) return;

		Sphere sphere(m_universe.getPosition(m_renderables[cmp].entity),
			m_renderables[cmp].model->getBoundingRadius());
		m_culling_system->addStatic(cmp, sphere);
	}


	void hideRenderable(ComponentIndex cmp) override
	{
		m_culling_system->removeStatic(cmp);
	}


	Path getRenderablePath(ComponentIndex cmp) override
	{
		return m_renderables[cmp].model ? m_renderables[cmp].model->getPath() : Path("");
	}


	void setRenderableLayer(ComponentIndex cmp, const int32& layer) override
	{
		m_culling_system->setLayerMask(cmp, (int64)1 << (int64)layer);
	}


	int getRenderableMaterialsCount(ComponentIndex cmp) override
	{
		return m_renderables[cmp].model ? m_renderables[cmp].mesh_count : 0;
	}


	void setRenderablePath(ComponentIndex cmp, const Path& path) override
	{
		Renderable& r = m_renderables[cmp];

		auto* manager = m_engine.getResourceManager().get(ResourceManager::MODEL);
		if (path.isValid())
		{
			Model* model = static_cast<Model*>(manager->load(path));
			setModel(cmp, model);
		}
		else
		{
			setModel(cmp, nullptr);
		}
		r.matrix = m_universe.getMatrix(r.entity);
	}


	void forceGrassUpdate(ComponentIndex cmp) override { m_terrains[cmp]->forceGrassUpdate(); }


	void getTerrainInfos(Array<const TerrainInfo*>& infos,
		const Vec3& camera_pos,
		LIFOAllocator& frame_allocator) override
	{
		PROFILE_FUNCTION();
		infos.reserve(m_terrains.size());
		for (int i = 0; i < m_terrains.size(); ++i)
		{
			if (m_terrains[i])
			{
				m_terrains[i]->getInfos(infos, camera_pos, frame_allocator);
			}
		}
	}


	void getGrassInfos(const Frustum& frustum,
		Array<GrassInfo>& infos,
		ComponentIndex camera) override
	{
		PROFILE_FUNCTION();

		if (!m_is_grass_enabled) return;

		for (int i = 0; i < m_terrains.size(); ++i)
		{
			if (m_terrains[i])
			{
				m_terrains[i]->getGrassInfos(frustum, infos, camera);
			}
		}
	}


	static Texture* LUA_getMaterialTexture(Material* material, int texture_index)
	{
		if (!material) return nullptr;
		return material->getTexture(texture_index);
	}


	static void LUA_setRenderablePath(IScene* scene, int component, const char* path)
	{
		RenderScene* render_scene = (RenderScene*)scene;
		render_scene->setRenderablePath(component, Path(path));
	}


	static void LUA_setRenderableMaterial(RenderScene* scene,
		ComponentIndex cmp,
		int index,
		const char* path)
	{
		scene->setRenderableMaterial(cmp, index, Path(path));
	}


	void registerLuaAPI()
	{
		auto* scene = m_universe.getScene(crc32("lua_script"));
		if (!scene) return;

		auto* script_scene = static_cast<LuaScriptScene*>(scene);

		lua_State* L = script_scene->getGlobalState();
		Pipeline::registerLuaAPI(L);

		#define REGISTER_FUNCTION(F)\
			do { \
			auto f = &LuaWrapper::wrapMethod<RenderSceneImpl, decltype(&RenderSceneImpl::F), &RenderSceneImpl::F>; \
			LuaWrapper::createSystemFunction(L, "Renderer", #F, f); \
			} while(false) \

		REGISTER_FUNCTION(setFogDensity);
		REGISTER_FUNCTION(setFogBottom);
		REGISTER_FUNCTION(setFogHeight);
		REGISTER_FUNCTION(setFogColor);
		REGISTER_FUNCTION(getFogDensity);
		REGISTER_FUNCTION(getFogBottom);
		REGISTER_FUNCTION(getFogHeight);
		REGISTER_FUNCTION(getFogColor);
		REGISTER_FUNCTION(getCameraSlot);
		REGISTER_FUNCTION(getCameraComponent);
		REGISTER_FUNCTION(getRenderableComponent);
		REGISTER_FUNCTION(addDebugCross);
		REGISTER_FUNCTION(getTerrainMaterial);

		#undef REGISTER_FUNCTION

		#define REGISTER_FUNCTION(F)\
			do { \
			auto f = &LuaWrapper::wrap<decltype(&RenderSceneImpl::LUA_##F), &RenderSceneImpl::LUA_##F>; \
			LuaWrapper::createSystemFunction(L, "Renderer", #F, f); \
			} while(false) \

		REGISTER_FUNCTION(getMaterialTexture);
		REGISTER_FUNCTION(setRenderableMaterial);
		REGISTER_FUNCTION(setRenderablePath);

		#undef REGISTER_FUNCTION
	}


	void sendMessage(uint32 type, void*) override
	{
		static const uint32 register_hash = crc32("registerLuaAPI");
		if (type == register_hash)
		{
			registerLuaAPI();
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


	void setGrassPath(ComponentIndex cmp, int index, const Path& path) override
	{
		m_terrains[cmp]->setGrassTypePath(index, path);
	}


	Path getGrassPath(ComponentIndex cmp, int index) override
	{
		return m_terrains[cmp]->getGrassTypePath(index);
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
		return getNextRenderable(-1);
	}


	ComponentIndex getNextRenderable(ComponentIndex cmp) override
	{
		for(int i = cmp + 1; i < m_renderables.size(); ++i)
		{
			if(m_renderables[i].entity != INVALID_ENTITY) return i;
		}
		return INVALID_COMPONENT;
	}


	const CullingSystem::Results* cull(const Frustum& frustum)
	{
		PROFILE_FUNCTION();
		if (m_renderables.empty()) return nullptr;

		m_culling_system->cullToFrustumAsync(frustum, ~0UL);
		return &m_culling_system->getResult();
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
		const Vec3& lod_ref_point)
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
			Array<RenderableMesh>& subinfos = m_temporary_infos[subresult_index];
			subinfos.clear();
			if (results[subresult_index].empty()) continue;

			MTJD::Job* job = MTJD::makeJob(m_engine.getMTJDManager(),
				[&subinfos, this, &results, subresult_index, &frustum, lod_ref_point]()
				{
					PROFILE_BLOCK("Temporary Info Job");
					PROFILE_INT("Renderable count", results[subresult_index].size());
					Vec3 ref_point = lod_ref_point;
					const int* LUMIX_RESTRICT raw_subresults = &results[subresult_index][0];
					Renderable* LUMIX_RESTRICT renderables = &m_renderables[0];
					for (int i = 0, c = results[subresult_index].size(); i < c; ++i)
					{
						Renderable* LUMIX_RESTRICT renderable = &renderables[raw_subresults[i]];
						Model* LUMIX_RESTRICT model = renderable->model;
						float squared_distance =
							(renderable->matrix.getTranslation() - ref_point).squaredLength();

						LODMeshIndices lod = model->getLODMeshIndices(squared_distance);
						for (int j = lod.from, c = lod.to; j <= c; ++j)
						{
							auto& info = subinfos.emplace();
							info.renderable = raw_subresults[i];
							info.mesh = &renderable->meshes[j];
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


	void getPointLightInfluencedGeometry(ComponentIndex light_cmp,
		const Frustum& frustum,
		Array<RenderableMesh>& infos) override
	{
		PROFILE_FUNCTION();

		int light_index = getPointLightIndex(light_cmp);
		for (int j = 0, cj = m_light_influenced_geometry[light_index].size(); j < cj; ++j)
		{
			ComponentIndex renderable_cmp = m_light_influenced_geometry[light_index][j];
			Renderable& renderable = m_renderables[renderable_cmp];
			const Sphere& sphere = m_culling_system->getSphere(renderable_cmp);
			if (frustum.isSphereInside(sphere.m_position, sphere.m_radius))
			{
				for (int k = 0, kc = renderable.model->getMeshCount(); k < kc; ++k)
				{
					auto& info = infos.emplace();
					info.mesh = &renderable.model->getMesh(k);
					info.renderable = renderable_cmp;
				}
			}
		}
	}


	void getPointLightInfluencedGeometry(ComponentIndex light_cmp,
		Array<RenderableMesh>& infos) override
	{
		PROFILE_FUNCTION();

		int light_index = getPointLightIndex(light_cmp);
		auto& geoms = m_light_influenced_geometry[light_index];
		for (int j = 0, cj = geoms.size(); j < cj; ++j)
		{
			const Renderable& renderable = m_renderables[geoms[j]];
			for (int k = 0, kc = renderable.model->getMeshCount(); k < kc; ++k)
			{
				auto& info = infos.emplace();
				info.mesh = &renderable.model->getMesh(k);
				info.renderable = geoms[j];
			}
		}
	}


	void getRenderableEntities(const Frustum& frustum, Array<Entity>& entities) override
	{
		PROFILE_FUNCTION();

		const CullingSystem::Results* results = cull(frustum);
		if (!results) return;

		for (auto& subresults : *results)
		{
			for (ComponentIndex renderable_cmp : subresults)
			{
				entities.push(m_renderables[renderable_cmp].entity);
			}
		}
	}


	Array<Array<RenderableMesh>>& getRenderableInfos(const Frustum& frustum,
		const Vec3& lod_ref_point) override
	{
		PROFILE_FUNCTION();

		for(auto& i : m_temporary_infos) i.clear();
		const CullingSystem::Results* results = cull(frustum);
		if (!results) return m_temporary_infos;

		fillTemporaryInfos(*results, frustum, lod_ref_point);
		return m_temporary_infos;
	}


	void setCameraSlot(ComponentIndex camera, const char* slot) override
	{
		copyString(m_cameras[camera].m_slot, Camera::MAX_SLOT_LENGTH, slot);
	}


	ComponentIndex getCameraComponent(Entity entity)
	{
		for (int i = 0; i < m_cameras.size(); ++i)
		{
			if (m_cameras[i].m_entity == entity) return i;
		}
		return INVALID_COMPONENT;
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
		DebugPoint& point = m_debug_points.emplace();
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
		DebugLine& line = m_debug_lines.emplace();
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
		PROFILE_FUNCTION();
		RayCastModelHit hit;
		hit.m_is_hit = false;
		Universe& universe = getUniverse();
		for (int i = 0; i < m_renderables.size(); ++i)
		{
			auto& r = m_renderables[i];
			if (ignored_renderable != i && r.model)
			{
				const Vec3& pos = r.matrix.getTranslation();
				float radius = r.model->getBoundingRadius();
				float scale = universe.getScale(r.entity);
				Vec3 intersection;
				if (dotProduct(pos - origin, pos - origin) < radius * radius ||
					Math::getRaySphereIntersection(origin, dir, pos, radius * scale, intersection))
				{
					RayCastModelHit new_hit = r.model->castRay(origin, dir, r.matrix);
					if (new_hit.m_is_hit && (!hit.m_is_hit || new_hit.m_t < hit.m_t))
					{
						new_hit.m_component = i;
						new_hit.m_entity = r.entity;
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
				RayCastModelHit terrain_hit = m_terrains[i]->castRay(origin, dir);
				if (terrain_hit.m_is_hit && (!hit.m_is_hit || terrain_hit.m_t < hit.m_t))
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
		return m_point_lights_map[cmp];
	}


	Vec4 getShadowmapCascades(ComponentIndex cmp) override
	{
		return m_global_lights[getGlobalLightIndex(cmp)].m_cascades;
	}


	void setShadowmapCascades(ComponentIndex cmp,
									  const Vec4& value) override
	{
		Vec4 valid_value = value;
		valid_value.x = Math::maximum(valid_value.x, 0.02f);
		valid_value.y = Math::maximum(valid_value.x + 0.01f, valid_value.y);
		valid_value.z = Math::maximum(valid_value.y + 0.01f, valid_value.z);
		valid_value.w = Math::maximum(valid_value.z + 0.01f, valid_value.w);

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

	void setPointLightIntensity(ComponentIndex cmp, float intensity) override
	{
		m_point_lights[getPointLightIndex(cmp)].m_diffuse_intensity = intensity;
	}

	void setGlobalLightIntensity(ComponentIndex cmp, float intensity) override
	{
		m_global_lights[getGlobalLightIndex(cmp)].m_diffuse_intensity = intensity;
	}

	void setPointLightColor(ComponentIndex cmp, const Vec3& color) override
	{
		m_point_lights[getPointLightIndex(cmp)].m_diffuse_color = color;
	}

	void setGlobalLightColor(ComponentIndex cmp, const Vec3& color) override
	{
		m_global_lights[getGlobalLightIndex(cmp)].m_diffuse_color = color;
	}

	void setGlobalLightSpecular(ComponentIndex cmp, const Vec3& color) override
	{
		m_global_lights[getGlobalLightIndex(cmp)].m_specular = color;
	}

	void setGlobalLightSpecularIntensity(ComponentIndex cmp, float intensity) override
	{
		m_global_lights[getGlobalLightIndex(cmp)].m_specular_intensity = intensity;
	}

	void setLightAmbientIntensity(ComponentIndex cmp, float intensity) override
	{
		m_global_lights[getGlobalLightIndex(cmp)].m_ambient_intensity = intensity;
	}

	void setLightAmbientColor(ComponentIndex cmp, const Vec3& color) override
	{
		m_global_lights[getGlobalLightIndex(cmp)].m_ambient_color = color;
	}

	float getPointLightIntensity(ComponentIndex cmp) override
	{
		return m_point_lights[getPointLightIndex(cmp)].m_diffuse_intensity;
	}

	float getGlobalLightIntensity(ComponentIndex cmp) override
	{
		return m_global_lights[getGlobalLightIndex(cmp)].m_diffuse_intensity;
	}

	Vec3 getPointLightColor(ComponentIndex cmp) override
	{
		return m_point_lights[getPointLightIndex(cmp)].m_diffuse_color;
	}

	void setPointLightSpecularColor(ComponentIndex cmp, const Vec3& color) override
	{
		m_point_lights[getPointLightIndex(cmp)].m_specular_color = color;
	}

	Vec3 getPointLightSpecularColor(ComponentIndex cmp) override
	{
		return m_point_lights[getPointLightIndex(cmp)].m_specular_color;
	}

	void setPointLightSpecularIntensity(ComponentIndex cmp, float intensity) override
	{
		m_point_lights[getPointLightIndex(cmp)].m_specular_intensity = intensity;
	}

	float getPointLightSpecularIntensity(ComponentIndex cmp) override
	{
		return m_point_lights[getPointLightIndex(cmp)].m_specular_intensity;
	}

	Vec3 getGlobalLightColor(ComponentIndex cmp) override
	{
		return m_global_lights[getGlobalLightIndex(cmp)].m_diffuse_color;
	}

	Vec3 getGlobalLightSpecular(ComponentIndex cmp) override
	{
		return m_global_lights[getGlobalLightIndex(cmp)].m_specular;
	}

	float getGlobalLightSpecularIntensity(ComponentIndex cmp) override
	{
		return m_global_lights[getGlobalLightIndex(cmp)].m_specular_intensity;
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
				compareString(m_cameras[i].m_slot, slot) == 0)
			{
				return i;
			}
		}
		return INVALID_COMPONENT;
	}

	float getTime() const override { return m_time; }


	void modelUnloaded(Model*, ComponentIndex component)
	{
		auto& r = m_renderables[component];
		if (!r.custom_meshes)
		{
			r.meshes = nullptr;
			r.mesh_count = 0;
		}
		LUMIX_DELETE(m_allocator, r.pose);
		r.pose = nullptr;

		for (int i = 0; i < m_point_lights.size(); ++i)
		{
			m_light_influenced_geometry[i].eraseItemFast(component);
		}
		m_culling_system->removeStatic(component);
	}


	void freeCustomMeshes(Renderable& r, MaterialManager* manager)
	{
		if (!r.custom_meshes) return;
		for (int i = 0; i < r.mesh_count; ++i)
		{
			manager->unload(*r.meshes[i].material);
			r.meshes[i].~Mesh();
		}
		m_allocator.deallocate(r.meshes);
		r.meshes = nullptr;
		r.custom_meshes = false;
		r.mesh_count = 0;
	}


	void modelLoaded(Model* model, ComponentIndex component)
	{
		auto& rm = m_engine.getResourceManager();
		auto* material_manager = static_cast<MaterialManager*>(rm.get(ResourceManager::MATERIAL));

		auto& r = m_renderables[component];
		float bounding_radius = r.model->getBoundingRadius();
		float scale = m_universe.getScale(r.entity);
		Sphere sphere(r.matrix.getTranslation(), bounding_radius * scale);
		m_culling_system->addStatic(component, sphere);
		m_culling_system->setLayerMask(component, r.layer_mask);
		ASSERT(!r.pose);
		if (model->getBoneCount() > 0)
		{
			r.pose = LUMIX_NEW(m_allocator, Pose)(m_allocator);
			r.pose->resize(model->getBoneCount());
			model->getPose(*r.pose);
		}
		r.matrix = m_universe.getMatrix(r.entity);
		ASSERT(!r.meshes || r.custom_meshes)
		if (r.meshes)
		{
			allocateCustomMeshes(r, model->getMeshCount());
			for (int i = 0; i < r.mesh_count; ++i)
			{
				auto& src = model->getMesh(i);
				if (!r.meshes[i].material)
				{
					material_manager->load(*src.material);
					r.meshes[i].material = src.material;
				}
				r.meshes[i].set(
					src.vertex_def,
					src.attribute_array_offset,
					src.attribute_array_size,
					src.indices_offset,
					src.indices_count
					);
			}
		}
		else
		{
			r.meshes = &r.model->getMesh(0);
			r.mesh_count = r.model->getMeshCount();
		}

		for (int i = 0; i < m_point_lights.size(); ++i)
		{
			PointLight& light = m_point_lights[i];
			Vec3 t = r.matrix.getTranslation();
			float radius = r.model->getBoundingRadius();
			if ((t - m_universe.getPosition(light.m_entity)).squaredLength() <
				(radius + light.m_range) * (radius + light.m_range))
			{
				m_light_influenced_geometry[i].push(component);
			}
		}
	}


	void modelUnloaded(Model* model)
	{
		for (int i = 0, c = m_renderables.size(); i < c; ++i)
		{
			if (m_renderables[i].entity != INVALID_ENTITY && m_renderables[i].model == model)
			{
				modelUnloaded(model, i);
			}
		}
	}


	void modelLoaded(Model* model)
	{
		for (int i = 0, c = m_renderables.size(); i < c; ++i)
		{
			if (m_renderables[i].entity != INVALID_ENTITY && m_renderables[i].model == model)
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


	void allocateCustomMeshes(Renderable& r, int count)
	{
		if (r.custom_meshes && r.mesh_count == count) return;

		auto& rm = r.model->getResourceManager();
		auto* material_manager = static_cast<MaterialManager*>(rm.get(ResourceManager::MATERIAL));

		auto* new_meshes = (Mesh*)m_allocator.allocate(count * sizeof(Mesh));
		if (r.meshes)
		{
			for (int i = 0; i < r.mesh_count; ++i)
			{
				new (NewPlaceholder(), new_meshes + i) Mesh(r.meshes[i]);
			}

			if (r.custom_meshes)
			{
				for (int i = count; i < r.mesh_count; ++i)
				{
					material_manager->unload(*r.meshes[i].material);
				}
				m_allocator.deallocate(r.meshes);
			}
			else
			{
				for (int i = 0; i < r.mesh_count; ++i)
				{
					material_manager->load(*r.meshes[i].material);
				}
			}
		}

		for (int i = r.mesh_count; i < count; ++i)
		{
			new (NewPlaceholder(), new_meshes + i)
				Mesh(m_renderer.getBasicVertexDecl(), nullptr, 0, 0, 0, 0, "", m_allocator);
		}
		r.meshes = new_meshes;
		r.mesh_count = count;
		r.custom_meshes = true;
	}


	void setRenderableMaterial(ComponentIndex cmp, int index, const Path& path) override
	{
		auto& r = m_renderables[cmp];
		if (r.meshes && r.mesh_count > index && path == r.meshes[index].material->getPath()) return;

		auto& rm = r.model->getResourceManager();
		auto* material_manager = static_cast<MaterialManager*>(rm.get(ResourceManager::MATERIAL));

		int new_count = Math::maximum(int8(index + 1), r.mesh_count);
		allocateCustomMeshes(r, new_count);

		if (r.meshes[index].material) material_manager->unload(*r.meshes[index].material);
		auto* new_material = static_cast<Material*>(material_manager->load(path));
		r.meshes[index].material = new_material;
	}


	Path getRenderableMaterial(ComponentIndex cmp, int index) override
	{
		auto& r = m_renderables[cmp];
		if (!r.meshes) return Path("");

		return r.meshes[index].material->getPath();
	}


	void setModel(ComponentIndex component, Model* model)
	{
		ASSERT(m_renderables[component].entity != INVALID_ENTITY);

		Model* old_model = m_renderables[component].model;
		bool no_change = model == old_model && old_model;
		if (no_change)
		{
			old_model->getResourceManager().get(ResourceManager::MODEL)->unload(*old_model);
			return;
		}
		if (old_model)
		{
			auto& rm = old_model->getResourceManager();
			auto* material_manager = static_cast<MaterialManager*>(rm.get(ResourceManager::MATERIAL));
			freeCustomMeshes(m_renderables[component], material_manager);
			ModelLoadedCallback* callback = getModelLoadedCallback(old_model);
			--callback->m_ref_count;
			if (old_model->isReady())
			{
				m_culling_system->removeStatic(component);
			}
			old_model->getResourceManager().get(ResourceManager::MODEL)->unload(*old_model);
		}
		m_renderables[component].model = model;
		m_renderables[component].meshes = nullptr;
		m_renderables[component].mesh_count = 0;
		LUMIX_DELETE(m_allocator, m_renderables[component].pose);
		m_renderables[component].pose = nullptr;
		if (model)
		{
			ModelLoadedCallback* callback = getModelLoadedCallback(model);
			++callback->m_ref_count;

			if (model->isReady())
			{
				modelLoaded(model, component);
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


	int getParticleEmitterAttractorCount(ComponentIndex cmp) override
	{
		auto* module = getEmitterModule<ParticleEmitter::AttractorModule>(cmp);
		return module ? module->m_count : 0;
	}


	void addParticleEmitterAttractor(ComponentIndex cmp, int index) override
	{
		auto* module = getEmitterModule<ParticleEmitter::AttractorModule>(cmp);
		if (!module) return;

		auto* plane_module = static_cast<ParticleEmitter::AttractorModule*>(module);
		if (plane_module->m_count == lengthOf(plane_module->m_entities)) return;

		if (index < 0)
		{
			plane_module->m_entities[plane_module->m_count] = INVALID_ENTITY;
			++plane_module->m_count;
			return;
		}

		for (int i = plane_module->m_count - 1; i > index; --i)
		{
			plane_module->m_entities[i] = plane_module->m_entities[i - 1];
		}
		plane_module->m_entities[index] = INVALID_ENTITY;
		++plane_module->m_count;
	}


	void removeParticleEmitterAttractor(ComponentIndex cmp, int index) override
	{
		auto* module = getEmitterModule<ParticleEmitter::AttractorModule>(cmp);
		if (!module) return;

		for (int i = index; i < module->m_count - 1; ++i)
		{
			module->m_entities[i] = module->m_entities[i + 1];
		}
		--module->m_count;
	}


	Entity getParticleEmitterAttractorEntity(ComponentIndex cmp, int index) override
	{
		auto* module = getEmitterModule<ParticleEmitter::AttractorModule>(cmp);
		return module ? module->m_entities[index] : INVALID_ENTITY;
	}


	void setParticleEmitterAttractorEntity(ComponentIndex cmp, int index, Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::AttractorModule>(cmp);
		if(module) module->m_entities[index] = entity;
	}


	float getParticleEmitterShapeRadius(ComponentIndex cmp) override
	{
		auto* module = getEmitterModule<ParticleEmitter::SpawnShapeModule>(cmp);
		return module ? module->m_radius : 0.0f;
	}


	void setParticleEmitterShapeRadius(ComponentIndex cmp, float value) override
	{
		auto* module = getEmitterModule<ParticleEmitter::SpawnShapeModule>(cmp);
		if (module) module->m_radius = value;
	}


	int getParticleEmitterPlaneCount(ComponentIndex cmp) override
	{
		auto* module = getEmitterModule<ParticleEmitter::PlaneModule>(cmp);
		return module ? module->m_count : 0;
	}


	void addParticleEmitterPlane(ComponentIndex cmp, int index) override
	{
		auto* plane_module = getEmitterModule<ParticleEmitter::PlaneModule>(cmp);
		if (!plane_module) return;

		if (plane_module->m_count == lengthOf(plane_module->m_entities)) return;

		if (index < 0)
		{
			plane_module->m_entities[plane_module->m_count] = INVALID_ENTITY;
			++plane_module->m_count;
			return;
		}

		for (int i = plane_module->m_count - 1; i > index; --i)
		{
			plane_module->m_entities[i] = plane_module->m_entities[i - 1];
		}
		plane_module->m_entities[index] = INVALID_ENTITY;
		++plane_module->m_count;
			
	}


	void removeParticleEmitterPlane(ComponentIndex cmp, int index) override
	{
		auto* plane_module = getEmitterModule<ParticleEmitter::PlaneModule>(cmp);
		if (!plane_module) return;

		for (int i = index; i < plane_module->m_count - 1; ++i)
		{
			plane_module->m_entities[i] = plane_module->m_entities[i + 1];
		}
		--plane_module->m_count;
	}


	Entity getParticleEmitterPlaneEntity(ComponentIndex cmp, int index) override
	{
		auto* module = getEmitterModule<ParticleEmitter::PlaneModule>(cmp);
		return module ? module->m_entities[index] : INVALID_ENTITY;
	}


	void setParticleEmitterPlaneEntity(ComponentIndex cmp, int index, Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::PlaneModule>(cmp);
		if (module) module->m_entities[index] = entity;
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
		GlobalLight& light = m_global_lights.emplace();
		light.m_entity = entity;
		light.m_diffuse_color.set(1, 1, 1);
		light.m_diffuse_intensity = 0;
		light.m_ambient_color.set(1, 1, 1);
		light.m_ambient_intensity = 1;
		light.m_fog_color.set(1, 1, 1);
		light.m_fog_density = 0;
		light.m_uid = ++m_global_light_last_uid;
		light.m_cascades.set(3, 8, 100, 300);
		light.m_fog_bottom = 0.0f;
		light.m_fog_height = 10.0f;
		light.m_specular.set(0, 0, 0);
		light.m_specular_intensity = 1;

		if (m_global_lights.size() == 1)
		{
			m_active_global_light_uid = light.m_uid;
		}

		m_universe.addComponent(entity, GLOBAL_LIGHT_HASH, this, light.m_uid);
		return light.m_uid;
	}


	ComponentIndex createPointLight(Entity entity)
	{
		PointLight& light = m_point_lights.emplace();
		m_light_influenced_geometry.push(Array<int>(m_allocator));
		light.m_entity = entity;
		light.m_diffuse_color.set(1, 1, 1);
		light.m_diffuse_intensity = 1;
		light.m_uid = ++m_point_light_last_uid;
		light.m_fov = 999;
		light.m_specular_color.set(1, 1, 1);
		light.m_specular_intensity = 1;
		light.m_cast_shadows = false;
		light.m_attenuation_param = 2;
		light.m_range = 10;
		m_point_lights_map.insert(light.m_uid, m_point_lights.size() - 1);

		m_universe.addComponent(entity, POINT_LIGHT_HASH, this, light.m_uid);

		detectLightInfluencedGeometry(m_point_lights.size() - 1);

		return light.m_uid;
	}


	ComponentIndex createRenderable(Entity entity)
	{
		while(entity >= m_renderables.size())
		{
			auto& r = m_renderables.emplace();
			r.entity = INVALID_ENTITY;
			r.model = nullptr;
			r.pose = nullptr;
		}
		auto& r = m_renderables[entity];
		r.entity = entity;
		r.model = nullptr;
		r.layer_mask = 1;
		r.meshes = nullptr;
		r.pose = nullptr;
		r.custom_meshes = false;
		r.mesh_count = 0;
		r.matrix = m_universe.getMatrix(entity);
		m_universe.addComponent(entity, RENDERABLE_HASH, this, entity);
		m_renderable_created.invoke(m_renderables.size() - 1);
		return entity;
	}


	void setParticleEmitterMaterialPath(ComponentIndex cmp, const Path& path) override
	{
		if (!m_particle_emitters[cmp]) return;

		auto* manager = m_engine.getResourceManager().get(ResourceManager::MATERIAL);
		Material* material = static_cast<Material*>(manager->load(path));
		m_particle_emitters[cmp]->setMaterial(material);
	}


	Path getParticleEmitterMaterialPath(ComponentIndex cmp) override
	{
		ParticleEmitter* emitter = m_particle_emitters[cmp];
		if (!emitter) return Path("");
		if (!emitter->getMaterial()) return Path("");

		return emitter->getMaterial()->getPath();
	}


	const Array<ParticleEmitter*>& getParticleEmitters() const override
	{
		return m_particle_emitters;
	}


private:
	IAllocator& m_allocator;
	Array<ModelLoadedCallback*> m_model_loaded_callbacks;

	Array<Renderable> m_renderables;

	int m_point_light_last_uid;
	Array<PointLight> m_point_lights;
	HashMap<ComponentIndex, int> m_point_lights_map;
	Array<Array<ComponentIndex>> m_light_influenced_geometry;
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
	Array<ParticleEmitter*> m_particle_emitters;
	Array<Array<RenderableMesh>> m_temporary_infos;
	MTJD::Group m_sync_point;
	Array<MTJD::Job*> m_jobs;
	float m_time;
	bool m_is_forward_rendered;
	bool m_is_grass_enabled;
	bool m_is_game_running;
	DelegateList<void(ComponentIndex)> m_renderable_created;
	DelegateList<void(ComponentIndex)> m_renderable_destroyed;
};



static struct
{
	uint32 type;
	ComponentIndex(RenderSceneImpl::*creator)(Entity);
	void (RenderSceneImpl::*destroyer)(ComponentIndex);
} COMPONENT_INFOS[] = {
	{RENDERABLE_HASH, &RenderSceneImpl::createRenderable, &RenderSceneImpl::destroyRenderable},
	{GLOBAL_LIGHT_HASH, &RenderSceneImpl::createGlobalLight, &RenderSceneImpl::destroyGlobalLight},
	{POINT_LIGHT_HASH, &RenderSceneImpl::createPointLight, &RenderSceneImpl::destroyPointLight},
	{CAMERA_HASH, &RenderSceneImpl::createCamera, &RenderSceneImpl::destroyCamera},
	{TERRAIN_HASH, &RenderSceneImpl::createTerrain, &RenderSceneImpl::destroyTerrain},
	{PARTICLE_EMITTER_HASH,
		&RenderSceneImpl::createParticleEmitter,
		&RenderSceneImpl::destroyParticleEmitter},
	{PARTICLE_EMITTER_FADE_HASH,
		&RenderSceneImpl::createParticleEmitterFade,
		&RenderSceneImpl::destroyParticleEmitterFade},
		{PARTICLE_EMITTER_FORCE_HASH,
		&RenderSceneImpl::createParticleEmitterForce,
		&RenderSceneImpl::destroyParticleEmitterForce},
	{PARTICLE_EMITTER_ATTRACTOR_HASH,
		&RenderSceneImpl::createParticleEmitterAttractor,
		&RenderSceneImpl::destroyParticleEmitterAttractor},
	{PARTICLE_EMITTER_SIZE_HASH,
		&RenderSceneImpl::createParticleEmitterSize,
		&RenderSceneImpl::destroyParticleEmitterSize},
	{PARTICLE_EMITTER_LINEAR_MOVEMENT_HASH,
		&RenderSceneImpl::createParticleEmitterLinearMovement,
		&RenderSceneImpl::destroyParticleEmitterLinearMovement},
	{PARTICLE_EMITTER_SPAWN_SHAPE_HASH,
		&RenderSceneImpl::createParticleEmitterSpawnShape,
		&RenderSceneImpl::destroyParticleEmitterSpawnShape },
	{PARTICLE_EMITTER_RANDOM_ROTATION_HASH,
		&RenderSceneImpl::createParticleEmitterRandomRotation,
		&RenderSceneImpl::destroyParticleEmitterRandomRotation},
	{PARTICLE_EMITTER_PLANE_HASH,
		&RenderSceneImpl::createParticleEmitterPlane,
		&RenderSceneImpl::destroyParticleEmitterPlane},
};


ComponentIndex RenderSceneImpl::createComponent(uint32 type, Entity entity)
{
	for (auto& i : COMPONENT_INFOS)
	{
		if (i.type == type)
		{
			return (this->*i.creator)(entity);
		}
	}

	return INVALID_COMPONENT;
}


void RenderSceneImpl::destroyComponent(ComponentIndex component, uint32 type)
{
	for (auto& i : COMPONENT_INFOS)
	{
		if (i.type == type)
		{
			(this->*i.destroyer)(component);
			return;
		}
	}
	ASSERT(false);
}


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