#include "render_scene.h"

#include "engine/array.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/fs/file_system.h"
#include "engine/geometry.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/math_utils.h"
#include "engine/plugin_manager.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/serializer.h"
#include "engine/universe/universe.h"
#include "lua_script/lua_script_system.h"
#include "renderer/culling_system.h"
#include "renderer/frame_buffer.h"
#include "renderer/material.h"
#include "renderer/material_manager.h"
#include "renderer/model.h"
#include "renderer/particle_system.h"
#include "renderer/pipeline.h"
#include "renderer/pose.h"
#include "renderer/renderer.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"
#include <cfloat>
#include <cmath>


namespace Lumix
{


enum class RenderSceneVersion : int
{
	GRASS_ROTATION_MODE,
	GLOBAL_LIGHT_REFACTOR,
	EMITTER_MATERIAL,
	BONE_ATTACHMENT_TRANSFORM,
	MODEL_INSTNACE_FLAGS,
	INDIRECT_INTENSITY,
	SCRIPTED_PARTICLES,
	POINT_LIGHT_NO_COMPONENT,
	MODEL_INSTANCE_ENABLE,

	LATEST
};


static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("renderable");
static const ComponentType DECAL_TYPE = Reflection::getComponentType("decal");
static const ComponentType POINT_LIGHT_TYPE = Reflection::getComponentType("point_light");
static const ComponentType PARTICLE_EMITTER_TYPE = Reflection::getComponentType("particle_emitter");
static const ComponentType SCRIPTED_PARTICLE_EMITTER_TYPE = Reflection::getComponentType("scripted_particle_emitter");
static const ComponentType PARTICLE_EMITTER_ALPHA_TYPE = Reflection::getComponentType("particle_emitter_alpha");
static const ComponentType PARTICLE_EMITTER_FORCE_HASH = Reflection::getComponentType("particle_emitter_force");
static const ComponentType PARTICLE_EMITTER_ATTRACTOR_TYPE =
	Reflection::getComponentType("particle_emitter_attractor");
static const ComponentType PARTICLE_EMITTER_SUBIMAGE_TYPE =
	Reflection::getComponentType("particle_emitter_subimage");
static const ComponentType PARTICLE_EMITTER_LINEAR_MOVEMENT_TYPE =
	Reflection::getComponentType("particle_emitter_linear_movement");
static const ComponentType PARTICLE_EMITTER_SPAWN_SHAPE_TYPE =
	Reflection::getComponentType("particle_emitter_spawn_shape");
static const ComponentType PARTICLE_EMITTER_PLANE_TYPE = Reflection::getComponentType("particle_emitter_plane");
static const ComponentType PARTICLE_EMITTER_RANDOM_ROTATION_TYPE =
	Reflection::getComponentType("particle_emitter_random_rotation");
static const ComponentType PARTICLE_EMITTER_SIZE_TYPE = Reflection::getComponentType("particle_emitter_size");
static const ComponentType GLOBAL_LIGHT_TYPE = Reflection::getComponentType("global_light");
static const ComponentType CAMERA_TYPE = Reflection::getComponentType("camera");
static const ComponentType TERRAIN_TYPE = Reflection::getComponentType("terrain");
static const ComponentType BONE_ATTACHMENT_TYPE = Reflection::getComponentType("bone_attachment");
static const ComponentType ENVIRONMENT_PROBE_TYPE = Reflection::getComponentType("environment_probe");


struct Decal : public DecalInfo
{
	Entity entity;
	Vec3 scale;
};


struct PointLight
{
	Vec3 m_diffuse_color;
	Vec3 m_specular_color;
	float m_diffuse_intensity;
	float m_specular_intensity;
	Entity m_entity;
	float m_fov;
	float m_attenuation_param;
	float m_range;
	bool m_cast_shadows;
};


struct GlobalLight
{
	Vec3 m_diffuse_color;
	float m_diffuse_intensity;
	float m_indirect_intensity;
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

	Entity entity;
	float fov;
	float aspect;
	float near;
	float far;
	float ortho_size;
	float screen_width;
	float screen_height;
	bool is_ortho;
	char slot[MAX_SLOT_LENGTH + 1];
};


struct EnvironmentProbe
{
	Texture* texture;
	Texture* irradiance;
	Texture* radiance;
	u64 guid;
};


struct BoneAttachment
{
	Entity entity;
	Entity parent_entity;
	int bone_index;
	RigidTransform relative_transform;
};


class RenderSceneImpl LUMIX_FINAL : public RenderScene
{
private:
	struct ModelLoadedCallback
	{
		ModelLoadedCallback(RenderSceneImpl& scene, Model* model)
			: m_scene(scene)
			, m_ref_count(0)
			, m_model(model)
		{
			m_model->getObserverCb().bind<RenderSceneImpl, &RenderSceneImpl::modelStateChanged>(&scene);
		}

		~ModelLoadedCallback()
		{
			m_model->getObserverCb().unbind<RenderSceneImpl, &RenderSceneImpl::modelStateChanged>(&m_scene);
		}

		Model* m_model;
		int m_ref_count;
		RenderSceneImpl& m_scene;
	};

public:
	RenderSceneImpl(Renderer& renderer,
		Engine& engine,
		Universe& universe,
		IAllocator& allocator);

	~RenderSceneImpl()
	{
		m_universe.entityTransformed().unbind<RenderSceneImpl, &RenderSceneImpl::onEntityMoved>(this);
		m_universe.entityDestroyed().unbind<RenderSceneImpl, &RenderSceneImpl::onEntityDestroyed>(this);
		CullingSystem::destroy(*m_culling_system);
	}


	void modelStateChanged(Resource::State old_state, Resource::State new_state, Resource& resource)
	{
		Model* model = static_cast<Model*>(&resource);
		if (new_state == Resource::State::READY)
		{
			modelLoaded(model);
		}
		else if (old_state == Resource::State::READY && new_state != Resource::State::READY)
		{
			modelUnloaded(model);
		}
	}


	void clear() override
	{
		auto& rm = m_engine.getResourceManager();
		auto* material_manager = static_cast<MaterialManager*>(rm.get(Material::TYPE));

		m_model_loaded_callbacks.clear();

		for (Decal& decal : m_decals)
		{
			if (decal.material) material_manager->unload(*decal.material);
		}
		m_decals.clear();

		m_cameras.clear();

		for (auto* terrain : m_terrains)
		{
			LUMIX_DELETE(m_allocator, terrain);
		}
		m_terrains.clear();

		for (auto* emitter : m_particle_emitters)
		{
			LUMIX_DELETE(m_allocator, emitter);
		}
		m_particle_emitters.clear();
		for (auto* emitter : m_scripted_particle_emitters)
		{
			LUMIX_DELETE(m_allocator, emitter);
		}
		m_scripted_particle_emitters.clear();

		for (auto& i : m_model_instances)
		{
			if (i.entity != INVALID_ENTITY && i.model)
			{
				freeCustomMeshes(i, material_manager);
				i.model->getResourceManager().unload(*i.model);
				LUMIX_DELETE(m_allocator, i.pose);
			}
		}
		m_model_instances.clear();
		m_culling_system->clear();

		for (auto& probe : m_environment_probes)
		{
			if (probe.texture) probe.texture->getResourceManager().unload(*probe.texture);
			if (probe.radiance) probe.radiance->getResourceManager().unload(*probe.radiance);
			if (probe.irradiance) probe.irradiance->getResourceManager().unload(*probe.irradiance);
		}
		m_environment_probes.clear();
	}


	void resetParticleEmitter(Entity entity) override
	{
		m_particle_emitters[entity]->reset();
	}


	ParticleEmitter* getParticleEmitter(Entity entity) override
	{
		return m_particle_emitters[entity];
	}


	void updateEmitter(Entity entity, float time_delta) override
	{
		m_particle_emitters[entity]->update(time_delta);
	}


	Universe& getUniverse() override { return m_universe; }


	IPlugin& getPlugin() const override { return m_renderer; }


	Int2 getParticleEmitterSpawnCount(Entity entity) override
	{
		Int2 ret;
		ParticleEmitter* emitter = m_particle_emitters[entity];
		ret.x = emitter->m_spawn_count.from;
		ret.y = emitter->m_spawn_count.to;
		return ret;
	}


	void setParticleEmitterSpawnCount(Entity entity, const Int2& value) override
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		emitter->m_spawn_count.from = value.x;
		emitter->m_spawn_count.to = Math::maximum(value.x, value.y);
	}



	void getRay(Entity camera_entity,
		const Vec2& screen_pos,
		Vec3& origin,
		Vec3& dir) override
	{
		Camera& camera = m_cameras[camera_entity];
		origin = m_universe.getPosition(camera_entity);

		float width = camera.screen_width;
		float height = camera.screen_height;
		if (width <= 0 || height <= 0)
		{
			dir = m_universe.getRotation(camera_entity).rotate(Vec3(0, 0, 1));
			return;
		}

		float nx = 2 * (screen_pos.x / width) - 1;
		float ny = 2 * ((height - screen_pos.y) / height) - 1;

		Matrix projection_matrix = getCameraProjection(camera_entity);
		Matrix view_matrix = m_universe.getMatrix(camera_entity);

		if (camera.is_ortho)
		{
			float ratio = camera.screen_height > 0 ? camera.screen_width / camera.screen_height : 1;
			origin += view_matrix.getXVector() * nx * camera.ortho_size * ratio
				+ view_matrix.getYVector() * ny * camera.ortho_size;
		}

		view_matrix.inverse();
		Matrix inverted = (projection_matrix * view_matrix);
		inverted.inverse();

		Vec4 p0 = inverted * Vec4(nx, ny, -1, 1);
		Vec4 p1 = inverted * Vec4(nx, ny, 1, 1);
		p0 *= 1 / p0.w;
		p1 *= 1 / p1.w;
		dir = p1 - p0;
		dir.normalize();
	}


	Frustum getCameraFrustum(Entity entity) const override
	{
		const Camera& camera = m_cameras[entity];
		Matrix mtx = m_universe.getMatrix(entity);
		Frustum ret;
		float ratio = camera.screen_height > 0 ? camera.screen_width / camera.screen_height : 1;
		if (camera.is_ortho)
		{
			ret.computeOrtho(mtx.getTranslation(),
				mtx.getZVector(),
				mtx.getYVector(),
				camera.ortho_size * ratio,
				camera.ortho_size,
				camera.near,
				camera.far);
			return ret;
		}
		ret.computePerspective(mtx.getTranslation(),
			-mtx.getZVector(),
			mtx.getYVector(),
			camera.fov,
			ratio,
			camera.near,
			camera.far);

		return ret;
	}


	Frustum getCameraFrustum(Entity entity, const Vec2& viewport_min_px, const Vec2& viewport_max_px) const override
	{
		const Camera& camera = m_cameras[entity];
		Matrix mtx = m_universe.getMatrix(entity);
		Frustum ret;
		float ratio = camera.screen_height > 0 ? camera.screen_width / camera.screen_height : 1;
		Vec2 viewport_min = { viewport_min_px.x / camera.screen_width * 2 - 1, (1 - viewport_max_px.y / camera.screen_height) * 2 - 1 };
		Vec2 viewport_max = { viewport_max_px.x / camera.screen_width * 2 - 1, (1 - viewport_min_px.y / camera.screen_height) * 2 - 1 };
		if (camera.is_ortho)
		{
			ret.computeOrtho(mtx.getTranslation(),
				mtx.getZVector(),
				mtx.getYVector(),
				camera.ortho_size * ratio,
				camera.ortho_size,
				camera.near,
				camera.far,
				viewport_min,
				viewport_max);
			return ret;
		}
		ret.computePerspective(mtx.getTranslation(),
			-mtx.getZVector(),
			mtx.getYVector(),
			camera.fov,
			ratio,
			camera.near,
			camera.far,
			viewport_min,
			viewport_max);

		return ret;
	}


	void updateBoneAttachment(const BoneAttachment& bone_attachment)
	{
		if (!bone_attachment.parent_entity.isValid()) return;
		Entity model_instance = bone_attachment.parent_entity;
		if (!model_instance.isValid()) return;
		if (!m_universe.hasComponent(model_instance, MODEL_INSTANCE_TYPE)) return;
		const Pose* parent_pose = lockPose(model_instance);
		if (!parent_pose) return;

		Transform parent_entity_transform = m_universe.getTransform(bone_attachment.parent_entity);
		int idx = bone_attachment.bone_index;
		if (idx < 0 || idx > parent_pose->count)
		{
			unlockPose(model_instance, false);
			return;
		}
		float original_scale = m_universe.getScale(bone_attachment.entity);
		Transform bone_transform = {parent_pose->positions[idx], parent_pose->rotations[idx], 1.0f};
		Transform relative_transform = { bone_attachment.relative_transform.pos, bone_attachment.relative_transform.rot, 1.0f};
		Transform result = parent_entity_transform * bone_transform * relative_transform;
		result.scale = original_scale;
		m_universe.setTransform(bone_attachment.entity, result);
		unlockPose(model_instance, false);
	}


	Entity getBoneAttachmentParent(Entity entity) override
	{
		return m_bone_attachments[entity].parent_entity;
	}


	void updateRelativeMatrix(BoneAttachment& attachment)
	{
		if (attachment.parent_entity == INVALID_ENTITY) return;
		if (attachment.bone_index < 0) return;
		Entity model_instance = attachment.parent_entity;
		if (!model_instance.isValid()) return;
		if (!m_universe.hasComponent(model_instance, MODEL_INSTANCE_TYPE)) return;
		const Pose* pose = lockPose(model_instance);
		if (!pose) return;
		ASSERT(pose->is_absolute);
		if (attachment.bone_index >= pose->count)
		{
			unlockPose(model_instance, false);
			return;
		}
		Transform bone_transform = {pose->positions[attachment.bone_index], pose->rotations[attachment.bone_index], 1.0f};

		Transform inv_parent_transform = m_universe.getTransform(attachment.parent_entity) * bone_transform;
		inv_parent_transform = inv_parent_transform.inverted();
		Transform child_transform = m_universe.getTransform(attachment.entity);
		Transform res = inv_parent_transform * child_transform;
		attachment.relative_transform = {res.pos, res.rot};
		unlockPose(model_instance, false);
	}


	Vec3 getBoneAttachmentPosition(Entity entity) override
	{
		return m_bone_attachments[entity].relative_transform.pos;
	}


	void setBoneAttachmentPosition(Entity entity, const Vec3& pos) override
	{
		BoneAttachment& attachment = m_bone_attachments[entity];
		attachment.relative_transform.pos = pos;
		m_is_updating_attachments = true;
		updateBoneAttachment(attachment);
		m_is_updating_attachments = false;
	}


	Vec3 getBoneAttachmentRotation(Entity entity) override
	{
		return m_bone_attachments[entity].relative_transform.rot.toEuler();
	}


	void setBoneAttachmentRotation(Entity entity, const Vec3& rot) override
	{
		BoneAttachment& attachment = m_bone_attachments[entity];
		Vec3 euler = rot;
		euler.x = Math::clamp(euler.x, -Math::PI * 0.5f, Math::PI * 0.5f);
		attachment.relative_transform.rot.fromEuler(euler);
		m_is_updating_attachments = true;
		updateBoneAttachment(attachment);
		m_is_updating_attachments = false;
	}


	void setBoneAttachmentRotationQuat(Entity entity, const Quat& rot) override
	{
		BoneAttachment& attachment = m_bone_attachments[entity];
		attachment.relative_transform.rot = rot;
		m_is_updating_attachments = true;
		updateBoneAttachment(attachment);
		m_is_updating_attachments = false;
	}


	int getBoneAttachmentBone(Entity entity) override
	{
		return m_bone_attachments[entity].bone_index;
	}


	void setBoneAttachmentBone(Entity entity, int value) override
	{
		BoneAttachment& ba = m_bone_attachments[entity];
		ba.bone_index = value;
		updateRelativeMatrix(ba);
	}


	void setBoneAttachmentParent(Entity entity, Entity parent) override
	{
		BoneAttachment& ba = m_bone_attachments[entity];
		ba.parent_entity = entity;
		if (entity.isValid() && entity.index < m_model_instances.size())
		{
			ModelInstance& mi = m_model_instances[entity.index];
			mi.flags.set(ModelInstance::IS_BONE_ATTACHMENT_PARENT);
		}
		updateRelativeMatrix(ba);
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
		for (int i = m_debug_triangles.size() - 1; i >= 0; --i)
		{
			float life = m_debug_triangles[i].life;
			if (life < 0)
			{
				m_debug_triangles.eraseFast(i);
			}
			else
			{
				life -= dt;
				m_debug_triangles[i].life = life;
			}
		}

		for(int i = m_debug_lines.size() - 1; i >= 0; --i)
		{
			float life = m_debug_lines[i].life;
			if(life < 0)
			{
				m_debug_lines.eraseFast(i);
			}
			else
			{
				life -= dt;
				m_debug_lines[i].life = life;
			}
		}


		for (int i = m_debug_points.size() - 1; i >= 0; --i)
		{
			float life = m_debug_points[i].life;
			if (life < 0)
			{
				m_debug_points.eraseFast(i);
			}
			else
			{
				life -= dt;
				m_debug_points[i].life = life;
			}
		}

		if (m_is_game_running && !paused)
		{
			for (auto* emitter : m_particle_emitters)
			{
				if (emitter->m_is_valid) emitter->update(dt);
			}
			for (auto* emitter : m_scripted_particle_emitters)
			{
				emitter->update(dt);
			}
		}
	}


	void serializeModelInstance(ISerializer& serialize, Entity entity)
	{
		ModelInstance& r = m_model_instances[entity.index];
		ASSERT(r.entity != INVALID_ENTITY);

		serialize.write("source", r.model ? r.model->getPath().c_str() : "");
		serialize.write("flags", u8(r.flags.base & ModelInstance::PERSISTENT_FLAGS));
		bool has_changed_materials = r.model && r.model->isReady() && r.meshes != &r.model->getMesh(0);
		serialize.write("custom_materials", has_changed_materials ? r.mesh_count : 0);
		if (has_changed_materials)
		{
			for (int i = 0; i < r.mesh_count; ++i)
			{
				serialize.write("", r.meshes[i].material->getPath().c_str());
			}
		}
	}


	static bool keepSkin(ModelInstance& r)
	{
		return r.flags.isSet(ModelInstance::KEEP_SKIN);
	}


	static bool hasCustomMeshes(ModelInstance& r)
	{
		return r.flags.isSet(ModelInstance::CUSTOM_MESHES);
	}


	void deserializeModelInstance(IDeserializer& serializer, Entity entity, int scene_version)
	{
		while (entity.index >= m_model_instances.size())
		{
			auto& r = m_model_instances.emplace();
			r.entity = INVALID_ENTITY;
			r.pose = nullptr;
			r.model = nullptr;
			r.meshes = nullptr;
			r.mesh_count = 0;
		}
		auto& r = m_model_instances[entity.index];
		r.entity = entity;
		r.model = nullptr;
		r.pose = nullptr;
		r.flags.clear();
		r.flags.set(ModelInstance::ENABLED);
		r.meshes = nullptr;
		r.mesh_count = 0;

		r.matrix = m_universe.getMatrix(r.entity);

		char path[MAX_PATH_LENGTH];
		serializer.read(path, lengthOf(path));
		if (scene_version > (int)RenderSceneVersion::MODEL_INSTNACE_FLAGS)
		{
			serializer.read(&r.flags.base);
			r.flags.base &= ModelInstance::PERSISTENT_FLAGS;

			if (scene_version <= (int)RenderSceneVersion::MODEL_INSTANCE_ENABLE)
			{
				r.flags.set(ModelInstance::ENABLED);
			}
		}

		if (path[0] != 0)
		{
			auto* model = static_cast<Model*>(m_engine.getResourceManager().get(Model::TYPE)->load(Path(path)));
			setModel(r.entity, model);
		}

		int material_count;
		serializer.read(&material_count);
		if (material_count > 0)
		{
			allocateCustomMeshes(r, material_count);
			for (int j = 0; j < material_count; ++j)
			{
				char path[MAX_PATH_LENGTH];
				serializer.read(path, lengthOf(path));
				setModelInstanceMaterial(r.entity, j, Path(path));
			}
		}

		m_universe.onComponentCreated(r.entity, MODEL_INSTANCE_TYPE, this);
	}


	void serializeGlobalLight(ISerializer& serializer, Entity entity)
	{
		GlobalLight& light = m_global_lights[entity];
		serializer.write("cascades", light.m_cascades);
		serializer.write("diffuse_color", light.m_diffuse_color);
		serializer.write("diffuse_intensity", light.m_diffuse_intensity);
		serializer.write("indirect_intensity", light.m_indirect_intensity);
		serializer.write("fog_bottom", light.m_fog_bottom);
		serializer.write("fog_color", light.m_fog_color);
		serializer.write("fog_density", light.m_fog_density);
		serializer.write("fog_height", light.m_fog_height);
	}


	void deserializeGlobalLight(IDeserializer& serializer, Entity entity, int scene_version)
	{
		GlobalLight light;
		light.m_entity = entity;
		serializer.read(&light.m_cascades);
		if (scene_version <= (int)RenderSceneVersion::GLOBAL_LIGHT_REFACTOR)
		{
			int dummy;
			serializer.read(&dummy);
		}
		serializer.read(&light.m_diffuse_color);
		serializer.read(&light.m_diffuse_intensity);
		if (scene_version > (int)RenderSceneVersion::INDIRECT_INTENSITY)
		{
			serializer.read(&light.m_indirect_intensity);
		}
		else
		{
			light.m_indirect_intensity = 1;
		}
		serializer.read(&light.m_fog_bottom);
		serializer.read(&light.m_fog_color);
		serializer.read(&light.m_fog_density);
		serializer.read(&light.m_fog_height);
		m_global_lights.insert(entity, light);
		m_universe.onComponentCreated(light.m_entity, GLOBAL_LIGHT_TYPE, this);
		m_active_global_light_entity = entity;
	}
	
	
	void serializePointLight(ISerializer& serializer, Entity entity)
	{
		PointLight& light = m_point_lights[m_point_lights_map[entity]];
		serializer.write("attenuation", light.m_attenuation_param);
		serializer.write("cast_shadow", light.m_cast_shadows);
		serializer.write("diffuse_color", light.m_diffuse_color);
		serializer.write("diffuse_intensity", light.m_diffuse_intensity);
		serializer.write("fov", light.m_fov);
		serializer.write("range", light.m_range);
		serializer.write("specular_color", light.m_specular_color);
		serializer.write("specular_intensity", light.m_specular_intensity);
	}


	void deserializePointLight(IDeserializer& serializer, Entity entity, int scene_version)
	{
		m_light_influenced_geometry.emplace(m_allocator);
		PointLight& light = m_point_lights.emplace();
		light.m_entity = entity;
		serializer.read(&light.m_attenuation_param);
		serializer.read(&light.m_cast_shadows);
		
		if (scene_version <= (int)RenderSceneVersion::POINT_LIGHT_NO_COMPONENT)
		{
			int dummy;
			serializer.read(&dummy);
		}
		serializer.read(&light.m_diffuse_color);
		serializer.read(&light.m_diffuse_intensity);
		serializer.read(&light.m_fov);
		serializer.read(&light.m_range);
		serializer.read(&light.m_specular_color);
		serializer.read(&light.m_specular_intensity);
		m_point_lights_map.insert(light.m_entity, m_point_lights.size() - 1);

		m_universe.onComponentCreated(light.m_entity, POINT_LIGHT_TYPE, this);
	}


	void serializeDecal(ISerializer& serializer, Entity entity)
	{
		const Decal& decal = m_decals[entity];
		serializer.write("scale", decal.scale);
		serializer.write("material", decal.material ? decal.material->getPath().c_str() : "");
	}


	void deserializeDecal(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		ResourceManagerBase* material_manager = m_engine.getResourceManager().get(Material::TYPE);
		Decal& decal = m_decals.insert(entity);
		char tmp[MAX_PATH_LENGTH];
		decal.entity = entity;
		serializer.read(&decal.scale);
		serializer.read(tmp, lengthOf(tmp));
		decal.material = tmp[0] == '\0' ? nullptr : static_cast<Material*>(material_manager->load(Path(tmp)));
		updateDecalInfo(decal);
		m_universe.onComponentCreated(decal.entity, DECAL_TYPE, this);
	}


	void serializeCamera(ISerializer& serialize, Entity entity)
	{
		Camera& camera = m_cameras[entity];
		serialize.write("far", camera.far);
		serialize.write("fov", camera.fov);
		serialize.write("is_ortho", camera.is_ortho);
		serialize.write("ortho_size", camera.ortho_size);
		serialize.write("near", camera.near);
		serialize.write("slot", camera.slot);
	}


	void deserializeCamera(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		Camera camera;
		camera.entity = entity;
		serializer.read(&camera.far);
		serializer.read(&camera.fov);
		serializer.read(&camera.is_ortho);
		serializer.read(&camera.ortho_size);
		serializer.read(&camera.near);
		serializer.read(camera.slot, lengthOf(camera.slot));
		m_cameras.insert(camera.entity, camera);
		m_universe.onComponentCreated(camera.entity, CAMERA_TYPE, this);
	}


	void serializeBoneAttachment(ISerializer& serializer, Entity entity) 
	{
		BoneAttachment& attachment = m_bone_attachments[entity];
		serializer.write("bone_index", attachment.bone_index);
		serializer.write("parent", attachment.parent_entity);
		serializer.write("relative_transform", attachment.relative_transform);
	}


	void deserializeBoneAttachment(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		BoneAttachment& bone_attachment = m_bone_attachments.emplace(entity);
		bone_attachment.entity = entity;
		serializer.read(&bone_attachment.bone_index);
		serializer.read(&bone_attachment.parent_entity);
		serializer.read(&bone_attachment.relative_transform);
		m_universe.onComponentCreated(bone_attachment.entity, BONE_ATTACHMENT_TYPE, this);
		Entity parent_entity = bone_attachment.parent_entity;
		if (parent_entity.isValid() && parent_entity.index < m_model_instances.size())
		{
			ModelInstance& mi = m_model_instances[parent_entity.index];
			mi.flags.set(ModelInstance::IS_BONE_ATTACHMENT_PARENT);
		}
	}


	void serializeTerrain(ISerializer& serializer, Entity entity)
	{
		Terrain* terrain = m_terrains[entity];
		serializer.write("layer_mask", terrain->m_layer_mask);
		serializer.write("scale", terrain->m_scale);
		serializer.write("material", terrain->m_material ? terrain->m_material->getPath().c_str() : "");
		serializer.write("grass_count", terrain->m_grass_types.size());
		for (Terrain::GrassType& type : terrain->m_grass_types)
		{
			serializer.write("density", type.m_density);
			serializer.write("distance", type.m_distance);
			serializer.write("rotation_mode", (int)type.m_rotation_mode);
			serializer.write("model", type.m_grass_model ? type.m_grass_model->getPath().c_str() : "");
		}
	}

	void deserializeTerrain(IDeserializer& serializer, Entity entity, int version)
	{
		Terrain* terrain = LUMIX_NEW(m_allocator, Terrain)(m_renderer, entity, *this, m_allocator);
		m_terrains.insert(entity, terrain);
		terrain->m_entity = entity;
		serializer.read(&terrain->m_layer_mask);
		serializer.read(&terrain->m_scale);
		char tmp[MAX_PATH_LENGTH];
		serializer.read(tmp, lengthOf(tmp));
		auto* material = tmp[0] ? m_engine.getResourceManager().get(Material::TYPE)->load(Path(tmp)) : nullptr;
		terrain->setMaterial((Material*)material);

		int count;
		serializer.read(&count);
		for(int i = 0; i < count; ++i)
		{
			Terrain::GrassType type(*terrain);
			serializer.read(&type.m_density);
			serializer.read(&type.m_distance);
			if (version >= (int)RenderSceneVersion::GRASS_ROTATION_MODE)
			{
				serializer.read((int*)&type.m_rotation_mode);
			}
			type.m_idx = i;
			serializer.read(tmp, lengthOf(tmp));
			terrain->m_grass_types.push(type);
			terrain->setGrassTypePath(terrain->m_grass_types.size() - 1, Path(tmp));
		}

		m_universe.onComponentCreated(entity, TERRAIN_TYPE, this);
	}

	void serializeEnvironmentProbe(ISerializer& serializer, Entity entity) 
	{
		serializer.write("guid", m_environment_probes[entity].guid);
	}


	int getVersion() const override { return (int)RenderSceneVersion::LATEST; }


	void deserializeEnvironmentProbe(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		auto* texture_manager = m_engine.getResourceManager().get(Texture::TYPE);
		StaticString<MAX_PATH_LENGTH> probe_dir("universes/", m_universe.getName(), "/probes/");
		EnvironmentProbe& probe = m_environment_probes.insert(entity);
		serializer.read(&probe.guid);
		StaticString<MAX_PATH_LENGTH> path_str(probe_dir, probe.guid, ".dds");
		probe.texture = static_cast<Texture*>(texture_manager->load(Path(path_str)));
		probe.texture->setFlag(BGFX_TEXTURE_SRGB, true);
		StaticString<MAX_PATH_LENGTH> irr_path_str(probe_dir, probe.guid, "_irradiance.dds");
		probe.irradiance = static_cast<Texture*>(texture_manager->load(Path(irr_path_str)));
		probe.irradiance->setFlag(BGFX_TEXTURE_SRGB, true);
		probe.irradiance->setFlag(BGFX_TEXTURE_MIN_ANISOTROPIC, true);
		probe.irradiance->setFlag(BGFX_TEXTURE_MAG_ANISOTROPIC, true);
		StaticString<MAX_PATH_LENGTH> r_path_str(probe_dir, probe.guid, "_radiance.dds");
		probe.radiance = static_cast<Texture*>(texture_manager->load(Path(r_path_str)));
		probe.radiance->setFlag(BGFX_TEXTURE_SRGB, true);
		probe.radiance->setFlag(BGFX_TEXTURE_MIN_ANISOTROPIC, true);
		probe.radiance->setFlag(BGFX_TEXTURE_MAG_ANISOTROPIC, true);

		m_universe.onComponentCreated(entity, ENVIRONMENT_PROBE_TYPE, this);
	}


	void serializeScriptedParticleEmitter(ISerializer& serializer, Entity entity)
	{
		ScriptedParticleEmitter* emitter = m_scripted_particle_emitters[entity];
		const Material* material = emitter->getMaterial();
		serializer.write("material", material ? material->getPath().c_str() : "");
	}


	void deserializeScriptedParticleEmitter(IDeserializer& serializer, Entity entity, int scene_version)
	{
		ScriptedParticleEmitter* emitter = LUMIX_NEW(m_allocator, ScriptedParticleEmitter)(entity, m_allocator);
		emitter->m_entity = entity;

		char tmp[MAX_PATH_LENGTH];
		serializer.read(tmp, lengthOf(tmp));
		ResourceManagerBase* material_manager = m_engine.getResourceManager().get(Material::TYPE);
		Material* material = (Material*)material_manager->load(Path(tmp));
		emitter->setMaterial(material);

		m_scripted_particle_emitters.insert(entity, emitter);
		m_universe.onComponentCreated(entity, SCRIPTED_PARTICLE_EMITTER_TYPE, this);
	}


	void serializeParticleEmitter(ISerializer& serializer, Entity entity)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		serializer.write("autoemit", emitter->m_autoemit);
		serializer.write("local_space", emitter->m_local_space);
		serializer.write("spawn_period_from", emitter->m_spawn_period.from);
		serializer.write("spawn_period_to", emitter->m_spawn_period.to);
		serializer.write("initial_life_from", emitter->m_initial_life.from);
		serializer.write("initial_life_to", emitter->m_initial_life.to);
		serializer.write("initial_size_from", emitter->m_initial_size.from);
		serializer.write("initial_size_to", emitter->m_initial_size.to);
		serializer.write("spawn_count_from", emitter->m_spawn_count.from);
		serializer.write("spawn_count_to", emitter->m_spawn_count.to);
		const Material* material = emitter->getMaterial();
		serializer.write("material", material ? material->getPath().c_str() : "");
	}


	void deserializeParticleEmitter(IDeserializer& serializer, Entity entity, int scene_version)
	{
		ParticleEmitter* emitter = LUMIX_NEW(m_allocator, ParticleEmitter)(entity, m_universe, m_allocator);
		emitter->m_entity = entity;
		serializer.read(&emitter->m_autoemit);
		serializer.read(&emitter->m_local_space);
		serializer.read(&emitter->m_spawn_period.from);
		serializer.read(&emitter->m_spawn_period.to);
		serializer.read(&emitter->m_initial_life.from);
		serializer.read(&emitter->m_initial_life.to);
		serializer.read(&emitter->m_initial_size.from);
		serializer.read(&emitter->m_initial_size.to);
		serializer.read(&emitter->m_spawn_count.from);
		serializer.read(&emitter->m_spawn_count.to);
		if (scene_version > (int)RenderSceneVersion::EMITTER_MATERIAL)
		{
			char tmp[MAX_PATH_LENGTH];
			serializer.read(tmp, lengthOf(tmp));
			ResourceManagerBase* material_manager = m_engine.getResourceManager().get(Material::TYPE);
			Material* material = (Material*)material_manager->load(Path(tmp));
			emitter->setMaterial(material);
		}

		m_particle_emitters.insert(entity, emitter);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_TYPE, this);
	}

	void serializeParticleEmitterAlpha(ISerializer& serializer, Entity entity)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = (ParticleEmitter::AlphaModule*)emitter->getModule(PARTICLE_EMITTER_ALPHA_TYPE);
		serializer.write("count", module->m_values.size());
		for (Vec2 v : module->m_values)
		{
			serializer.write("", v.x);
			serializer.write("", v.y);
		}
	}
	
	
	void deserializeParticleEmitterAlpha(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = LUMIX_NEW(m_allocator, ParticleEmitter::AlphaModule)(*emitter);
		int count;
		serializer.read(&count);
		module->m_values.clear();
		for (int i = 0; i < count; ++i)
		{
			Vec2& v = module->m_values.emplace();
			serializer.read(&v.x);
			serializer.read(&v.y);
		}
		module->sample();
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_ALPHA_TYPE, this);
	}


	void serializeParticleEmitterAttractor(ISerializer& serializer, Entity entity)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = (ParticleEmitter::AttractorModule*)emitter->getModule(PARTICLE_EMITTER_ATTRACTOR_TYPE);
		serializer.write("force", module->m_force);
		serializer.write("count", module->m_count);
		for (int i = 0; i < module->m_count; ++i)
		{
			serializer.write("", module->m_entities[i]);
		}
	}


	void deserializeParticleEmitterAttractor(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = LUMIX_NEW(m_allocator, ParticleEmitter::AttractorModule)(*emitter);
		serializer.read(&module->m_force);
		serializer.read(&module->m_count);
		for (int i = 0; i < module->m_count; ++i)
		{
			serializer.read(&module->m_entities[i]);
		}
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_ATTRACTOR_TYPE, this);
	}

	void serializeParticleEmitterForce(ISerializer& serializer, Entity entity)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = (ParticleEmitter::ForceModule*)emitter->getModule(PARTICLE_EMITTER_FORCE_HASH);
		serializer.write("acceleration", module->m_acceleration);
	}


	void deserializeParticleEmitterForce(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = LUMIX_NEW(m_allocator, ParticleEmitter::ForceModule)(*emitter);
		serializer.read(&module->m_acceleration);
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_FORCE_HASH, this);
	}


	void serializeParticleEmitterLinearMovement(ISerializer& serializer, Entity entity)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = (ParticleEmitter::LinearMovementModule*)emitter->getModule(PARTICLE_EMITTER_LINEAR_MOVEMENT_TYPE);
		serializer.write("x_from", module->m_x.from);
		serializer.write("x_to", module->m_x.to);
		serializer.write("y_from", module->m_y.from);
		serializer.write("y_to", module->m_y.to);
		serializer.write("z_from", module->m_z.from);
		serializer.write("z_to", module->m_z.to);
	}


	void deserializeParticleEmitterLinearMovement(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = LUMIX_NEW(m_allocator, ParticleEmitter::LinearMovementModule)(*emitter);
		serializer.read(&module->m_x.from);
		serializer.read(&module->m_x.to);
		serializer.read(&module->m_y.from);
		serializer.read(&module->m_y.to);
		serializer.read(&module->m_z.from);
		serializer.read(&module->m_z.to);
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_LINEAR_MOVEMENT_TYPE, this);
	}


	void serializeParticleEmitterPlane(ISerializer& serializer, Entity entity)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = (ParticleEmitter::PlaneModule*)emitter->getModule(PARTICLE_EMITTER_PLANE_TYPE);
		serializer.write("bounce", module->m_bounce);
		serializer.write("entities_count", module->m_count);
		for (int i = 0; i < module->m_count; ++i)
		{
			serializer.write("", module->m_entities[i]);
		}
	}


	void deserializeParticleEmitterPlane(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = LUMIX_NEW(m_allocator, ParticleEmitter::PlaneModule)(*emitter);
		serializer.read(&module->m_bounce);
		serializer.read(&module->m_count);
		for (int i = 0; i < module->m_count; ++i)
		{
			serializer.read(&module->m_entities[i]);
		}
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_PLANE_TYPE, this);
	}

	void serializeParticleEmitterSpawnShape(ISerializer& serializer, Entity entity)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = (ParticleEmitter::SpawnShapeModule*)emitter->getModule(PARTICLE_EMITTER_SPAWN_SHAPE_TYPE);
		serializer.write("shape", (u8)module->m_shape);
		serializer.write("radius", module->m_radius);
	}


	void deserializeParticleEmitterSpawnShape(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = LUMIX_NEW(m_allocator, ParticleEmitter::SpawnShapeModule)(*emitter);
		serializer.read((u8*)&module->m_shape);
		serializer.read(&module->m_radius);
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_SPAWN_SHAPE_TYPE, this);
	}

	void serializeParticleEmitterSize(ISerializer& serializer, Entity entity)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = (ParticleEmitter::SizeModule*)emitter->getModule(PARTICLE_EMITTER_SIZE_TYPE);
		serializer.write("count", module->m_values.size());
		for (Vec2 v : module->m_values)
		{
			serializer.write("", v.x);
			serializer.write("", v.y);
		}
	}


	void deserializeParticleEmitterSize(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = LUMIX_NEW(m_allocator, ParticleEmitter::SizeModule)(*emitter);
		int count;
		serializer.read(&count);
		module->m_values.clear();
		for (int i = 0; i < count; ++i)
		{
			Vec2& v = module->m_values.emplace();
			serializer.read(&v.x);
			serializer.read(&v.y);
		}
		module->sample();
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_SIZE_TYPE, this);
	}


	void serializeParticleEmitterRandomRotation(ISerializer& serialize, Entity entity) {}


	void deserializeParticleEmitterRandomRotation(IDeserializer& serialize, Entity entity, int /*scene_version*/)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = LUMIX_NEW(m_allocator, ParticleEmitter::RandomRotationModule)(*emitter);
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_RANDOM_ROTATION_TYPE, this);
	}


	void serializeParticleEmitterSubimage(ISerializer& serializer, Entity entity)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = (ParticleEmitter::SubimageModule*)emitter->getModule(PARTICLE_EMITTER_SUBIMAGE_TYPE);
		serializer.write("rows", module->rows);
		serializer.write("cols", module->cols);
	}


	void deserializeParticleEmitterSubimage(IDeserializer& serializer, Entity entity, int /*scene_version*/)
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		auto* module = LUMIX_NEW(m_allocator, ParticleEmitter::SubimageModule)(*emitter);
		serializer.read(&module->rows);
		serializer.read(&module->cols);
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_SUBIMAGE_TYPE, this);
	}


	void serializeBoneAttachments(OutputBlob& serializer)
	{
		serializer.write((i32)m_bone_attachments.size());
		for (auto& attachment : m_bone_attachments)
		{
			serializer.write(attachment.bone_index);
			serializer.write(attachment.entity);
			serializer.write(attachment.parent_entity);
			serializer.write(attachment.relative_transform);
		}
	}

	void serializeCameras(OutputBlob& serializer)
	{
		serializer.write((i32)m_cameras.size());
		for (auto& camera : m_cameras)
		{
			serializer.write(camera.entity);
			serializer.write(camera.far);
			serializer.write(camera.fov);
			serializer.write(camera.is_ortho);
			serializer.write(camera.ortho_size);
			serializer.write(camera.near);
			serializer.writeString(camera.slot);
		}
	}

	void serializeLights(OutputBlob& serializer)
	{
		serializer.write((i32)m_point_lights.size());
		for (int i = 0, c = m_point_lights.size(); i < c; ++i)
		{
			serializer.write(m_point_lights[i]);
		}

		serializer.write((i32)m_global_lights.size());
		for (const GlobalLight& light : m_global_lights)
		{
			serializer.write(light);
		}
		serializer.write(m_active_global_light_entity);
	}

	void serializeModelInstances(OutputBlob& serializer)
	{
		serializer.write((i32)m_model_instances.size());
		for (auto& r : m_model_instances)
		{
			serializer.write(r.entity);
			serializer.write(u8(r.flags.base & ModelInstance::PERSISTENT_FLAGS));
			if(r.entity != INVALID_ENTITY)
			{
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
		serializer.write((i32)m_terrains.size());
		for (auto* terrain : m_terrains)
		{
			terrain->serialize(serializer);
		}
	}


	void deserializeDecals(InputBlob& serializer)
	{
		ResourceManagerBase* material_manager = m_engine.getResourceManager().get(Material::TYPE);
		int count;
		serializer.read(count);
		m_decals.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			char tmp[MAX_PATH_LENGTH];
			Decal decal;
			serializer.read(decal.entity);
			serializer.read(decal.scale);
			serializer.readString(tmp, lengthOf(tmp));
			decal.material = tmp[0] == '\0' ? nullptr : static_cast<Material*>(material_manager->load(Path(tmp)));
			updateDecalInfo(decal);
			m_decals.insert(decal.entity, decal);
			m_universe.onComponentCreated(decal.entity, DECAL_TYPE, this);
		}
	}


	void serializeDecals(OutputBlob& serializer)
	{
		serializer.write(m_decals.size());
		for (auto& decal : m_decals)
		{
			serializer.write(decal.entity);
			serializer.write(decal.scale);
			serializer.writeString(decal.material ? decal.material->getPath().c_str() : "");
		}
	}


	void serializeEnvironmentProbes(OutputBlob& serializer)
	{
		i32 count = m_environment_probes.size();
		serializer.write(count);
		for (int i = 0; i < count; ++i)
		{
			Entity entity = m_environment_probes.getKey(i);
			serializer.write(entity);
			serializer.write(m_environment_probes.at(i).guid);
		}
	}


	void deserializeEnvironmentProbes(InputBlob& serializer)
	{
		i32 count;
		serializer.read(count);
		m_environment_probes.reserve(count);
		auto* texture_manager = m_engine.getResourceManager().get(Texture::TYPE);
		StaticString<MAX_PATH_LENGTH> probe_dir("universes/", m_universe.getName(), "/probes/");
		for (int i = 0; i < count; ++i)
		{
			Entity entity;
			serializer.read(entity);
			EnvironmentProbe& probe = m_environment_probes.insert(entity);
			serializer.read(probe.guid);
			StaticString<MAX_PATH_LENGTH> path_str(probe_dir, probe.guid, ".dds");
			probe.texture = static_cast<Texture*>(texture_manager->load(Path(path_str)));
			probe.texture->setFlag(BGFX_TEXTURE_SRGB, true);
			StaticString<MAX_PATH_LENGTH> irr_path_str(probe_dir, probe.guid, "_irradiance.dds");
			probe.irradiance = static_cast<Texture*>(texture_manager->load(Path(irr_path_str)));
			probe.irradiance->setFlag(BGFX_TEXTURE_SRGB, true);
			probe.irradiance->setFlag(BGFX_TEXTURE_MIN_ANISOTROPIC, true);
			probe.irradiance->setFlag(BGFX_TEXTURE_MAG_ANISOTROPIC, true);
			StaticString<MAX_PATH_LENGTH> r_path_str(probe_dir, probe.guid, "_radiance.dds");
			probe.radiance = static_cast<Texture*>(texture_manager->load(Path(r_path_str)));
			probe.radiance->setFlag(BGFX_TEXTURE_SRGB, true);
			probe.radiance->setFlag(BGFX_TEXTURE_MIN_ANISOTROPIC, true);
			probe.radiance->setFlag(BGFX_TEXTURE_MAG_ANISOTROPIC, true);

			m_universe.onComponentCreated(entity, ENVIRONMENT_PROBE_TYPE, this);
		}
	}


	void deserializeBoneAttachments(InputBlob& serializer)
	{
		i32 count;
		serializer.read(count);
		m_bone_attachments.clear();
		m_bone_attachments.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			BoneAttachment bone_attachment;
			serializer.read(bone_attachment.bone_index);
			serializer.read(bone_attachment.entity);
			serializer.read(bone_attachment.parent_entity);
			serializer.read(bone_attachment.relative_transform);
			m_bone_attachments.insert(bone_attachment.entity, bone_attachment);
			m_universe.onComponentCreated(bone_attachment.entity, BONE_ATTACHMENT_TYPE, this);
		}
	}


	void deserializeParticleEmitters(InputBlob& serializer)
	{
		int count;
		serializer.read(count);
		m_particle_emitters.reserve(count);
		for(int i = 0; i < count; ++i)
		{
			ParticleEmitter* emitter = LUMIX_NEW(m_allocator, ParticleEmitter)(INVALID_ENTITY, m_universe, m_allocator);
			serializer.read(emitter->m_is_valid);
			if (emitter->m_is_valid)
			{
				emitter->deserialize(serializer, m_engine.getResourceManager());
				if (emitter->m_is_valid) m_universe.onComponentCreated(emitter->m_entity, PARTICLE_EMITTER_TYPE, this);
				for (auto* module : emitter->m_modules)
				{
					if (module->getType() == ParticleEmitter::AlphaModule::s_type)
					{
						m_universe.onComponentCreated(emitter->m_entity, PARTICLE_EMITTER_ALPHA_TYPE, this);
					}
					else if (module->getType() == ParticleEmitter::ForceModule::s_type)
					{
						m_universe.onComponentCreated(emitter->m_entity, PARTICLE_EMITTER_FORCE_HASH, this);
					}
					else if (module->getType() == ParticleEmitter::SubimageModule::s_type)
					{
						m_universe.onComponentCreated(emitter->m_entity, PARTICLE_EMITTER_SUBIMAGE_TYPE, this);
					}
					else if (module->getType() == ParticleEmitter::SpawnShapeModule::s_type)
					{
						m_universe.onComponentCreated(emitter->m_entity, PARTICLE_EMITTER_SPAWN_SHAPE_TYPE, this);
					}
					else if (module->getType() == ParticleEmitter::AttractorModule::s_type)
					{
						m_universe.onComponentCreated(emitter->m_entity, PARTICLE_EMITTER_ATTRACTOR_TYPE, this);
					}
					else if (module->getType() == ParticleEmitter::LinearMovementModule::s_type)
					{
						m_universe.onComponentCreated(emitter->m_entity, PARTICLE_EMITTER_LINEAR_MOVEMENT_TYPE, this);
					}
					else if (module->getType() == ParticleEmitter::PlaneModule::s_type)
					{
						m_universe.onComponentCreated(emitter->m_entity, PARTICLE_EMITTER_PLANE_TYPE, this);
					}
					else if (module->getType() == ParticleEmitter::RandomRotationModule::s_type)
					{
						m_universe.onComponentCreated(emitter->m_entity, PARTICLE_EMITTER_RANDOM_ROTATION_TYPE, this);
					}
					else if (module->getType() == ParticleEmitter::SizeModule::s_type)
					{
						m_universe.onComponentCreated(emitter->m_entity, PARTICLE_EMITTER_SIZE_TYPE, this);
					}
				}
			}
			if (!emitter->m_is_valid && emitter->m_modules.empty())
			{
				LUMIX_DELETE(m_allocator, emitter);
			}
			else
			{
				m_particle_emitters.insert(emitter->m_entity, emitter);
			}
		}

		serializer.read(count);
		m_scripted_particle_emitters.reserve(count);
		for (int i = 0; i < count; ++i)
		{
			ScriptedParticleEmitter* emitter = LUMIX_NEW(m_allocator, ScriptedParticleEmitter)(INVALID_ENTITY, m_allocator);
			emitter->deserialize(serializer, m_engine.getResourceManager());
			m_scripted_particle_emitters.insert(emitter->m_entity, emitter);
			m_universe.onComponentCreated(emitter->m_entity, SCRIPTED_PARTICLE_EMITTER_TYPE, this);
		}
	}


	void serializeParticleEmitters(OutputBlob& serializer)
	{
		serializer.write(m_particle_emitters.size());
		for (auto* emitter : m_particle_emitters)
		{
			serializer.write(emitter->m_is_valid);
			emitter->serialize(serializer);
		}

		serializer.write(m_scripted_particle_emitters.size());
		for (auto* emitter : m_scripted_particle_emitters)
		{
			emitter->serialize(serializer);
		}
	}


	void serialize(OutputBlob& serializer) override
	{
		serializeCameras(serializer);
		serializeModelInstances(serializer);
		serializeLights(serializer);
		serializeTerrains(serializer);
		serializeParticleEmitters(serializer);
		serializeBoneAttachments(serializer);
		serializeEnvironmentProbes(serializer);
		serializeDecals(serializer);
	}


	void deserializeCameras(InputBlob& serializer)
	{
		i32 size;
		serializer.read(size);
		m_cameras.rehash(size);
		for (int i = 0; i < size; ++i)
		{
			Camera camera;
			serializer.read(camera.entity);
			serializer.read(camera.far);
			serializer.read(camera.fov);
			serializer.read(camera.is_ortho);
			serializer.read(camera.ortho_size);
			serializer.read(camera.near);
			serializer.readString(camera.slot, lengthOf(camera.slot));

			m_cameras.insert(camera.entity, camera);
			m_universe.onComponentCreated(camera.entity, CAMERA_TYPE, this);
		}
	}

	void deserializeModelInstances(InputBlob& serializer)
	{
		i32 size = 0;
		serializer.read(size);
		m_model_instances.reserve(size);
		for (int i = 0; i < size; ++i)
		{
			auto& r = m_model_instances.emplace();
			serializer.read(r.entity);
			serializer.read(r.flags);
			r.flags.base &= ModelInstance::PERSISTENT_FLAGS;
			ASSERT(r.entity.index == i || !r.entity.isValid());
			r.model = nullptr;
			r.pose = nullptr;
			r.meshes = nullptr;
			r.mesh_count = 0;

			if(r.entity != INVALID_ENTITY)
			{
				r.matrix = m_universe.getMatrix(r.entity);

				u32 path;
				serializer.read(path);

				if (path != 0)
				{
					auto* model = static_cast<Model*>(m_engine.getResourceManager().get(Model::TYPE)->load(Path(path)));
					setModel(r.entity, model);
				}

				int material_count;
				serializer.read(material_count);
				if (material_count > 0)
				{
					allocateCustomMeshes(r, material_count);
					for (int j = 0; j < material_count; ++j)
					{
						char path[MAX_PATH_LENGTH];
						serializer.readString(path, lengthOf(path));
						setModelInstanceMaterial(r.entity, j, Path(path));
					}
				}

				m_universe.onComponentCreated(r.entity, MODEL_INSTANCE_TYPE, this);
			}
		}
	}

	void deserializeLights(InputBlob& serializer)
	{
		i32 size = 0;
		serializer.read(size);
		m_point_lights.resize(size);
		for (int i = 0; i < size; ++i)
		{
			m_light_influenced_geometry.emplace(m_allocator);
			PointLight& light = m_point_lights[i];
			serializer.read(light);
			m_point_lights_map.insert(light.m_entity, i);

			m_universe.onComponentCreated(light.m_entity, POINT_LIGHT_TYPE, this);
		}

		serializer.read(size);
		for (int i = 0; i < size; ++i)
		{
			GlobalLight light;
			serializer.read(light);
			m_global_lights.insert(light.m_entity, light);
			m_universe.onComponentCreated(light.m_entity, GLOBAL_LIGHT_TYPE, this);
		}
		serializer.read(m_active_global_light_entity);
	}

	void deserializeTerrains(InputBlob& serializer)
	{
		i32 size = 0;
		serializer.read(size);
		for (int i = 0; i < size; ++i)
		{
			auto* terrain = LUMIX_NEW(m_allocator, Terrain)(m_renderer, INVALID_ENTITY, *this, m_allocator);
			terrain->deserialize(serializer, m_universe, *this);
			m_terrains.insert(terrain->getEntity(), terrain);
		}
	}


	void deserialize(InputBlob& serializer) override
	{
		deserializeCameras(serializer);
		deserializeModelInstances(serializer);
		deserializeLights(serializer);
		deserializeTerrains(serializer);
		deserializeParticleEmitters(serializer);
		deserializeBoneAttachments(serializer);
		deserializeEnvironmentProbes(serializer);
		deserializeDecals(serializer);
	}


	void destroyBoneAttachment(Entity entity)
	{
		const BoneAttachment& bone_attachment = m_bone_attachments[entity];
		Entity parent_entity = bone_attachment.parent_entity;
		if (parent_entity.isValid() && parent_entity.index < m_model_instances.size())
		{
			ModelInstance& mi = m_model_instances[bone_attachment.parent_entity.index];
			mi.flags.unset(ModelInstance::IS_BONE_ATTACHMENT_PARENT);
		}
		m_bone_attachments.erase(entity);
		m_universe.onComponentDestroyed(entity, BONE_ATTACHMENT_TYPE, this);
	}


	void destroyEnvironmentProbe(Entity entity)
	{
		auto& probe = m_environment_probes[entity];
		if (probe.texture) probe.texture->getResourceManager().unload(*probe.texture);
		if (probe.irradiance) probe.irradiance->getResourceManager().unload(*probe.irradiance);
		if (probe.radiance) probe.radiance->getResourceManager().unload(*probe.radiance);
		m_environment_probes.erase(entity);
		m_universe.onComponentDestroyed(entity, ENVIRONMENT_PROBE_TYPE, this);
	}


	void destroyModelInstance(Entity entity)
	{
		for (int i = 0; i < m_light_influenced_geometry.size(); ++i)
		{
			Array<Entity>& influenced_geometry = m_light_influenced_geometry[i];
			for (int j = 0; j < influenced_geometry.size(); ++j)
			{
				if (influenced_geometry[j] == entity)
				{
					influenced_geometry.erase(j);
					break;
				}
			}
		}

		setModel(entity, nullptr);
		auto& model_instance = m_model_instances[entity.index];
		LUMIX_DELETE(m_allocator, model_instance.pose);
		model_instance.pose = nullptr;
		model_instance.entity = INVALID_ENTITY;
		m_universe.onComponentDestroyed(entity, MODEL_INSTANCE_TYPE, this);
	}


	void destroyGlobalLight(Entity entity)
	{
		m_universe.onComponentDestroyed(entity, GLOBAL_LIGHT_TYPE, this);

		if (entity == m_active_global_light_entity)
		{
			m_active_global_light_entity = INVALID_ENTITY;
		}
		m_global_lights.erase(entity);
	}


	void destroyDecal(Entity entity)
	{
		m_decals.erase(entity);
		m_universe.onComponentDestroyed(entity, DECAL_TYPE, this);
	}


	void destroyPointLight(Entity entity)
	{
		int index = m_point_lights_map[entity];
		m_point_lights.eraseFast(index);
		m_point_lights_map.erase(entity);
		m_light_influenced_geometry.eraseFast(index);
		if (index < m_point_lights.size())
		{
			m_point_lights_map[{m_point_lights[index].m_entity.index}] = index;
		}
		m_universe.onComponentDestroyed(entity, POINT_LIGHT_TYPE, this);
	}


	void destroyCamera(Entity entity)
	{
		m_cameras.erase(entity);
		m_universe.onComponentDestroyed(entity, CAMERA_TYPE, this);
	}


	void destroyTerrain(Entity entity)
	{
		LUMIX_DELETE(m_allocator, m_terrains[entity]);
		m_terrains.erase(entity);
		m_universe.onComponentDestroyed(entity, TERRAIN_TYPE, this);
	}


	void destroyParticleEmitter(Entity entity)
	{
		auto* emitter = m_particle_emitters[entity];
		emitter->reset();
		emitter->m_is_valid = false;
		m_universe.onComponentDestroyed(emitter->m_entity, PARTICLE_EMITTER_TYPE, this);
		cleanup(emitter);
	}


	void destroyScriptedParticleEmitter(Entity entity)
	{
		auto* emitter = m_scripted_particle_emitters[entity];
		m_universe.onComponentDestroyed(emitter->m_entity, SCRIPTED_PARTICLE_EMITTER_TYPE, this);
		m_scripted_particle_emitters.erase(emitter->m_entity);
		LUMIX_DELETE(m_allocator, emitter);
	}


	void cleanup(ParticleEmitter* emitter)
	{
		if (emitter->m_is_valid) return;
		if (!emitter->m_modules.empty()) return;

		m_particle_emitters.erase(emitter->m_entity);
		LUMIX_DELETE(m_allocator, emitter);
	}


	void destroyParticleEmitterAlpha(Entity entity)
	{
		auto* emitter = m_particle_emitters[entity];
		auto* module = emitter->getModule(PARTICLE_EMITTER_ALPHA_TYPE);
		ASSERT(module);

		LUMIX_DELETE(m_allocator, module);
		emitter->m_modules.eraseItem(module);
		m_universe.onComponentDestroyed(emitter->m_entity, PARTICLE_EMITTER_ALPHA_TYPE, this);
		cleanup(emitter);
	}


	void destroyParticleEmitterForce(Entity entity)
	{
		auto* emitter = m_particle_emitters[entity];
		auto* module = emitter->getModule(PARTICLE_EMITTER_FORCE_HASH);

		ASSERT(module);

		LUMIX_DELETE(m_allocator, module);
		emitter->m_modules.eraseItem(module);
		m_universe.onComponentDestroyed(emitter->m_entity, PARTICLE_EMITTER_FORCE_HASH, this);
		cleanup(emitter);
	}


	void destroyParticleEmitterSubimage(Entity entity)
	{
		auto* emitter = m_particle_emitters[entity];
		auto* module = emitter->getModule(PARTICLE_EMITTER_SUBIMAGE_TYPE);

		ASSERT(module);

		LUMIX_DELETE(m_allocator, module);
		emitter->m_modules.eraseItem(module);
		emitter->m_subimage_module = nullptr;
		m_universe.onComponentDestroyed(emitter->m_entity, PARTICLE_EMITTER_SUBIMAGE_TYPE, this);
		cleanup(emitter);
	}


	void destroyParticleEmitterAttractor(Entity entity)
	{
		auto* emitter = m_particle_emitters[entity];
		auto* module = emitter->getModule(PARTICLE_EMITTER_ATTRACTOR_TYPE);

		ASSERT(module);

		LUMIX_DELETE(m_allocator, module);
		emitter->m_modules.eraseItem(module);
		m_universe.onComponentDestroyed(emitter->m_entity, PARTICLE_EMITTER_ATTRACTOR_TYPE, this);
		cleanup(emitter);
	}


	void destroyParticleEmitterSize(Entity entity)
	{
		auto* emitter = m_particle_emitters[entity];
		auto* module = emitter->getModule(PARTICLE_EMITTER_SIZE_TYPE);

		ASSERT(module);

		LUMIX_DELETE(m_allocator, module);
		emitter->m_modules.eraseItem(module);
		m_universe.onComponentDestroyed(emitter->m_entity, PARTICLE_EMITTER_SIZE_TYPE, this);
		cleanup(emitter);

	}


	float getParticleEmitterPlaneBounce(Entity entity) override
	{
		auto* emitter = m_particle_emitters[entity];
		for(auto* module : emitter->m_modules)
		{
			if(module->getType() == ParticleEmitter::PlaneModule::s_type)
			{
				return static_cast<ParticleEmitter::PlaneModule*>(module)->m_bounce;
			}
		}
		return 0;
	}


	void setParticleEmitterPlaneBounce(Entity entity, float value) override
	{
		auto* emitter = m_particle_emitters[entity];
		for (auto* module : emitter->m_modules)
		{
			if (module->getType() == ParticleEmitter::PlaneModule::s_type)
			{
				static_cast<ParticleEmitter::PlaneModule*>(module)->m_bounce = value;
				break;
			}
		}
	}


	float getParticleEmitterAttractorForce(Entity entity) override
	{
		auto* emitter = m_particle_emitters[entity];
		for (auto* module : emitter->m_modules)
		{
			if (module->getType() == ParticleEmitter::AttractorModule::s_type)
			{
				return static_cast<ParticleEmitter::AttractorModule*>(module)->m_force;
			}
		}
		return 0;
	}


	void setParticleEmitterAttractorForce(Entity entity, float value) override
	{
		auto* emitter = m_particle_emitters[entity];
		for (auto* module : emitter->m_modules)
		{
			if (module->getType() == ParticleEmitter::AttractorModule::s_type)
			{
				static_cast<ParticleEmitter::AttractorModule*>(module)->m_force = value;
				break;
			}
		}
	}


	void destroyParticleEmitterPlane(Entity entity)
	{
		auto* emitter = m_particle_emitters[entity];
		auto* module = emitter->getModule(PARTICLE_EMITTER_PLANE_TYPE);

		ASSERT(module);

		LUMIX_DELETE(m_allocator, module);
		emitter->m_modules.eraseItem(module);
		m_universe.onComponentDestroyed(emitter->m_entity, PARTICLE_EMITTER_PLANE_TYPE, this);
		cleanup(emitter);
	}


	void destroyParticleEmitterLinearMovement(Entity entity)
	{
		auto* emitter = m_particle_emitters[entity];
		auto* module = emitter->getModule(PARTICLE_EMITTER_LINEAR_MOVEMENT_TYPE);

		ASSERT(module);

		LUMIX_DELETE(m_allocator, module);
		emitter->m_modules.eraseItem(module);
		m_universe.onComponentDestroyed(emitter->m_entity, PARTICLE_EMITTER_LINEAR_MOVEMENT_TYPE, this);
		cleanup(emitter);
	}


	void destroyParticleEmitterSpawnShape(Entity entity)
	{
		auto* emitter = m_particle_emitters[entity];
		auto* module = emitter->getModule(PARTICLE_EMITTER_SPAWN_SHAPE_TYPE);

		ASSERT(module);

		LUMIX_DELETE(m_allocator, module);
		emitter->m_modules.eraseItem(module);
		m_universe.onComponentDestroyed(emitter->m_entity, PARTICLE_EMITTER_SPAWN_SHAPE_TYPE, this);
		cleanup(emitter);

	}


	void destroyParticleEmitterRandomRotation(Entity entity)
	{
		auto* emitter = m_particle_emitters[entity];
		auto* module = emitter->getModule(PARTICLE_EMITTER_RANDOM_ROTATION_TYPE);

		ASSERT(module);

		LUMIX_DELETE(m_allocator, module);
		emitter->m_modules.eraseItem(module);
		m_universe.onComponentDestroyed(emitter->m_entity, PARTICLE_EMITTER_RANDOM_ROTATION_TYPE, this);
		cleanup(emitter);

	}


	void setParticleEmitterAlpha(Entity entity, const Vec2* values, int count) override
	{
		ASSERT(count > 0);
		ASSERT(values[1].x < 0.001f);
		ASSERT(values[count - 2].x > 0.999f);

		auto* alpha_module = getEmitterModule<ParticleEmitter::AlphaModule>(entity);
		if (!alpha_module) return;

		alpha_module->m_values.resize(count);
		for (int i = 0; i < count; ++i)
		{
			alpha_module->m_values[i] = values[i];
		}
		alpha_module->sample();
	}


	void setParticleEmitterSubimageRows(Entity entity, const int& value) override
	{
		auto* module = getEmitterModule<ParticleEmitter::SubimageModule>(entity);
		if (module) module->rows = value;
	}


	void setParticleEmitterSubimageCols(Entity entity, const int& value) override
	{
		auto* module = getEmitterModule<ParticleEmitter::SubimageModule>(entity);
		if (module) module->cols = value;
	}


	int getParticleEmitterSubimageRows(Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::SubimageModule>(entity);
		return module ? module->rows : 1;
	}


	int getParticleEmitterSubimageCols(Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::SubimageModule>(entity);
		return module ? module->cols : 1;
	}


	void setParticleEmitterAcceleration(Entity entity, const Vec3& value) override
	{
		auto* module = getEmitterModule<ParticleEmitter::ForceModule>(entity);
		if (module) module->m_acceleration = value;
	}


	void setParticleEmitterAutoemit(Entity entity, bool autoemit) override
	{
		m_particle_emitters[entity]->m_autoemit = autoemit;
	}


	bool getParticleEmitterAutoemit(Entity entity) override
	{
		return m_particle_emitters[entity]->m_autoemit;
	}


	void setParticleEmitterLocalSpace(Entity entity, bool local_space) override
	{
		m_particle_emitters[entity]->m_local_space = local_space;
	}


	bool getParticleEmitterLocalSpace(Entity entity) override
	{
		return m_particle_emitters[entity]->m_local_space;
	}


	Vec3 getParticleEmitterAcceleration(Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::ForceModule>(entity);
		return module ? module->m_acceleration : Vec3();
	}


	int getParticleEmitterSizeCount(Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::SizeModule>(entity);
		return module ? module->m_values.size() : 0; 
	}


	const Vec2* getParticleEmitterSize(Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::SizeModule>(entity);
		return module ? &module->m_values[0] : nullptr;
	}


	void setParticleEmitterSize(Entity entity, const Vec2* values, int count) override
	{
		ASSERT(count > 0);
		ASSERT(values[0].x < 0.001f);
		ASSERT(values[count-1].x > 0.999f);

		auto* module = getEmitterModule<ParticleEmitter::SizeModule>(entity);
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
	T* getEmitterModule(Entity entity) const
	{
		auto& modules = m_particle_emitters[entity]->m_modules;
		for (auto* module : modules)
		{
			if (module->getType() == T::s_type)
			{
				return static_cast<T*>(module);
			}
		}
		return nullptr;
	}


	int getParticleEmitterAlphaCount(Entity entity) override 
	{
		auto* module = getEmitterModule<ParticleEmitter::AlphaModule>(entity);
		return module ? module->m_values.size() : 0;
	}


	const Vec2* getParticleEmitterAlpha(Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::AlphaModule>(entity);
		return module ? &module->m_values[0] : 0;
	}


	Vec2 getParticleEmitterLinearMovementX(Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::LinearMovementModule>(entity);
		return module ? Vec2(module->m_x.from, module->m_x.to) : Vec2(0, 0);
	}


	void setParticleEmitterLinearMovementX(Entity entity, const Vec2& value) override
	{
		auto* module = getEmitterModule<ParticleEmitter::LinearMovementModule>(entity);
		if (module)
		{
			module->m_x = value;
			module->m_x.check();
		}
	}


	Vec2 getParticleEmitterLinearMovementY(Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::LinearMovementModule>(entity);
		return module ? Vec2(module->m_y.from, module->m_y.to) : Vec2(0, 0);
	}


	void setParticleEmitterLinearMovementY(Entity entity, const Vec2& value) override
	{
		auto* module = getEmitterModule<ParticleEmitter::LinearMovementModule>(entity);
		if (module)
		{
			module->m_y = value;
			module->m_y.check();
		}
	}


	Vec2 getParticleEmitterLinearMovementZ(Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::LinearMovementModule>(entity);
		return module ? Vec2(module->m_z.from, module->m_z.to) : Vec2(0, 0);
	}


	void setParticleEmitterLinearMovementZ(Entity entity, const Vec2& value) override
	{
		auto* module = getEmitterModule<ParticleEmitter::LinearMovementModule>(entity);
		if (module)
		{
			module->m_z = value;
			module->m_z.check();
		}
	}


	Vec2 getParticleEmitterInitialLife(Entity entity) override
	{
		return m_particle_emitters[entity]->m_initial_life;
	}


	Vec2 getParticleEmitterSpawnPeriod(Entity entity) override
	{
		return m_particle_emitters[entity]->m_spawn_period;
	}


	void setParticleEmitterInitialLife(Entity entity, const Vec2& value) override
	{
		m_particle_emitters[entity]->m_initial_life = value;
		m_particle_emitters[entity]->m_initial_life.checkZero();
	}


	void setParticleEmitterInitialSize(Entity entity, const Vec2& value) override
	{
		m_particle_emitters[entity]->m_initial_size = value;
		m_particle_emitters[entity]->m_initial_size.checkZero();
	}


	Vec2 getParticleEmitterInitialSize(Entity entity) override
	{
		return m_particle_emitters[entity]->m_initial_size;
	}


	void setParticleEmitterSpawnPeriod(Entity entity, const Vec2& value) override
	{
		auto* emitter = m_particle_emitters[entity];
		emitter->m_spawn_period = value;
		emitter->m_spawn_period.from = Math::maximum(0.01f, emitter->m_spawn_period.from);
		emitter->m_spawn_period.checkZero();
	}


	void createCamera(Entity entity)
	{
		Camera camera;
		camera.is_ortho = false;
		camera.ortho_size = 10;
		camera.entity = entity;
		camera.fov = Math::degreesToRadians(60);
		camera.screen_width = 800;
		camera.screen_height = 600;
		camera.aspect = 800.0f / 600.0f;
		camera.near = 0.1f;
		camera.far = 10000.0f;
		camera.slot[0] = '\0';
		if (!getCameraInSlot("main").isValid()) copyString(camera.slot, "main");
		m_cameras.insert(entity, camera);
		m_universe.onComponentCreated(entity, CAMERA_TYPE, this);
	}


	void createTerrain(Entity entity)
	{
		Terrain* terrain = LUMIX_NEW(m_allocator, Terrain)(m_renderer, entity, *this, m_allocator);
		m_terrains.insert(entity, terrain);
		m_universe.onComponentCreated(entity, TERRAIN_TYPE, this);
	}


	void createParticleEmitterRandomRotation(Entity entity)
	{
		int index = allocateParticleEmitter(entity);
		auto* emitter = m_particle_emitters.at(index);
		auto module = LUMIX_NEW(m_allocator, ParticleEmitter::RandomRotationModule)(*emitter);
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_RANDOM_ROTATION_TYPE, this);
	}


	void createParticleEmitterPlane(Entity entity)
	{
		int index = allocateParticleEmitter(entity);
		auto* emitter = m_particle_emitters.at(index);
		auto module = LUMIX_NEW(m_allocator, ParticleEmitter::PlaneModule)(*emitter);
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_PLANE_TYPE, this);
	}


	void createParticleEmitterLinearMovement(Entity entity)
	{
		int index = allocateParticleEmitter(entity);
		auto* emitter = m_particle_emitters.at(index);
		auto module = LUMIX_NEW(m_allocator, ParticleEmitter::LinearMovementModule)(*emitter);
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_LINEAR_MOVEMENT_TYPE, this);
	}


	void createParticleEmitterSpawnShape(Entity entity)
	{
		int index = allocateParticleEmitter(entity);
		auto* emitter = m_particle_emitters.at(index);
		auto module = LUMIX_NEW(m_allocator, ParticleEmitter::SpawnShapeModule)(*emitter);
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_SPAWN_SHAPE_TYPE, this);
	}


	void createParticleEmitterAlpha(Entity entity)
	{
		int index = allocateParticleEmitter(entity);
		auto* emitter = m_particle_emitters.at(index);
		auto module = LUMIX_NEW(m_allocator, ParticleEmitter::AlphaModule)(*emitter);
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_ALPHA_TYPE, this);
	}


	void createParticleEmitterForce(Entity entity)
	{
		int index = allocateParticleEmitter(entity);
		auto* emitter = m_particle_emitters.at(index);
		auto module = LUMIX_NEW(m_allocator, ParticleEmitter::ForceModule)(*emitter);
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_FORCE_HASH, this);
	}


	void createParticleEmitterSubimage(Entity entity)
	{
		int index = allocateParticleEmitter(entity);
		auto* emitter = m_particle_emitters.at(index);
		auto module = LUMIX_NEW(m_allocator, ParticleEmitter::SubimageModule)(*emitter);
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_SUBIMAGE_TYPE, this);
	}


	void createParticleEmitterAttractor(Entity entity)
	{
		int index = allocateParticleEmitter(entity);
		auto* emitter = m_particle_emitters.at(index);
		auto module = LUMIX_NEW(m_allocator, ParticleEmitter::AttractorModule)(*emitter);
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_ATTRACTOR_TYPE, this);
	}


	void createParticleEmitterSize(Entity entity)
	{
		int index = allocateParticleEmitter(entity);
		auto* emitter = m_particle_emitters.at(index);
		auto module = LUMIX_NEW(m_allocator, ParticleEmitter::SizeModule)(*emitter);
		emitter->addModule(module);
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_SIZE_TYPE, this);
	}


	void createScriptedParticleEmitter(Entity entity)
	{
		m_scripted_particle_emitters.insert(entity, LUMIX_NEW(m_allocator, ScriptedParticleEmitter)(entity, m_allocator));
		m_universe.onComponentCreated(entity, SCRIPTED_PARTICLE_EMITTER_TYPE, this);
	}


	void createParticleEmitter(Entity entity)
	{
		int index = allocateParticleEmitter(entity);
		m_particle_emitters.at(index)->init();

		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_TYPE, this);
	}


	int allocateParticleEmitter(Entity entity)
	{
		int index = m_particle_emitters.find(entity);
		if (index >= 0) return index;
		return m_particle_emitters.insert(entity, LUMIX_NEW(m_allocator, ParticleEmitter)(entity, m_universe, m_allocator));
	}


	ModelInstance* getModelInstances() override
	{
		return &m_model_instances[0];
	}


	ModelInstance* getModelInstance(Entity entity) override
	{
		return &m_model_instances[entity.index];
	}


	Vec3 getPoseBonePosition(Entity model_instance, int bone_index)
	{
		Pose* pose = m_model_instances[model_instance.index].pose;
		return pose->positions[bone_index];
	}


	Frustum getPointLightFrustum(int light_idx) const
	{
		const PointLight& light = m_point_lights[light_idx];
		Frustum frustum;
		frustum.computeOrtho(m_universe.getPosition(light.m_entity),
			Vec3(1, 0, 0),
			Vec3(0, 1, 0),
			light.m_range,
			light.m_range,
			-light.m_range,
			light.m_range);

		return frustum;
	}


	void onEntityDestroyed(Entity entity)
	{
		for (auto& i : m_bone_attachments)
		{
			if (i.parent_entity == entity)
			{
				i.parent_entity = INVALID_ENTITY;
				break;
			}
		}
	}


	void onEntityMoved(Entity entity)
	{
		int index = entity.index;

		if (index < m_model_instances.size() && m_model_instances[index].entity.isValid() &&
			m_model_instances[index].model && m_model_instances[index].model->isReady())
		{
			ModelInstance& r = m_model_instances[index];
			r.matrix = m_universe.getMatrix(entity);
			if (r.model && r.model->isReady())
			{
				float radius = m_universe.getScale(entity) * r.model->getBoundingRadius();
				Vec3 position = m_universe.getPosition(entity);
				m_culling_system->updateBoundingSphere({position, radius}, entity);
			}

			float bounding_radius = r.model ? r.model->getBoundingRadius() : 1;
			for (int light_idx = 0, c = m_point_lights.size(); light_idx < c; ++light_idx)
			{
				for (int j = 0, c2 = m_light_influenced_geometry[light_idx].size(); j < c2; ++j)
				{
					if(m_light_influenced_geometry[light_idx][j] == entity)
					{
						m_light_influenced_geometry[light_idx].eraseFast(j);
						break;
					}
				}

				Vec3 pos = m_universe.getPosition(r.entity);
				Frustum frustum = getPointLightFrustum(light_idx);
				if(frustum.isSphereInside(pos, bounding_radius))
				{
					m_light_influenced_geometry[light_idx].push(entity);
				}
			}
		}

		int decal_idx = m_decals.find(entity);
		if (decal_idx >= 0)
		{
			updateDecalInfo(m_decals.at(decal_idx));
		}

		for (int i = 0, c = m_point_lights.size(); i < c; ++i)
		{
			if (m_point_lights[i].m_entity == entity)
			{
				detectLightInfluencedGeometry({ m_point_lights[i].m_entity.index });
				break;
			}
		}

		bool was_updating = m_is_updating_attachments;
		m_is_updating_attachments = true;
		for (auto& attachment : m_bone_attachments)
		{
			if (attachment.parent_entity == entity)
			{
				updateBoneAttachment(attachment);
			}
		}
		m_is_updating_attachments = was_updating;

		if (m_is_updating_attachments || m_is_game_running) return;
		for (auto& attachment : m_bone_attachments)
		{
			if (attachment.entity == entity)
			{
				updateRelativeMatrix(attachment);
				break;
			}
		}

	}

	Engine& getEngine() const override { return m_engine; }


	Entity getTerrainEntity(Entity entity) override
	{
		return entity;
	}


	Vec2 getTerrainResolution(Entity entity) override
	{
		auto* terrain = m_terrains[entity];
		return Vec2((float)terrain->getWidth(), (float)terrain->getHeight());
	}


	Entity getFirstTerrain() override
	{
		if (m_terrains.empty()) return INVALID_ENTITY;
		auto iter = m_terrains.begin();
		return iter.value()->getEntity();
	}


	Entity getNextTerrain(Entity entity) override
	{
		auto iter = m_terrains.find(entity);
		++iter;
		if (!iter.isValid()) return INVALID_ENTITY;
		return iter.value()->getEntity();
	}


	Vec3 getTerrainNormalAt(Entity entity, float x, float z) override
	{
		return m_terrains[entity]->getNormal(x, z);
	}


	float getTerrainHeightAt(Entity entity, float x, float z) override
	{
		return m_terrains[entity]->getHeight(x, z);
	}


	AABB getTerrainAABB(Entity entity) override
	{
		return m_terrains[entity]->getAABB();
	}


	Vec2 getTerrainSize(Entity entity) override
	{
		return m_terrains[entity]->getSize();
	}


	void setTerrainMaterialPath(Entity entity, const Path& path) override
	{
		if (path.isValid())
		{
			Material* material = static_cast<Material*>(m_engine.getResourceManager().get(Material::TYPE)->load(path));
			m_terrains[entity]->setMaterial(material);
		}
		else
		{
			m_terrains[entity]->setMaterial(nullptr);
		}
	}


	Material* getTerrainMaterial(Entity entity) override { return m_terrains[entity]->getMaterial(); }


	void setDecalScale(Entity entity, const Vec3& value) override
	{
		Decal& decal = m_decals[entity];
		decal.scale = value;
		updateDecalInfo(decal);
	}


	Vec3 getDecalScale(Entity entity) override
	{
		return m_decals[entity].scale;
	}


	void getDecals(const Frustum& frustum, Array<DecalInfo>& decals) override
	{
		decals.reserve(m_decals.size());
		for (const Decal& decal : m_decals)
		{
			if (!decal.material || !decal.material->isReady()) continue;
			if (frustum.isSphereInside(decal.position, decal.radius)) decals.push(decal);
		}
	}


	void setDecalMaterialPath(Entity entity, const Path& path) override
	{
		ResourceManagerBase* material_manager = m_engine.getResourceManager().get(Material::TYPE);
		Decal& decal = m_decals[entity];
		if (decal.material)
		{
			material_manager->unload(*decal.material);
		}
		if (path.isValid())
		{
			decal.material = static_cast<Material*>(material_manager->load(path));
		}
		else
		{
			decal.material = nullptr;
		}
	}


	Path getDecalMaterialPath(Entity entity) override
	{
		Decal& decal = m_decals[entity];
		return decal.material ? decal.material->getPath() : Path("");
	}


	Path getTerrainMaterialPath(Entity entity) override
	{
		Terrain* terrain = m_terrains[entity];
		if (terrain->getMaterial())
		{
			return terrain->getMaterial()->getPath();
		}
		else
		{
			return Path("");
		}
	}


	void setTerrainXZScale(Entity entity, float scale) override
	{
		m_terrains[entity]->setXZScale(scale);
	}

	float getTerrainXZScale(Entity entity) override { return m_terrains[entity]->getXZScale(); }


	void setTerrainYScale(Entity entity, float scale) override
	{
		m_terrains[entity]->setYScale(scale);
	}

	float getTerrainYScale(Entity entity) override { return m_terrains[entity]->getYScale(); }


	Pose* lockPose(Entity entity) override { return m_model_instances[entity.index].pose; }
	void unlockPose(Entity entity, bool changed) override
	{
		if (!changed) return;
		if (entity.index < m_model_instances.size()
			&& (m_model_instances[entity.index].flags.isSet(ModelInstance::IS_BONE_ATTACHMENT_PARENT)) == 0)
		{
			return;
		}

		Entity parent = entity;
		for (BoneAttachment& ba : m_bone_attachments)
		{
			if (ba.parent_entity != parent) continue;
			m_is_updating_attachments = true;
			updateBoneAttachment(ba);
			m_is_updating_attachments = false;
		}
	}


	Model* getModelInstanceModel(Entity entity) override { return m_model_instances[entity.index].model; }


	static u64 getLayerMask(ModelInstance& model_instance)
	{
		Model* model = model_instance.model;
		if (!model->isReady()) return 1;
		u64 layer_mask = 0;
		for(int i = 0; i < model->getMeshCount(); ++i)
		{ 
			layer_mask |= model->getMesh(i).material->getRenderLayerMask();
		}
		return layer_mask;
	}


	bool isModelInstanceEnabled(Entity entity) override
	{
		ModelInstance& model_instance = m_model_instances[entity.index];
		return model_instance.flags.isSet(ModelInstance::ENABLED);
	}


	void enableModelInstance(Entity entity, bool enable) override
	{
		ModelInstance& model_instance = m_model_instances[entity.index];
		model_instance.flags.set(ModelInstance::ENABLED, enable);
		if (enable)
		{
			if (!model_instance.model || !model_instance.model->isReady()) return;

			Sphere sphere(m_universe.getPosition(model_instance.entity), model_instance.model->getBoundingRadius());
			u64 layer_mask = getLayerMask(model_instance);
			if (!m_culling_system->isAdded(entity)) m_culling_system->addStatic(entity, sphere, layer_mask);
		}
		else
		{
			m_culling_system->removeStatic(entity);
		}
	}


	Path getModelInstancePath(Entity entity) override
	{
		return m_model_instances[entity.index].model ? m_model_instances[entity.index].model->getPath() : Path("");
	}


	int getModelInstanceMaterialsCount(Entity entity) override
	{
		return m_model_instances[entity.index].model ? m_model_instances[entity.index].mesh_count : 0;
	}


	void setModelInstancePath(Entity entity, const Path& path) override
	{
		ModelInstance& r = m_model_instances[entity.index];

		auto* manager = m_engine.getResourceManager().get(Model::TYPE);
		if (path.isValid())
		{
			Model* model = static_cast<Model*>(manager->load(path));
			setModel(entity, model);
		}
		else
		{
			setModel(entity, nullptr);
		}
		r.matrix = m_universe.getMatrix(r.entity);
	}


	void forceGrassUpdate(Entity entity) override { m_terrains[entity]->forceGrassUpdate(); }


	void getTerrainInfos(const Frustum& frustum, const Vec3& lod_ref_point, Array<TerrainInfo>& infos) override
	{
		PROFILE_FUNCTION();
		infos.reserve(m_terrains.size());
		for (auto* terrain : m_terrains)
		{
			terrain->getInfos(infos, frustum, lod_ref_point);
		}
	}


	void getGrassInfos(const Frustum& frustum, Entity camera, Array<GrassInfo>& infos) override
	{
		PROFILE_FUNCTION();

		if (!m_is_grass_enabled) return;

		for (auto* terrain : m_terrains)
		{
			terrain->getGrassInfos(frustum, infos, camera);
		}
	}


	static int LUA_castCameraRay(lua_State* L)
	{
		auto* scene = LuaWrapper::checkArg<RenderSceneImpl*>(L, 1);
		const char* slot = LuaWrapper::checkArg<const char*>(L, 2);
		float x, y;
		Entity camera_entity = scene->getCameraInSlot(slot);
		if (!camera_entity.isValid()) return 0;
		if (lua_gettop(L) > 3)
		{
			x = LuaWrapper::checkArg<float>(L, 3);
			y = LuaWrapper::checkArg<float>(L, 4);
		}
		else
		{
			x = scene->getCameraScreenWidth(camera_entity) * 0.5f;
			y = scene->getCameraScreenHeight(camera_entity) * 0.5f;
		}

		Vec3 origin, dir;
		scene->getRay(camera_entity, {x, y}, origin, dir);

		RayCastModelHit hit = scene->castRay(origin, dir, INVALID_ENTITY);
		LuaWrapper::push(L, hit.m_is_hit);
		LuaWrapper::push(L, hit.m_is_hit ? hit.m_origin + hit.m_dir * hit.m_t : Vec3(0, 0, 0));

		return 2;
	}


	static bgfx::TextureHandle* LUA_getTextureHandle(RenderScene* scene, int resource_idx)
	{
		Resource* res = scene->getEngine().getLuaResource(resource_idx);
		if (!res) return nullptr;
		return &static_cast<Texture*>(res)->handle;
	}

	
	static void LUA_setTexturePixel(Texture* texture, int x, int y, u32 value)
	{
		if (!texture) return;
		if (!texture->isReady()) return;
		if (texture->data.empty()) return;
		if (texture->bytes_per_pixel != 4) return;

		x = Math::clamp(x, 0, texture->width - 1);
		y = Math::clamp(y, 0, texture->height - 1);

		((u32*)&texture->data[0])[x + y * texture->width] = value;
	}


	static void LUA_updateTextureData(Texture* texture, int x, int y, int w, int h)
	{
		if (!texture) return;
		if (!texture->isReady()) return;
		if (texture->data.empty()) return;

		texture->onDataUpdated(x, y, w, h);
	}

	
	static int LUA_getTextureWidth(Texture* texture)
	{
		if (!texture) return 0;
		if (!texture->isReady()) return 0;

		return texture->width;
	}


	static int LUA_getTextureHeight(Texture* texture)
	{
		if (!texture) return 0;
		if (!texture->isReady()) return 0;

		return texture->height;
	}


	static float LUA_getTerrainHeightAt(RenderSceneImpl* render_scene, Entity entity, int x, int z)
	{
		return render_scene->m_terrains[entity]->getHeight(x, z);
	}


	static void LUA_emitParticle(RenderSceneImpl* render_scene, Entity emitter)
	{
		int idx = render_scene->m_particle_emitters.find(emitter);
		if (idx < 0) return;
		return render_scene->m_particle_emitters.at(idx)->emit();
	}


	void setTerrainHeightAt(Entity entity, int x, int z, float height)
	{
		m_terrains[entity]->setHeight(x, z, height);
	}

	static u32 LUA_getTexturePixel(Texture* texture, int x, int y)
	{
		if (!texture) return 0;
		if (!texture->isReady()) return 0;
		if (texture->data.empty()) return 0;
		if (texture->bytes_per_pixel != 4) return 0;
		
		x = Math::clamp(x, 0, texture->width - 1);
		y = Math::clamp(y, 0, texture->height - 1);

		return ((u32*)&texture->data[0])[x + y * texture->width];
	}


	static Pipeline* LUA_createPipeline(Engine* engine, const char* path)
	{
		Renderer& renderer = *static_cast<Renderer*>(engine->getPluginManager().getPlugin("renderer"));
		Pipeline* pipeline = Pipeline::create(renderer, Path(path), "", renderer.getEngine().getAllocator());
		pipeline->load();
		return pipeline;
	}


	static void LUA_destroyPipeline(Pipeline* pipeline)
	{
		Pipeline::destroy(pipeline);
	}


	static void LUA_setPipelineScene(Pipeline* pipeline, RenderScene* scene)
	{
		pipeline->setScene(scene);
	}


	static RenderScene* LUA_getPipelineScene(Pipeline* pipeline)
	{
		return pipeline->getScene();
	}


	static void LUA_pipelineRender(Pipeline* pipeline, int w, int h)
	{
		pipeline->resize(w, h);
		pipeline->render();
	}


	static bgfx::TextureHandle* LUA_getRenderBuffer(Pipeline* pipeline,
		const char* framebuffer_name,
		int renderbuffer_idx)
	{
		bgfx::TextureHandle& handle = pipeline->getRenderbuffer(framebuffer_name, renderbuffer_idx);
		return &handle;
	}


	static Texture* LUA_getMaterialTexture(Material* material, int texture_index)
	{
		if (!material) return nullptr;
		return material->getTexture(texture_index);
	}


	static void LUA_setModelInstancePath(IScene* scene, int component, const char* path)
	{
		RenderScene* render_scene = (RenderScene*)scene;
		render_scene->setModelInstancePath({component}, Path(path));
	}


	static int LUA_getModelBoneIndex(Model* model, const char* bone)
	{
		if (!model) return 0;
		return model->getBoneIndex(crc32(bone)).value();
	}


	static unsigned int LUA_compareTGA(RenderSceneImpl* scene, const char* path, const char* path_preimage, int min_diff)
	{
		auto& fs = scene->m_engine.getFileSystem();
		auto file1 = fs.open(fs.getDefaultDevice(), Path(path), FS::Mode::OPEN_AND_READ);
		auto file2 = fs.open(fs.getDefaultDevice(), Path(path_preimage), FS::Mode::OPEN_AND_READ);
		if (!file1)
		{
			if (file2) fs.close(*file2);
			g_log_error.log("render_test") << "Failed to open " << path;
			return 0xffffFFFF;
		}
		else if (!file2)
		{
			fs.close(*file1);
			g_log_error.log("render_test") << "Failed to open " << path_preimage;
			return 0xffffFFFF;
		}
		unsigned int result = Texture::compareTGA(file1, file2, min_diff, scene->m_allocator);
		fs.close(*file1);
		fs.close(*file2);
		return result;
	}


	static void LUA_makeScreenshot(RenderSceneImpl* scene, const char* path)
	{
		scene->m_renderer.makeScreenshot(Path(path));
	}


	static void LUA_setModelInstanceMaterial(RenderScene* scene,
		Entity entity,
		int index,
		const char* path)
	{
		scene->setModelInstanceMaterial(entity, index, Path(path));
	}


	bool isGrassEnabled() const override
	{
		return m_is_grass_enabled;
	}


	int getGrassRotationMode(Entity entity, int index) override
	{
		return (int)m_terrains[entity]->getGrassTypeRotationMode(index);
	}


	void setGrassRotationMode(Entity entity, int index, int value) override
	{
		m_terrains[entity]->setGrassTypeRotationMode(index, (Terrain::GrassType::RotationMode)value);
	}


	float getGrassDistance(Entity entity, int index) override
	{
		return m_terrains[entity]->getGrassTypeDistance(index);
	}


	void setGrassDistance(Entity entity, int index, float value) override
	{
		m_terrains[entity]->setGrassTypeDistance(index, value);
	}


	void enableGrass(bool enabled) override { m_is_grass_enabled = enabled; }


	void setGrassDensity(Entity entity, int index, int density) override
	{
		m_terrains[entity]->setGrassTypeDensity(index, density);
	}


	int getGrassDensity(Entity entity, int index) override
	{
		return m_terrains[entity]->getGrassTypeDensity(index);
	}


	void setGrassPath(Entity entity, int index, const Path& path) override
	{
		m_terrains[entity]->setGrassTypePath(index, path);
	}


	Path getGrassPath(Entity entity, int index) override
	{
		return m_terrains[entity]->getGrassTypePath(index);
	}


	int getGrassCount(Entity entity) override
	{
		return m_terrains[entity]->getGrassTypeCount();
	}


	void addGrass(Entity entity, int index) override
	{
		m_terrains[entity]->addGrassType(index);
	}


	void removeGrass(Entity entity, int index) override
	{
		m_terrains[entity]->removeGrassType(index);
	}


	Entity getFirstModelInstance() override
	{
		return getNextModelInstance(INVALID_ENTITY);
	}


	Entity getNextModelInstance(Entity entity) override
	{
		for(int i = entity.index + 1; i < m_model_instances.size(); ++i)
		{
			if (m_model_instances[i].entity != INVALID_ENTITY) return {i};
		}
		return INVALID_ENTITY;
	}

	
	int getClosestPointLights(const Vec3& reference_pos,
		Entity* lights,
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
			lights[light_count] = { light.m_entity.index };

			for (int i = light_count; i > 0 && dists[i - 1] > dists[i]; --i)
			{
				float tmp = dists[i];
				dists[i] = dists[i - 1];
				dists[i - 1] = tmp;

				Entity tmp2 = lights[i];
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
				lights[max_lights - 1] = { light.m_entity.index };

				for (int i = max_lights - 1; i > 0 && dists[i - 1] > dists[i];
					 --i)
				{
					float tmp = dists[i];
					dists[i] = dists[i - 1];
					dists[i - 1] = tmp;

					Entity tmp2 = lights[i];
					lights[i] = lights[i - 1];
					lights[i - 1] = tmp2;
				}
			}
		}

		return light_count;
	}


	void getPointLights(const Frustum& frustum, Array<Entity>& lights) override
	{
		for (int i = 0, ci = m_point_lights.size(); i < ci; ++i)
		{
			PointLight& light = m_point_lights[i];

			if (frustum.isSphereInside(m_universe.getPosition(light.m_entity), light.m_range))
			{
				lights.push(light.m_entity);
			}
		}
	}


	void setLightCastShadows(Entity entity, bool cast_shadows) override
	{
		m_point_lights[m_point_lights_map[entity]].m_cast_shadows = cast_shadows;
	}


	bool getLightCastShadows(Entity entity) override
	{
		return m_point_lights[m_point_lights_map[entity]].m_cast_shadows;
	}


	void getPointLightInfluencedGeometry(Entity light,
		const Frustum& frustum,
		Array<ModelInstanceMesh>& infos) override
	{
		PROFILE_FUNCTION();

		int light_index = m_point_lights_map[light];
		for (int j = 0, cj = m_light_influenced_geometry[light_index].size(); j < cj; ++j)
		{
			Entity model_instance_entity = m_light_influenced_geometry[light_index][j];
			ModelInstance& model_instance = m_model_instances[model_instance_entity.index];
			const Sphere& sphere = m_culling_system->getSphere(model_instance_entity);
			if (frustum.isSphereInside(sphere.position, sphere.radius))
			{
				for (int k = 0, kc = model_instance.model->getMeshCount(); k < kc; ++k)
				{
					auto& info = infos.emplace();
					info.mesh = &model_instance.model->getMesh(k);
					info.model_instance = model_instance_entity;
				}
			}
		}
	}


	void getPointLightInfluencedGeometry(Entity light, Array<ModelInstanceMesh>& infos) override
	{
		PROFILE_FUNCTION();

		int light_index = m_point_lights_map[light];
		auto& geoms = m_light_influenced_geometry[light_index];
		for (int j = 0, cj = geoms.size(); j < cj; ++j)
		{
			const ModelInstance& model_instance = m_model_instances[geoms[j].index];
			for (int k = 0, kc = model_instance.model->getMeshCount(); k < kc; ++k)
			{
				auto& info = infos.emplace();
				info.mesh = &model_instance.model->getMesh(k);
				info.model_instance = geoms[j];
			}
		}
	}


	void getModelInstanceEntities(const Frustum& frustum, Array<Entity>& entities) override
	{
		PROFILE_FUNCTION();

		auto& results = m_culling_system->cull(frustum, ~0ULL);

		for (auto& subresults : results)
		{
			for (Entity model_instance : subresults)
			{
				entities.push(model_instance);
			}
		}
	}


	float getCameraLODMultiplier(Entity camera)
	{
		float lod_multiplier;
		if (isCameraOrtho(camera))
		{
			lod_multiplier = 1;
		}
		else
		{
			lod_multiplier = getCameraFOV(camera) / Math::degreesToRadians(60);
			lod_multiplier *= lod_multiplier;
		}
		return lod_multiplier;
	}


	Array<Array<ModelInstanceMesh>>& getModelInstanceInfos(const Frustum& frustum,
		const Vec3& lod_ref_point,
		Entity camera,
		u64 layer_mask) override
	{
		for (auto& i : m_temporary_infos) i.clear();
		const CullingSystem::Results& results = m_culling_system->cull(frustum, layer_mask);

		while (m_temporary_infos.size() < results.size())
		{
			m_temporary_infos.emplace(m_allocator);
		}
		while (m_temporary_infos.size() > results.size())
		{
			m_temporary_infos.pop();
		}

		JobSystem::JobDecl jobs[64];
		JobSystem::LambdaJob job_storage[64];
		ASSERT(results.size() <= lengthOf(jobs));

		volatile int counter = 0;
		for (int subresult_index = 0; subresult_index < results.size(); ++subresult_index)
		{
			Array<ModelInstanceMesh>& subinfos = m_temporary_infos[subresult_index];
			subinfos.clear();

			JobSystem::fromLambda([&layer_mask, &subinfos, this, &results, subresult_index, lod_ref_point, camera]() {
				PROFILE_BLOCK("Temporary Info Job");
				PROFILE_INT("ModelInstance count", results[subresult_index].size());
				if (results[subresult_index].empty()) return;

				float lod_multiplier = getCameraLODMultiplier(camera);
				Vec3 ref_point = lod_ref_point;
				float final_lod_multiplier = m_lod_multiplier * lod_multiplier;
				const Entity* LUMIX_RESTRICT raw_subresults = &results[subresult_index][0];
				ModelInstance* LUMIX_RESTRICT model_instances = &m_model_instances[0];
				for (int i = 0, c = results[subresult_index].size(); i < c; ++i)
				{
					const ModelInstance* LUMIX_RESTRICT model_instance = &model_instances[raw_subresults[i].index];
					float squared_distance = (model_instance->matrix.getTranslation() - ref_point).squaredLength();
					squared_distance *= final_lod_multiplier;

					const Model* LUMIX_RESTRICT model = model_instance->model;
					LODMeshIndices lod = model->getLODMeshIndices(squared_distance);
					for (int j = lod.from, c = lod.to; j <= c; ++j)
					{
						Mesh& mesh = model_instance->meshes[j];
						if ((mesh.layer_mask & layer_mask) == 0) continue;
						
						ModelInstanceMesh& info = subinfos.emplace();
						info.model_instance = raw_subresults[i];
						info.mesh = &mesh;
						info.depth = squared_distance;
					}
				}
			}, &job_storage[subresult_index], &jobs[subresult_index], nullptr);
		}
		JobSystem::runJobs(jobs, results.size(), &counter);
		JobSystem::wait(&counter);

		return m_temporary_infos;
	}


	void setCameraSlot(Entity entity, const char* slot) override
	{
		auto& camera = m_cameras[entity];
		copyString(camera.slot, lengthOf(camera.slot), slot);
	}


	const char* getCameraSlot(Entity camera) override { return m_cameras[camera].slot; }
	float getCameraFOV(Entity camera) override { return m_cameras[camera].fov; }
	void setCameraFOV(Entity camera, float fov) override { m_cameras[camera].fov = fov; }
	void setCameraNearPlane(Entity camera, float near_plane) override { m_cameras[camera].near = Math::maximum(near_plane, 0.00001f); }
	float getCameraNearPlane(Entity camera) override { return m_cameras[camera].near; }
	void setCameraFarPlane(Entity camera, float far_plane) override { m_cameras[camera].far = far_plane; }
	float getCameraFarPlane(Entity camera) override { return m_cameras[camera].far; }
	float getCameraScreenWidth(Entity camera) override { return m_cameras[camera].screen_width; }
	float getCameraScreenHeight(Entity camera) override { return m_cameras[camera].screen_height; }


	void setGlobalLODMultiplier(float multiplier) { m_lod_multiplier = multiplier; }
	float getGlobalLODMultiplier() const { return m_lod_multiplier; }


	Matrix getCameraViewProjection(Entity entity) override
	{
		Matrix view = m_universe.getMatrix(entity);
		view.fastInverse();
		return getCameraProjection(entity) * view;
	}


	Matrix getCameraProjection(Entity entity) override
	{
		Camera& camera = m_cameras[entity];
		Matrix mtx;
		float ratio = camera.screen_height > 0 ? camera.screen_width / camera.screen_height : 1;
		bool is_homogenous_depth = bgfx::getCaps()->homogeneousDepth;
		if (camera.is_ortho)
		{
			mtx.setOrtho(-camera.ortho_size * ratio,
				camera.ortho_size * ratio,
				-camera.ortho_size,
				camera.ortho_size,
				camera.near,
				camera.far,
				is_homogenous_depth);
		}
		else
		{
			mtx.setPerspective(camera.fov, ratio, camera.near, camera.far, is_homogenous_depth);
		}
		return mtx;
	}


	void setCameraScreenSize(Entity camera, int w, int h) override
	{
		auto& cam = m_cameras[{camera.index}];
		cam.screen_width = (float)w;
		cam.screen_height = (float)h;
		cam.aspect = w / (float)h;
	}


	Vec2 getCameraScreenSize(Entity camera) override
	{
		auto& cam = m_cameras[{camera.index}];
		return Vec2(cam.screen_width, cam.screen_height);
	}


	float getCameraOrthoSize(Entity camera) override { return m_cameras[{camera.index}].ortho_size; }
	void setCameraOrthoSize(Entity camera, float value) override { m_cameras[{camera.index}].ortho_size = value; }
	bool isCameraOrtho(Entity camera) override { return m_cameras[{camera.index}].is_ortho; }
	void setCameraOrtho(Entity camera, bool is_ortho) override { m_cameras[{camera.index}].is_ortho = is_ortho; }


	const Array<DebugTriangle>& getDebugTriangles() const override { return m_debug_triangles; }
	const Array<DebugLine>& getDebugLines() const override { return m_debug_lines; }
	const Array<DebugPoint>& getDebugPoints() const override { return m_debug_points; }


	void addDebugSphere(const Vec3& center,
		float radius,
		u32 color,
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


	void addDebugHalfSphere(const Matrix& transform, float radius, bool top, u32 color, float life)
	{
		Vec3 center = transform.getTranslation();
		Vec3 x_vec = transform.getXVector();
		Vec3 y_vec = transform.getYVector();
		if (!top) y_vec *= -1;
		Vec3 z_vec = transform.getZVector();
		static const int COLS = 36;
		static const int ROWS = COLS >> 1;
		static const float STEP = Math::degreesToRadians(360.0f) / COLS;
		for (int y = 0; y < ROWS >> 1; ++y)
		{
			float cy = cos(y * STEP);
			float cy1 = cos((y + 1) * STEP);
			float sy = sin(y * STEP);
			float sy1 = sin((y + 1) * STEP);
			float prev_ci = cos(-STEP);
			float prev_si = sin(-STEP);

			Vec3 y_offset = y_vec * sy;
			Vec3 y_offset1 = y_vec * sy1;

			for (int i = 0; i < COLS; ++i)
			{
				float ci = cos(i * STEP);
				float si = sin(i * STEP);

				addDebugLine(
					center + radius * (x_vec * ci * cy + z_vec * si * cy + y_offset),
					center + radius * (x_vec * prev_ci * cy + z_vec * prev_si * cy + y_offset),
					color,
					life);
				addDebugLine(
					center + radius * (x_vec * ci * cy + z_vec * si * cy + y_offset),
					center + radius * (x_vec * ci * cy1 + z_vec * si * cy1 + y_offset1),
					color,
					life);
				prev_ci = ci;
				prev_si = si;
			}
		}
	}


	void addDebugHalfSphere(const Vec3& center, float radius, bool top, u32 color, float life)
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


	void addDebugTriangle(const Vec3& p0,
		const Vec3& p1,
		const Vec3& p2,
		u32 color,
		float life) override
	{
		DebugTriangle& tri = m_debug_triangles.emplace();
		tri.p0 = p0;
		tri.p1 = p1;
		tri.p2 = p2;
		tri.color = ARGBToABGR(color);
		tri.life = life;
	}


	void addDebugCapsule(const Vec3& position,
		float height,
		float radius,
		u32 color,
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


	void addDebugCapsule(const Matrix& transform,
		float height,
		float radius,
		u32 color,
		float life) override
	{
		Vec3 x_vec = transform.getXVector();
		Vec3 y_vec = transform.getYVector();
		Vec3 z_vec = transform.getZVector();
		Vec3 position = transform.getTranslation();

		Matrix tmp = transform;
		tmp.setTranslation(transform.getTranslation() + y_vec * radius);
		addDebugHalfSphere(tmp, radius, false, color, life);
		tmp.setTranslation(transform.getTranslation() + y_vec * (radius + height));
		addDebugHalfSphere(tmp, radius, true, color, life);

		Vec3 bottom = position + y_vec * radius;
		Vec3 top = bottom + y_vec * height;
		for (int i = 1; i <= 32; ++i)
		{
			float a = i / 32.0f * 2 * Math::PI;
			float x = cosf(a) * radius;
			float z = sinf(a) * radius;
			addDebugLine(bottom + x_vec * x + z_vec * z, top + x_vec * x + z_vec * z, color, life);
		}
	}



	void addDebugCylinder(const Vec3& position,
								  const Vec3& up,
								  float radius,
								  u32 color,
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
		u32 color,
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


	void addDebugCubeSolid(const Vec3& min,
		const Vec3& max,
		u32 color,
		float life) override
	{
		Vec3 a = min;
		Vec3 b = min;
		Vec3 c = max;

		b.x = max.x;
		c.z = min.z;
		addDebugTriangle(a, c, b, color, life);
		b.x = min.x;
		b.y = max.y;
		addDebugTriangle(a, b, c, color, life);

		b = max;
		c = max;
		a.z = max.z;
		b.y = min.y;
		addDebugTriangle(a, b, c, color, life);
		b.x = min.x;
		b.y = max.y;
		addDebugTriangle(a, c, b, color, life);

		a = min;
		b = min;
		c = max;

		b.x = max.x;
		c.y = min.y;
		addDebugTriangle(a, b, c, color, life);
		b.x = min.x;
		b.z = max.z;
		addDebugTriangle(a, c, b, color, life);

		b = max;
		c = max;
		a.y = max.y;
		b.z = min.z;
		addDebugTriangle(a, c, b, color, life);
		b.x = min.x;
		b.z = max.z;
		addDebugTriangle(a, b, c, color, life);

		a = min;
		b = min;
		c = max;

		b.y = max.y;
		c.x = min.x;
		addDebugTriangle(a, c, b, color, life);
		b.y = min.y;
		b.z = max.z;
		addDebugTriangle(a, b, c, color, life);

		b = max;
		c = max;
		a.x = max.x;
		b.z = min.z;
		addDebugTriangle(a, b, c, color, life);
		b.y = min.y;
		b.z = max.z;
		addDebugTriangle(a, c, b, color, life);
	}



	void addDebugCube(const Vec3& min,
							  const Vec3& max,
							  u32 color,
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


	void addDebugFrustum(const Frustum& frustum, u32 color, float life) override
	{
		addDebugLine(frustum.points[0], frustum.points[1], color, life);
		addDebugLine(frustum.points[1], frustum.points[2], color, life);
		addDebugLine(frustum.points[2], frustum.points[3], color, life);
		addDebugLine(frustum.points[3], frustum.points[0], color, life);

		addDebugLine(frustum.points[4], frustum.points[5], color, life);
		addDebugLine(frustum.points[5], frustum.points[6], color, life);
		addDebugLine(frustum.points[6], frustum.points[7], color, life);
		addDebugLine(frustum.points[7], frustum.points[4], color, life);

		addDebugLine(frustum.points[0], frustum.points[4], color, life);
		addDebugLine(frustum.points[1], frustum.points[5], color, life);
		addDebugLine(frustum.points[2], frustum.points[6], color, life);
		addDebugLine(frustum.points[3], frustum.points[7], color, life);
	}


	void addDebugCircle(const Vec3& center, const Vec3& up, float radius, u32 color, float life) override
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
			addDebugLine(center + x_vec * x + z_vec * z, center + x_vec * prevx + z_vec * prevz, color, life);
			prevx = x;
			prevz = z;
		}
	}


	void addDebugCross(const Vec3& center, float size, u32 color, float life) override
	{
		addDebugLine(center, Vec3(center.x - size, center.y, center.z), color, life);
		addDebugLine(center, Vec3(center.x + size, center.y, center.z), color, life);
		addDebugLine(center, Vec3(center.x, center.y - size, center.z), color, life);
		addDebugLine(center, Vec3(center.x, center.y + size, center.z), color, life);
		addDebugLine(center, Vec3(center.x, center.y, center.z - size), color, life);
		addDebugLine(center, Vec3(center.x, center.y, center.z + size), color, life);
	}


	void addDebugPoint(const Vec3& pos, u32 color, float life) override
	{
		DebugPoint& point = m_debug_points.emplace();
		point.pos = pos;
		point.color = ARGBToABGR(color);
		point.life = life;
	}


	void addDebugCone(const Vec3& vertex,
		const Vec3& dir,
		const Vec3& axis0,
		const Vec3& axis1,
		u32 color,
		float life) override
	{
		Vec3 base_center = vertex + dir;
		Vec3 prev_p = base_center + axis0;
		for (int i = 1; i <= 32; ++i)
		{
			float angle = i / 32.0f * 2 * Math::PI;
			Vec3 x = cosf(angle) * axis0;
			Vec3 z = sinf(angle) * axis1;
			Vec3 p = base_center + x + z;
			addDebugLine(p, prev_p, color, life);
			addDebugLine(vertex, p, color, life);
			prev_p = p;
		}
	}


	static u32 ARGBToABGR(u32 color)
	{
		return ((color & 0xff) << 16) | (color & 0xff00) | ((color & 0xff0000) >> 16) | (color & 0xff000000);
	}


	void addDebugLine(const Vec3& from, const Vec3& to, u32 color, float life) override
	{
		DebugLine& line = m_debug_lines.emplace();
		line.from = from;
		line.to = to;
		line.color = ARGBToABGR(color);
		line.life = life;
	}


	RayCastModelHit castRayTerrain(Entity entity, const Vec3& origin, const Vec3& dir) override
	{
		RayCastModelHit hit;
		hit.m_is_hit = false;
		auto iter = m_terrains.find(entity);
		if (!iter.isValid()) return hit;

		auto* terrain = iter.value();
		hit = terrain->castRay(origin, dir);
		hit.m_component_type = TERRAIN_TYPE;
		hit.m_entity = terrain->getEntity();
		return hit;
	}


	RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, Entity ignored_model_instance) override
	{
		PROFILE_FUNCTION();
		RayCastModelHit hit;
		hit.m_is_hit = false;
		hit.m_origin = origin;
		hit.m_dir = dir;
		float cur_dist = FLT_MAX;
		Universe& universe = getUniverse();
		for (int i = 0; i < m_model_instances.size(); ++i)
		{
			auto& r = m_model_instances[i];
			if (ignored_model_instance.index == i || !r.model) continue;
			if (!r.flags.isSet(ModelInstance::ENABLED)) continue;

			const Vec3& pos = r.matrix.getTranslation();
			float scale = universe.getScale(r.entity);
			float radius = r.model->getBoundingRadius() * scale;
			float dist = (pos - origin).length();
			if (dist - radius > cur_dist) continue;
			
			Vec3 intersection;
			if (Math::getRaySphereIntersection(origin, dir, pos, radius, intersection))
			{
				RayCastModelHit new_hit = r.model->castRay(origin, dir, r.matrix, r.pose);
				if (new_hit.m_is_hit && (!hit.m_is_hit || new_hit.m_t < hit.m_t))
				{
					new_hit.m_entity = r.entity;
					new_hit.m_component_type = MODEL_INSTANCE_TYPE;
					hit = new_hit;
					hit.m_is_hit = true;
					cur_dist = dir.length() * hit.m_t;
				}
			}
		}

		for (auto* terrain : m_terrains)
		{
			RayCastModelHit terrain_hit = terrain->castRay(origin, dir);
			if (terrain_hit.m_is_hit && (!hit.m_is_hit || terrain_hit.m_t < hit.m_t))
			{
				terrain_hit.m_component_type = TERRAIN_TYPE;
				terrain_hit.m_entity = terrain->getEntity();
				hit = terrain_hit;
			}
		}

		return hit;
	}

	
	Vec4 getShadowmapCascades(Entity entity) override
	{
		return m_global_lights[entity].m_cascades;
	}


	void setShadowmapCascades(Entity entity, const Vec4& value) override
	{
		Vec4 valid_value = value;
		valid_value.x = Math::maximum(valid_value.x, 0.02f);
		valid_value.y = Math::maximum(valid_value.x + 0.01f, valid_value.y);
		valid_value.z = Math::maximum(valid_value.y + 0.01f, valid_value.z);
		valid_value.w = Math::maximum(valid_value.z + 0.01f, valid_value.w);

		m_global_lights[entity].m_cascades = valid_value;
	}


	void setFogDensity(Entity entity, float density) override
	{
		m_global_lights[entity].m_fog_density = density;
	}


	void setFogColor(Entity entity, const Vec3& color) override
	{
		m_global_lights[entity].m_fog_color = color;
	}


	float getFogDensity(Entity entity) override
	{
		return m_global_lights[entity].m_fog_density;
	}


	float getFogBottom(Entity entity) override
	{
		return m_global_lights[entity].m_fog_bottom;
	}


	void setFogBottom(Entity entity, float bottom) override
	{
		m_global_lights[entity].m_fog_bottom = bottom;
	}


	float getFogHeight(Entity entity) override
	{
		return m_global_lights[entity].m_fog_height;
	}


	void setFogHeight(Entity entity, float height) override
	{
		m_global_lights[entity].m_fog_height = height;
	}


	Vec3 getFogColor(Entity entity) override
	{
		return m_global_lights[entity].m_fog_color;
	}


	float getLightAttenuation(Entity entity) override
	{
		return m_point_lights[m_point_lights_map[entity]].m_attenuation_param;
	}


	void setLightAttenuation(Entity entity, float attenuation) override
	{
		int index = m_point_lights_map[entity];
		m_point_lights[index].m_attenuation_param = attenuation;
	}


	float getLightRange(Entity entity) override
	{
		return m_point_lights[m_point_lights_map[entity]].m_range;
	}


	void setLightRange(Entity entity, float value) override
	{
		m_point_lights[m_point_lights_map[entity]].m_range = value;
	}


	void setPointLightIntensity(Entity entity, float intensity) override
	{
		m_point_lights[m_point_lights_map[entity]].m_diffuse_intensity = intensity;
	}


	void setGlobalLightIntensity(Entity entity, float intensity) override
	{
		m_global_lights[entity].m_diffuse_intensity = intensity;
	}


	void setGlobalLightIndirectIntensity(Entity entity, float intensity) override
	{
		m_global_lights[entity].m_indirect_intensity = intensity;
	}


	void setPointLightColor(Entity entity, const Vec3& color) override
	{
		m_point_lights[m_point_lights_map[entity]].m_diffuse_color = color;
	}


	void setGlobalLightColor(Entity entity, const Vec3& color) override
	{
		m_global_lights[entity].m_diffuse_color = color;
	}

	
	float getPointLightIntensity(Entity entity) override
	{
		return m_point_lights[m_point_lights_map[entity]].m_diffuse_intensity;
	}


	float getGlobalLightIntensity(Entity entity) override
	{
		return m_global_lights[entity].m_diffuse_intensity;
	}


	float getGlobalLightIndirectIntensity(Entity entity) override
	{
		return m_global_lights[entity].m_indirect_intensity;
	}


	Vec3 getPointLightColor(Entity entity) override
	{
		return m_point_lights[m_point_lights_map[entity]].m_diffuse_color;
	}


	void setPointLightSpecularColor(Entity entity, const Vec3& color) override
	{
		m_point_lights[m_point_lights_map[entity]].m_specular_color = color;
	}


	Vec3 getPointLightSpecularColor(Entity entity) override
	{
		return m_point_lights[m_point_lights_map[entity]].m_specular_color;
	}


	void setPointLightSpecularIntensity(Entity entity, float intensity) override
	{
		m_point_lights[m_point_lights_map[entity]].m_specular_intensity = intensity;
	}


	float getPointLightSpecularIntensity(Entity entity) override
	{
		return m_point_lights[m_point_lights_map[entity]].m_specular_intensity;
	}


	Vec3 getGlobalLightColor(Entity entity) override
	{
		return m_global_lights[entity].m_diffuse_color;
	}


	void setActiveGlobalLight(Entity entity) override
	{
		m_active_global_light_entity = entity;
	}


	Entity getActiveGlobalLight() override
	{
		return m_active_global_light_entity;
	}


	Entity getPointLightEntity(Entity entity) const override
	{
		return m_point_lights[m_point_lights_map[entity]].m_entity;
	}


	Entity getGlobalLightEntity(Entity entity) const override
	{
		return m_global_lights[entity].m_entity;
	}


	void reloadEnvironmentProbe(Entity entity) override
	{
		auto& probe = m_environment_probes[entity];
		auto* texture_manager = m_engine.getResourceManager().get(Texture::TYPE);
		if (probe.texture) texture_manager->unload(*probe.texture);
		StaticString<MAX_PATH_LENGTH> path("universes/", m_universe.getName(), "/probes/", probe.guid, ".dds");
		probe.texture = static_cast<Texture*>(texture_manager->load(Path(path)));
		probe.texture->setFlag(BGFX_TEXTURE_SRGB, true);
		path = "universes/";
		path << m_universe.getName() << "/probes/" << probe.guid << "_irradiance.dds";
		probe.irradiance = static_cast<Texture*>(texture_manager->load(Path(path)));
		probe.irradiance->setFlag(BGFX_TEXTURE_SRGB, true);
		probe.irradiance->setFlag(BGFX_TEXTURE_MIN_ANISOTROPIC, true);
		probe.irradiance->setFlag(BGFX_TEXTURE_MAG_ANISOTROPIC, true);
		path = "universes/";
		path << m_universe.getName() << "/probes/" << probe.guid << "_radiance.dds";
		probe.radiance = static_cast<Texture*>(texture_manager->load(Path(path)));
		probe.radiance->setFlag(BGFX_TEXTURE_SRGB, true);
		probe.radiance->setFlag(BGFX_TEXTURE_MIN_ANISOTROPIC, true);
		probe.radiance->setFlag(BGFX_TEXTURE_MAG_ANISOTROPIC, true);
	}


	Entity getNearestEnvironmentProbe(const Vec3& pos) const override
	{
		float nearest_dist_squared = FLT_MAX;
		Entity nearest = INVALID_ENTITY;
		for (int i = 0, c = m_environment_probes.size(); i < c; ++i)
		{
			Entity probe_entity = m_environment_probes.getKey(i);
			Vec3 probe_pos = m_universe.getPosition(probe_entity);
			float dist_squared = (pos - probe_pos).squaredLength();
			if (dist_squared < nearest_dist_squared)
			{
				nearest = probe_entity;
				nearest_dist_squared = dist_squared;
			}
		}
		if (!nearest.isValid()) return INVALID_ENTITY;
		return nearest;
	}


	Texture* getEnvironmentProbeTexture(Entity entity) const override
	{
		return m_environment_probes[entity].texture;
	}


	Texture* getEnvironmentProbeIrradiance(Entity entity) const override
	{
		return m_environment_probes[entity].irradiance;
	}


	Texture* getEnvironmentProbeRadiance(Entity entity) const override
	{
		return m_environment_probes[entity].radiance;
	}


	u64 getEnvironmentProbeGUID(Entity entity) const override
	{
		return m_environment_probes[entity].guid;
	}


	Entity getCameraInSlot(const char* slot) override
	{
		for (const auto& camera : m_cameras)
		{
			if (equalStrings(camera.slot, slot))
			{
				return camera.entity;
			}
		}
		return INVALID_ENTITY;
	}


	float getTime() const override { return m_time; }


	void modelUnloaded(Model*, Entity entity)
	{
		auto& r = m_model_instances[entity.index];
		if (!hasCustomMeshes(r))
		{
			r.meshes = nullptr;
			r.mesh_count = 0;
		}
		LUMIX_DELETE(m_allocator, r.pose);
		r.pose = nullptr;

		for (int i = 0; i < m_point_lights.size(); ++i)
		{
			m_light_influenced_geometry[i].eraseItemFast(entity);
		}
		m_culling_system->removeStatic(entity);
	}


	void freeCustomMeshes(ModelInstance& r, MaterialManager* manager)
	{
		if (!hasCustomMeshes(r)) return;
		for (int i = 0; i < r.mesh_count; ++i)
		{
			manager->unload(*r.meshes[i].material);
			r.meshes[i].~Mesh();
		}
		m_allocator.deallocate(r.meshes);
		r.meshes = nullptr;
		r.flags.unset(ModelInstance::CUSTOM_MESHES);
		r.mesh_count = 0;
	}


	void modelLoaded(Model* model, Entity entity)
	{
		auto& rm = m_engine.getResourceManager();
		auto* material_manager = static_cast<MaterialManager*>(rm.get(Material::TYPE));

		auto& r = m_model_instances[entity.index];

		float bounding_radius = r.model->getBoundingRadius();
		float scale = m_universe.getScale(r.entity);
		Sphere sphere(r.matrix.getTranslation(), bounding_radius * scale);
		if(r.flags.isSet(ModelInstance::ENABLED)) m_culling_system->addStatic(entity, sphere, getLayerMask(r));
		ASSERT(!r.pose);
		if (model->getBoneCount() > 0)
		{
			r.pose = LUMIX_NEW(m_allocator, Pose)(m_allocator);
			r.pose->resize(model->getBoneCount());
			model->getPose(*r.pose);
			int skinned_define_idx = m_renderer.getShaderDefineIdx("SKINNED");
			for (int i = 0; i < model->getMeshCount(); ++i)
			{
				Mesh& mesh = model->getMesh(i);
				mesh.material->setDefine(skinned_define_idx, !mesh.skin.empty());
			}
		}
		r.matrix = m_universe.getMatrix(r.entity);
		ASSERT(!r.meshes || hasCustomMeshes(r));
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
				r.meshes[i].set(src);
			}
		}
		else
		{
			r.meshes = &r.model->getMesh(0);
			r.mesh_count = r.model->getMeshCount();
		}

		if (r.flags.isSet(ModelInstance::IS_BONE_ATTACHMENT_PARENT))
		{
			updateBoneAttachment(m_bone_attachments[r.entity]);
		}

		for (int i = 0; i < m_point_lights.size(); ++i)
		{
			PointLight& light = m_point_lights[i];
			Vec3 t = r.matrix.getTranslation();
			float radius = r.model->getBoundingRadius();
			if ((t - m_universe.getPosition(light.m_entity)).squaredLength() <
				(radius + light.m_range) * (radius + light.m_range))
			{
				m_light_influenced_geometry[i].push(entity);
			}
		}
	}


	void modelUnloaded(Model* model)
	{
		for (int i = 0, c = m_model_instances.size(); i < c; ++i)
		{
			if (m_model_instances[i].entity != INVALID_ENTITY && m_model_instances[i].model == model)
			{
				modelUnloaded(model, {i});
			}
		}
	}


	void modelLoaded(Model* model)
	{
		for (int i = 0, c = m_model_instances.size(); i < c; ++i)
		{
			if (m_model_instances[i].entity != INVALID_ENTITY && m_model_instances[i].model == model)
			{
				modelLoaded(model, {i});
			}
		}
	}


	ModelLoadedCallback& getModelLoadedCallback(Model* model)
	{
		int idx = m_model_loaded_callbacks.find(model);
		if (idx >= 0) return m_model_loaded_callbacks.at(idx);
		return m_model_loaded_callbacks.emplace(model, *this, model);
	}


	void allocateCustomMeshes(ModelInstance& r, int count)
	{
		if (hasCustomMeshes(r) && r.mesh_count == count) return;

		ASSERT(r.model);
		auto& rm = r.model->getResourceManager();
		auto* material_manager = static_cast<MaterialManager*>(rm.getOwner().get(Material::TYPE));

		auto* new_meshes = (Mesh*)m_allocator.allocate(count * sizeof(Mesh));
		if (r.meshes)
		{
			for (int i = 0; i < r.mesh_count; ++i)
			{
				new (NewPlaceholder(), new_meshes + i) Mesh(r.meshes[i]);
			}

			if (hasCustomMeshes(r))
			{
				for (int i = count; i < r.mesh_count; ++i)
				{
					material_manager->unload(*r.meshes[i].material);
				}
				for (int i = 0; i < r.mesh_count; ++i)
				{
					r.meshes[i].~Mesh();
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
			new (NewPlaceholder(), new_meshes + i) Mesh(nullptr, bgfx::VertexDecl(), "", m_allocator);
		}
		r.meshes = new_meshes;
		r.mesh_count = count;
		r.flags.set(ModelInstance::CUSTOM_MESHES);
	}


	bool getModelInstanceKeepSkin(Entity entity) override
	{
		auto& r = m_model_instances[entity.index];
		return keepSkin(r);
	}


	void setModelInstanceKeepSkin(Entity entity, bool keep) override
	{
		auto& r = m_model_instances[entity.index];
		r.flags.set(ModelInstance::KEEP_SKIN, keep);
	}


	void setModelInstanceMaterial(Entity entity, int index, const Path& path) override
	{
		auto& r = m_model_instances[entity.index];
		if (r.meshes && r.mesh_count > index && r.meshes[index].material && path == r.meshes[index].material->getPath()) return;

		auto& rm = r.model->getResourceManager();
		auto* material_manager = static_cast<MaterialManager*>(rm.getOwner().get(Material::TYPE));

		int new_count = Math::maximum(i8(index + 1), r.mesh_count);
		allocateCustomMeshes(r, new_count);
		ASSERT(r.meshes);

		auto* new_material = static_cast<Material*>(material_manager->load(path));
		r.meshes[index].setMaterial(new_material, *r.model, m_renderer);
	}


	Path getModelInstanceMaterial(Entity entity, int index) override
	{
		auto& r = m_model_instances[entity.index];
		if (!r.meshes) return Path("");

		return r.meshes[index].material->getPath();
	}


	void setModel(Entity entity, Model* model)
	{
		auto& model_instance = m_model_instances[entity.index];
		ASSERT(model_instance.entity.isValid());
		Model* old_model = model_instance.model;
		bool no_change = model == old_model && old_model;
		if (no_change)
		{
			old_model->getResourceManager().unload(*old_model);
			return;
		}
		if (old_model)
		{
			auto& rm = old_model->getResourceManager();
			auto* material_manager = static_cast<MaterialManager*>(rm.getOwner().get(Material::TYPE));
			freeCustomMeshes(model_instance, material_manager);
			ModelLoadedCallback& callback = getModelLoadedCallback(old_model);
			--callback.m_ref_count;
			if (callback.m_ref_count == 0)
			{
				m_model_loaded_callbacks.erase(old_model);
			}

			if (old_model->isReady())
			{
				m_culling_system->removeStatic(entity);
			}
			old_model->getResourceManager().unload(*old_model);
		}
		model_instance.model = model;
		model_instance.meshes = nullptr;
		model_instance.mesh_count = 0;
		LUMIX_DELETE(m_allocator, model_instance.pose);
		model_instance.pose = nullptr;
		if (model)
		{
			if (keepSkin(model_instance)) model->setKeepSkin();
			ModelLoadedCallback& callback = getModelLoadedCallback(model);
			++callback.m_ref_count;

			if (model->isReady())
			{
				modelLoaded(model, entity);
			}
		}
	}

	IAllocator& getAllocator() override { return m_allocator; }


	void detectLightInfluencedGeometry(Entity entity)
	{
		int light_idx = m_point_lights_map[entity];
		Frustum frustum = getPointLightFrustum(light_idx);
		const CullingSystem::Results& results = m_culling_system->cull(frustum, ~0ULL);
		auto& influenced_geometry = m_light_influenced_geometry[light_idx];
		influenced_geometry.clear();
		for (int i = 0; i < results.size(); ++i)
		{
			const CullingSystem::Subresults& subresult = results[i];
			influenced_geometry.reserve(influenced_geometry.size() + subresult.size());
			for (int j = 0, c = subresult.size(); j < c; ++j)
			{
				influenced_geometry.push(subresult[j]);
			}
		}
	}


	int getParticleEmitterAttractorCount(Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::AttractorModule>(entity);
		return module ? module->m_count : 0;
	}


	void addParticleEmitterAttractor(Entity entity, int index) override
	{
		auto* module = getEmitterModule<ParticleEmitter::AttractorModule>(entity);
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


	void removeParticleEmitterAttractor(Entity entity, int index) override
	{
		auto* module = getEmitterModule<ParticleEmitter::AttractorModule>(entity);
		if (!module) return;

		for (int i = index; i < module->m_count - 1; ++i)
		{
			module->m_entities[i] = module->m_entities[i + 1];
		}
		--module->m_count;
	}


	Entity getParticleEmitterAttractorEntity(Entity entity, int index) override
	{
		auto* module = getEmitterModule<ParticleEmitter::AttractorModule>(entity);
		return module ? module->m_entities[index] : INVALID_ENTITY;
	}


	void setParticleEmitterAttractorEntity(Entity module_entity, int index, Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::AttractorModule>(module_entity);
		if(module) module->m_entities[index] = entity;
	}


	float getParticleEmitterShapeRadius(Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::SpawnShapeModule>(entity);
		return module ? module->m_radius : 0.0f;
	}


	void setParticleEmitterShapeRadius(Entity entity, float value) override
	{
		auto* module = getEmitterModule<ParticleEmitter::SpawnShapeModule>(entity);
		if (module) module->m_radius = value;
	}


	int getParticleEmitterPlaneCount(Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::PlaneModule>(entity);
		return module ? module->m_count : 0;
	}


	void addParticleEmitterPlane(Entity entity, int index) override
	{
		auto* plane_module = getEmitterModule<ParticleEmitter::PlaneModule>(entity);
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


	void removeParticleEmitterPlane(Entity entity, int index) override
	{
		auto* plane_module = getEmitterModule<ParticleEmitter::PlaneModule>(entity);
		if (!plane_module) return;

		for (int i = index; i < plane_module->m_count - 1; ++i)
		{
			plane_module->m_entities[i] = plane_module->m_entities[i + 1];
		}
		--plane_module->m_count;
	}


	Entity getParticleEmitterPlaneEntity(Entity entity, int index) override
	{
		auto* module = getEmitterModule<ParticleEmitter::PlaneModule>(entity);
		return module ? module->m_entities[index] : INVALID_ENTITY;
	}


	void setParticleEmitterPlaneEntity(Entity module_entity, int index, Entity entity) override
	{
		auto* module = getEmitterModule<ParticleEmitter::PlaneModule>(module_entity);
		if (module) module->m_entities[index] = entity;
	}


	float getLightFOV(Entity entity) override
	{
		return m_point_lights[m_point_lights_map[entity]].m_fov;
	}


	void setLightFOV(Entity entity, float fov) override
	{
		m_point_lights[m_point_lights_map[entity]].m_fov = fov;
	}


	void createGlobalLight(Entity entity)
	{
		GlobalLight light;
		light.m_entity = entity;
		light.m_diffuse_color.set(1, 1, 1);
		light.m_diffuse_intensity = 0;
		light.m_indirect_intensity = 1;
		light.m_fog_color.set(1, 1, 1);
		light.m_fog_density = 0;
		light.m_cascades.set(3, 8, 100, 300);
		light.m_fog_bottom = 0.0f;
		light.m_fog_height = 10.0f;

		if (m_global_lights.empty()) m_active_global_light_entity = entity;

		m_global_lights.insert(entity, light);
		m_universe.onComponentCreated(entity, GLOBAL_LIGHT_TYPE, this);
	}


	void createPointLight(Entity entity)
	{
		PointLight& light = m_point_lights.emplace();
		m_light_influenced_geometry.emplace(m_allocator);
		light.m_entity = entity;
		light.m_diffuse_color.set(1, 1, 1);
		light.m_diffuse_intensity = 1;
		light.m_fov = Math::degreesToRadians(360);
		light.m_specular_color.set(1, 1, 1);
		light.m_specular_intensity = 1;
		light.m_cast_shadows = false;
		light.m_attenuation_param = 2;
		light.m_range = 10;
		m_point_lights_map.insert(entity, m_point_lights.size() - 1);

		m_universe.onComponentCreated(entity, POINT_LIGHT_TYPE, this);

		detectLightInfluencedGeometry(entity);
	}


	void updateDecalInfo(Decal& decal) const
	{
		decal.position = m_universe.getPosition(decal.entity);
		decal.radius = decal.scale.length();
		decal.mtx = m_universe.getMatrix(decal.entity);
		decal.mtx.setXVector(decal.mtx.getXVector() * decal.scale.x);
		decal.mtx.setYVector(decal.mtx.getYVector() * decal.scale.y);
		decal.mtx.setZVector(decal.mtx.getZVector() * decal.scale.z);
		decal.inv_mtx = decal.mtx;
		decal.inv_mtx.inverse();
	}


	void createDecal(Entity entity)
	{
		Decal& decal = m_decals.insert(entity);
		decal.material = nullptr;
		decal.entity = entity;
		decal.scale.set(1, 1, 1);
		updateDecalInfo(decal);

		m_universe.onComponentCreated(entity, DECAL_TYPE, this);
	}


	void createEnvironmentProbe(Entity entity)
	{
		EnvironmentProbe& probe = m_environment_probes.insert(entity);
		auto* texture_manager = m_engine.getResourceManager().get(Texture::TYPE);
		probe.texture = static_cast<Texture*>(texture_manager->load(Path("pipelines/pbr/default_probe.dds")));
		probe.texture->setFlag(BGFX_TEXTURE_SRGB, true);
		probe.irradiance = static_cast<Texture*>(texture_manager->load(Path("pipelines/pbr/default_probe.dds")));
		probe.irradiance->setFlag(BGFX_TEXTURE_SRGB, true);
		probe.radiance = static_cast<Texture*>(texture_manager->load(Path("pipelines/pbr/default_probe.dds")));
		probe.radiance->setFlag(BGFX_TEXTURE_SRGB, true);
		probe.guid = Math::randGUID();

		m_universe.onComponentCreated(entity, ENVIRONMENT_PROBE_TYPE, this);
	}


	void createBoneAttachment(Entity entity)
	{
		BoneAttachment& attachment = m_bone_attachments.emplace(entity);
		attachment.entity = entity;
		attachment.parent_entity = INVALID_ENTITY;
		attachment.bone_index = -1;

		m_universe.onComponentCreated(entity, BONE_ATTACHMENT_TYPE, this);
	}


	void createModelInstance(Entity entity)
	{
		while(entity.index >= m_model_instances.size())
		{
			auto& r = m_model_instances.emplace();
			r.entity = INVALID_ENTITY;
			r.model = nullptr;
			r.pose = nullptr;
		}
		auto& r = m_model_instances[entity.index];
		r.entity = entity;
		r.model = nullptr;
		r.meshes = nullptr;
		r.pose = nullptr;
		r.flags.clear();
		r.flags.set(ModelInstance::ENABLED);
		r.mesh_count = 0;
		r.matrix = m_universe.getMatrix(entity);
		m_universe.onComponentCreated(entity, MODEL_INSTANCE_TYPE, this);
	}


	void setScriptedParticleEmitterMaterialPath(Entity entity, const Path& path) override
	{
		if (!m_scripted_particle_emitters[entity]) return;

		auto* manager = m_engine.getResourceManager().get(Material::TYPE);
		Material* material = static_cast<Material*>(manager->load(path));
		m_scripted_particle_emitters[entity]->setMaterial(material);
	}


	Path getScriptedParticleEmitterMaterialPath(Entity entity) override
	{
		ScriptedParticleEmitter* emitter = m_scripted_particle_emitters[entity];
		if (!emitter) return Path("");
		if (!emitter->getMaterial()) return Path("");

		return emitter->getMaterial()->getPath();
	}

	void setParticleEmitterMaterialPath(Entity entity, const Path& path) override
	{
		if (!m_particle_emitters[entity]) return;

		auto* manager = m_engine.getResourceManager().get(Material::TYPE);
		Material* material = static_cast<Material*>(manager->load(path));
		m_particle_emitters[entity]->setMaterial(material);
	}


	Path getParticleEmitterMaterialPath(Entity entity) override
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		if (!emitter) return Path("");
		if (!emitter->getMaterial()) return Path("");

		return emitter->getMaterial()->getPath();
	}


	const AssociativeArray<Entity, ParticleEmitter*>& getParticleEmitters() const override
	{
		return m_particle_emitters;
	}

	const AssociativeArray<Entity, ScriptedParticleEmitter*>& getScriptedParticleEmitters() const override
	{
		return m_scripted_particle_emitters;
	}

private:
	IAllocator& m_allocator;
	Universe& m_universe;
	Renderer& m_renderer;
	Engine& m_engine;
	CullingSystem* m_culling_system;

	Array<Array<Entity>> m_light_influenced_geometry;
	Entity m_active_global_light_entity;
	HashMap<Entity, int> m_point_lights_map;

	AssociativeArray<Entity, Decal> m_decals;
	Array<ModelInstance> m_model_instances;
	HashMap<Entity, GlobalLight> m_global_lights;
	Array<PointLight> m_point_lights;
	HashMap<Entity, Camera> m_cameras;
	AssociativeArray<Entity, BoneAttachment> m_bone_attachments;
	AssociativeArray<Entity, EnvironmentProbe> m_environment_probes;
	HashMap<Entity, Terrain*> m_terrains;
	AssociativeArray<Entity, ParticleEmitter*> m_particle_emitters;
	AssociativeArray<Entity, ScriptedParticleEmitter*> m_scripted_particle_emitters;

	Array<DebugTriangle> m_debug_triangles;
	Array<DebugLine> m_debug_lines;
	Array<DebugPoint> m_debug_points;

	Array<Array<ModelInstanceMesh>> m_temporary_infos;

	float m_time;
	float m_lod_multiplier;
	bool m_is_updating_attachments;
	bool m_is_grass_enabled;
	bool m_is_game_running;

	AssociativeArray<Model*, ModelLoadedCallback> m_model_loaded_callbacks;
};



#define COMPONENT_TYPE(type, name) \
	{ \
		type \
		, static_cast<Universe::Serialize>(&RenderSceneImpl::serialize##name) \
		, static_cast<Universe::Deserialize>(&RenderSceneImpl::deserialize##name) \
		, &RenderSceneImpl::create##name \
		, &RenderSceneImpl::destroy##name \
	}

static struct
{
	ComponentType type;
	Universe::Serialize serialize;
	Universe::Deserialize deserialize;
	void (RenderSceneImpl::*creator)(Entity);
	void (RenderSceneImpl::*destroyer)(Entity);
} COMPONENT_INFOS[] = {
	COMPONENT_TYPE(MODEL_INSTANCE_TYPE, ModelInstance),
	COMPONENT_TYPE(GLOBAL_LIGHT_TYPE, GlobalLight),
	COMPONENT_TYPE(POINT_LIGHT_TYPE, PointLight),
	COMPONENT_TYPE(DECAL_TYPE, Decal),
	COMPONENT_TYPE(CAMERA_TYPE, Camera),
	COMPONENT_TYPE(TERRAIN_TYPE, Terrain),
	COMPONENT_TYPE(BONE_ATTACHMENT_TYPE, BoneAttachment),
	COMPONENT_TYPE(ENVIRONMENT_PROBE_TYPE, EnvironmentProbe),
	COMPONENT_TYPE(PARTICLE_EMITTER_TYPE, ParticleEmitter),
	COMPONENT_TYPE(SCRIPTED_PARTICLE_EMITTER_TYPE, ScriptedParticleEmitter),
	COMPONENT_TYPE(PARTICLE_EMITTER_ALPHA_TYPE, ParticleEmitterAlpha),
	COMPONENT_TYPE(PARTICLE_EMITTER_ATTRACTOR_TYPE, ParticleEmitterAttractor),
	COMPONENT_TYPE(PARTICLE_EMITTER_FORCE_HASH, ParticleEmitterForce),
	COMPONENT_TYPE(PARTICLE_EMITTER_LINEAR_MOVEMENT_TYPE, ParticleEmitterLinearMovement),
	COMPONENT_TYPE(PARTICLE_EMITTER_PLANE_TYPE, ParticleEmitterPlane),
	COMPONENT_TYPE(PARTICLE_EMITTER_RANDOM_ROTATION_TYPE, ParticleEmitterRandomRotation),
	COMPONENT_TYPE(PARTICLE_EMITTER_SIZE_TYPE, ParticleEmitterSize),
	COMPONENT_TYPE(PARTICLE_EMITTER_SPAWN_SHAPE_TYPE, ParticleEmitterSpawnShape),
	COMPONENT_TYPE(PARTICLE_EMITTER_SUBIMAGE_TYPE, ParticleEmitterSubimage)
};

#undef COMPONENT_TYPE

RenderSceneImpl::RenderSceneImpl(Renderer& renderer,
	Engine& engine,
	Universe& universe,
	IAllocator& allocator)
	: m_engine(engine)
	, m_universe(universe)
	, m_renderer(renderer)
	, m_allocator(allocator)
	, m_model_loaded_callbacks(m_allocator)
	, m_model_instances(m_allocator)
	, m_cameras(m_allocator)
	, m_terrains(m_allocator)
	, m_point_lights(m_allocator)
	, m_light_influenced_geometry(m_allocator)
	, m_global_lights(m_allocator)
	, m_decals(m_allocator)
	, m_debug_triangles(m_allocator)
	, m_debug_lines(m_allocator)
	, m_debug_points(m_allocator)
	, m_temporary_infos(m_allocator)
	, m_active_global_light_entity(INVALID_ENTITY)
	, m_is_grass_enabled(true)
	, m_is_game_running(false)
	, m_particle_emitters(m_allocator)
	, m_scripted_particle_emitters(m_allocator)
	, m_point_lights_map(m_allocator)
	, m_bone_attachments(m_allocator)
	, m_environment_probes(m_allocator)
	, m_lod_multiplier(1.0f)
	, m_time(0)
	, m_is_updating_attachments(false)
{
	m_universe.entityTransformed().bind<RenderSceneImpl, &RenderSceneImpl::onEntityMoved>(this);
	m_universe.entityDestroyed().bind<RenderSceneImpl, &RenderSceneImpl::onEntityDestroyed>(this);
	m_culling_system = CullingSystem::create(m_allocator);
	m_model_instances.reserve(5000);

	for (auto& i : COMPONENT_INFOS)
	{
		universe.registerComponentType(i.type, this, i.creator, i.destroyer, i.serialize, i.deserialize);
	}
}


RenderScene* RenderScene::createInstance(Renderer& renderer,
	Engine& engine,
	Universe& universe,
	IAllocator& allocator)
{
	return LUMIX_NEW(allocator, RenderSceneImpl)(renderer, engine, universe, allocator);
}


void RenderScene::destroyInstance(RenderScene* scene)
{
	LUMIX_DELETE(scene->getAllocator(), static_cast<RenderSceneImpl*>(scene));
}


void RenderScene::registerLuaAPI(lua_State* L)
{
	Pipeline::registerLuaAPI(L);
	Model::registerLuaAPI(L);

	#define REGISTER_FUNCTION(F)\
		do { \
		auto f = &LuaWrapper::wrapMethod<RenderSceneImpl, decltype(&RenderSceneImpl::F), &RenderSceneImpl::F>; \
		LuaWrapper::createSystemFunction(L, "Renderer", #F, f); \
		} while(false) \

	REGISTER_FUNCTION(setGlobalLODMultiplier);
	REGISTER_FUNCTION(getGlobalLODMultiplier);
	REGISTER_FUNCTION(getCameraViewProjection);
	REGISTER_FUNCTION(getGlobalLightEntity);
	REGISTER_FUNCTION(getActiveGlobalLight);
	REGISTER_FUNCTION(getCameraInSlot);
	REGISTER_FUNCTION(getCameraSlot);
	REGISTER_FUNCTION(getModelInstanceModel);
	REGISTER_FUNCTION(addDebugCross);
	REGISTER_FUNCTION(addDebugLine);
	REGISTER_FUNCTION(addDebugCircle);
	REGISTER_FUNCTION(addDebugSphere);
	REGISTER_FUNCTION(getTerrainMaterial);
	REGISTER_FUNCTION(getTerrainNormalAt);
	REGISTER_FUNCTION(setTerrainHeightAt);
	REGISTER_FUNCTION(enableModelInstance);
	REGISTER_FUNCTION(getPoseBonePosition);

	#undef REGISTER_FUNCTION

	#define REGISTER_FUNCTION(F)\
		do { \
		auto f = &LuaWrapper::wrap<decltype(&RenderSceneImpl::LUA_##F), &RenderSceneImpl::LUA_##F>; \
		LuaWrapper::createSystemFunction(L, "Renderer", #F, f); \
		} while(false) \

	REGISTER_FUNCTION(createPipeline);
	REGISTER_FUNCTION(destroyPipeline);
	REGISTER_FUNCTION(setPipelineScene);
	REGISTER_FUNCTION(getPipelineScene);
	REGISTER_FUNCTION(pipelineRender);
	REGISTER_FUNCTION(getRenderBuffer);
	REGISTER_FUNCTION(getMaterialTexture);
	REGISTER_FUNCTION(getTextureWidth);
	REGISTER_FUNCTION(getTextureHeight);
	REGISTER_FUNCTION(getTexturePixel);
	REGISTER_FUNCTION(setTexturePixel);
	REGISTER_FUNCTION(getTextureHandle);
	REGISTER_FUNCTION(updateTextureData);
	REGISTER_FUNCTION(setModelInstanceMaterial);
	REGISTER_FUNCTION(setModelInstancePath);
	REGISTER_FUNCTION(getModelBoneIndex);
	REGISTER_FUNCTION(makeScreenshot);
	REGISTER_FUNCTION(compareTGA);
	REGISTER_FUNCTION(getTerrainHeightAt);
	REGISTER_FUNCTION(emitParticle);

	LuaWrapper::createSystemFunction(L, "Renderer", "castCameraRay", &RenderSceneImpl::LUA_castCameraRay);

	#undef REGISTER_FUNCTION
}


} // namespace Lumix