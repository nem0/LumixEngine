#include "render_scene.h"

#include "engine/array.h"
#include "engine/associative_array.h"
#include "engine/crc32.h"
#include "engine/crt.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/geometry.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/page_allocator.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "engine/universe.h"
#include "imgui/IconsFontAwesome5.h"
#include "renderer/culling_system.h"
#include "renderer/font.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/particle_system.h"
#include "renderer/pipeline.h"
#include "renderer/pose.h"
#include "renderer/renderer.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"


namespace Lumix
{


enum class RenderSceneVersion : int
{
	DECAL_UV_SCALE,
	LATEST
};


static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
static const ComponentType DECAL_TYPE = reflection::getComponentType("decal");
static const ComponentType POINT_LIGHT_TYPE = reflection::getComponentType("point_light");
static const ComponentType PARTICLE_EMITTER_TYPE = reflection::getComponentType("particle_emitter");
static const ComponentType ENVIRONMENT_TYPE = reflection::getComponentType("environment");
static const ComponentType CAMERA_TYPE = reflection::getComponentType("camera");
static const ComponentType TERRAIN_TYPE = reflection::getComponentType("terrain");
static const ComponentType BONE_ATTACHMENT_TYPE = reflection::getComponentType("bone_attachment");
static const ComponentType ENVIRONMENT_PROBE_TYPE = reflection::getComponentType("environment_probe");
static const ComponentType REFLECTION_PROBE_TYPE = reflection::getComponentType("reflection_probe");
static const ComponentType FUR_TYPE = reflection::getComponentType("fur");


struct Decal
{
	Material* material = nullptr;
	Transform transform;
	float radius;
	EntityRef entity;
	EntityPtr prev_decal = INVALID_ENTITY;
	EntityPtr next_decal = INVALID_ENTITY;
	Vec3 half_extents;
	Vec2 uv_scale;
};


struct BoneAttachment
{
	EntityRef entity;
	EntityPtr parent_entity;
	int bone_index;
	LocalRigidTransform relative_transform;
};


static RenderableTypes getRenderableType(const Model& model, bool custom_material)
{
	ASSERT(model.isReady());
	if (custom_material) return RenderableTypes::MESH_MATERIAL_OVERRIDE;
	if (model.isSkinned()) return RenderableTypes::SKINNED;
	if (model.getMeshCount() > 1) return RenderableTypes::MESH_GROUP;
	return RenderableTypes::MESH;
}


struct RenderSceneImpl final : RenderScene
{
public:
	RenderSceneImpl(Renderer& renderer,
		Engine& engine,
		Universe& universe,
		IAllocator& allocator);

	~RenderSceneImpl()
	{
		m_renderer.destroy(m_reflection_probes_texture);
		m_universe.entityTransformed().unbind<&RenderSceneImpl::onEntityMoved>(this);
		m_universe.entityDestroyed().unbind<&RenderSceneImpl::onEntityDestroyed>(this);
		m_culling_system.reset();
	}


	void decalMaterialStateChanged(Resource::State old_state, Resource::State new_state, Resource& resource)
	{
		Material& material = static_cast<Material&>(resource);
		
		if (new_state == Resource::State::READY) {
			auto map_iter = m_material_decal_map.find(&material);
			EntityPtr e = map_iter.value();
			while(e.isValid()) {
				const float radius = length(m_decals[(EntityRef)e].half_extents);
				const DVec3 pos = m_universe.getPosition((EntityRef)e);
				m_culling_system->add((EntityRef)e, (u8)RenderableTypes::DECAL, pos, radius);
				e = m_decals[(EntityRef)e].next_decal;
			}
			return;
		}
		
		if (old_state == Resource::State::READY) {
			auto map_iter = m_material_decal_map.find(&material);
			EntityPtr e = map_iter.value();
			while(e.isValid()) {
				m_culling_system->remove((EntityRef)e);
				e = m_decals[(EntityRef)e].next_decal;
			}
		}
	}


	void modelStateChanged(Resource::State old_state, Resource::State new_state, Resource& resource)
	{
		Model* model = static_cast<Model*>(&resource);
		if (new_state == Resource::State::READY)
		{
			modelLoaded(model);
		}
		else if (old_state == Resource::State::READY)
		{
			modelUnloaded(model);
		}
	}


	void clear() override
	{
		auto& rm = m_engine.getResourceManager();
		auto* material_manager = rm.get(Material::TYPE);

		for (Decal& decal : m_decals)
		{
			if (decal.material) decal.material->decRefCount();
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

		for (ModelInstance& i : m_model_instances)
		{
			if (i.flags.isSet(ModelInstance::VALID) && i.model)
			{
				i.model->decRefCount();
				if (i.custom_material) i.custom_material->decRefCount();
				i.custom_material = nullptr;
				LUMIX_DELETE(m_allocator, i.pose);
				i.pose = nullptr;
			}
		}
		m_model_instances.clear();
		for(auto iter = m_model_entity_map.begin(), end = m_model_entity_map.end(); iter != end; ++iter) {
			Model* model = iter.key();
			model->getObserverCb().unbind<&RenderSceneImpl::modelStateChanged>(this);
		}
		m_model_entity_map.clear();

		for(auto iter = m_material_decal_map.begin(), end = m_material_decal_map.end(); iter != end; ++iter) {
			Material* mat = iter.key();
			mat->getObserverCb().unbind<&RenderSceneImpl::decalMaterialStateChanged>(this);
		}
		m_material_decal_map.clear();

		m_culling_system->clear();

		m_reflection_probes.clear();
		m_environment_probes.clear();
	}


	Universe& getUniverse() override { return m_universe; }


	IPlugin& getPlugin() const override { return m_renderer; }


	void getRay(EntityRef camera_entity,
		const Vec2& screen_pos,
		DVec3& origin,
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

		const Matrix projection_matrix = getCameraProjection(camera_entity);
		const Transform view = m_universe.getTransform(camera_entity);

		if (camera.is_ortho) {
			const float ratio = camera.screen_height > 0 ? camera.screen_width / camera.screen_height : 1;
			origin += view.rot * Vec3(1, 0, 0) * nx * camera.ortho_size * ratio
				+ view.rot * Vec3(0, 1, 0) * ny * camera.ortho_size;
		}

		const Matrix inv_projection = projection_matrix.inverted();

		Vec4 p0 = inv_projection * Vec4(nx, ny, -1, 1);
		Vec4 p1 = inv_projection * Vec4(nx, ny, 1, 1);
		p0 *= 1 / p0.w;
		p1 *= 1 / p1.w;
		dir = normalize((p1 - p0).xyz());
		dir = view.rot * dir;
	}

	
	EntityPtr getActiveCamera() const override
	{
		return m_active_camera;
	}


	Viewport getCameraViewport(EntityRef entity) const override
	{
		Viewport vp;
		const Camera& cam = m_cameras[entity];
		vp.far = cam.far;
		vp.near = cam.near;
		vp.is_ortho = cam.is_ortho;
		vp.h = (int)cam.screen_height;
		vp.w = (int)cam.screen_width;
		if(vp.is_ortho) {
			vp.ortho_size = cam.ortho_size;
		}
		else {
			vp.fov = cam.fov;
		}
		vp.pos = m_universe.getPosition(entity);
		vp.rot = m_universe.getRotation(entity);
		return vp;
	}


	float getCameraLODMultiplier(float fov, bool is_ortho) const override
	{
		if (is_ortho) return 1;

		const float lod_multiplier = fov / degreesToRadians(60);
		return lod_multiplier  * lod_multiplier;
	}


	float getCameraLODMultiplier(EntityRef entity) const override
	{
		const Camera& camera = m_cameras[entity];
		return getCameraLODMultiplier(camera.fov, camera.is_ortho);
	}


	ShiftedFrustum getCameraFrustum(EntityRef entity) const override
	{
		ShiftedFrustum ret;
		const Camera& camera = m_cameras[entity];
		const Transform tr = m_universe.getTransform(entity);
		float ratio = camera.screen_height > 0 ? camera.screen_width / camera.screen_height : 1;
		if (camera.is_ortho) {
			ret.computeOrtho(tr.pos,
				tr.rot * Vec3(0, 0, 1),
				tr.rot * Vec3(0, 1, 0),
				camera.ortho_size * ratio,
				camera.ortho_size,
				camera.near,
				camera.far);
			return ret;
		}

		ret.computePerspective(tr.pos,
			tr.rot * Vec3(0, 0, -1),
			tr.rot * Vec3(0, 1, 0),
			camera.fov,
			ratio,
			camera.near,
			camera.far);
		return ret;
	}


	ShiftedFrustum getCameraFrustum(EntityRef entity, const Vec2& viewport_min_px, const Vec2& viewport_max_px) const override
	{
		ShiftedFrustum ret;
		const Camera& camera = m_cameras[entity];
		const Transform tr = m_universe.getTransform(entity);
		float ratio = camera.screen_height > 0 ? camera.screen_width / camera.screen_height : 1;
		Vec2 viewport_min = { viewport_min_px.x / camera.screen_width * 2 - 1, (1 - viewport_max_px.y / camera.screen_height) * 2 - 1 };
		Vec2 viewport_max = { viewport_max_px.x / camera.screen_width * 2 - 1, (1 - viewport_min_px.y / camera.screen_height) * 2 - 1 };
		if (camera.is_ortho) {
			ret.computeOrtho(tr.pos,
				tr.rot * Vec3(0, 0, 1),
				tr.rot * Vec3(0, 1, 0),
				camera.ortho_size * ratio,
				camera.ortho_size,
				camera.near,
				camera.far,
				viewport_min,
				viewport_max);
			return ret;
		}

		ret.computePerspective(tr.pos,
			tr.rot * Vec3(0, 0, -1),
			tr.rot * Vec3(0, 1, 0),
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
		const EntityPtr model_instance_ptr = bone_attachment.parent_entity;
		if (!model_instance_ptr.isValid()) return;

		const EntityRef model_instance = (EntityRef)model_instance_ptr;
		if (!m_universe.hasComponent(model_instance, MODEL_INSTANCE_TYPE)) return;
		const Pose* parent_pose = lockPose(model_instance);
		if (!parent_pose) return;

		Transform parent_entity_transform = m_universe.getTransform((EntityRef)bone_attachment.parent_entity);
		int idx = bone_attachment.bone_index;
		if (idx < 0 || idx >= (int)parent_pose->count) {
			unlockPose(model_instance, false);
			return;
		}
		float original_scale = m_universe.getScale(bone_attachment.entity);
		const LocalRigidTransform bone_transform = {parent_pose->positions[idx], parent_pose->rotations[idx] };
		const LocalRigidTransform relative_transform = { bone_attachment.relative_transform.pos, bone_attachment.relative_transform.rot };
		Transform result = parent_entity_transform * bone_transform * relative_transform;
		result.scale = original_scale;
		m_universe.setTransform(bone_attachment.entity, result);
		unlockPose(model_instance, false);
	}


	EntityPtr getBoneAttachmentParent(EntityRef entity) override
	{
		return m_bone_attachments[entity].parent_entity;
	}


	void updateRelativeMatrix(BoneAttachment& attachment)
	{
		if (!attachment.parent_entity.isValid()) return;
		if (attachment.bone_index < 0) return;
		const EntityPtr model_instance_ptr = attachment.parent_entity;
		if (!model_instance_ptr.isValid()) return;
		const EntityRef model_instance = (EntityRef)model_instance_ptr;
		if (!m_universe.hasComponent(model_instance, MODEL_INSTANCE_TYPE)) return;
		const Pose* pose = lockPose(model_instance);
		if (!pose) return;

		ASSERT(pose->is_absolute);
		if (attachment.bone_index >= (int)pose->count) {
			unlockPose(model_instance, false);
			return;
		}
		const LocalRigidTransform bone_transform = {pose->positions[attachment.bone_index], pose->rotations[attachment.bone_index]};

		const EntityRef parent = (EntityRef)attachment.parent_entity;
		Transform inv_parent_transform = m_universe.getTransform(parent) * bone_transform;
		inv_parent_transform = inv_parent_transform.inverted();
		const Transform child_transform = m_universe.getTransform(attachment.entity);
		const Transform res = inv_parent_transform * child_transform;
		attachment.relative_transform = {Vec3(res.pos), res.rot};
		unlockPose(model_instance, false);
	}


	Vec3 getBoneAttachmentPosition(EntityRef entity) override
	{
		return m_bone_attachments[entity].relative_transform.pos;
		
	}


	void setBoneAttachmentPosition(EntityRef entity, const Vec3& pos) override
	{
		BoneAttachment& attachment = m_bone_attachments[entity];
		attachment.relative_transform.pos = pos;
		m_is_updating_attachments = true;
		updateBoneAttachment(attachment);
		m_is_updating_attachments = false;
	}


	Vec3 getBoneAttachmentRotation(EntityRef entity) override
	{
		return m_bone_attachments[entity].relative_transform.rot.toEuler();
	}


	void setBoneAttachmentRotation(EntityRef entity, const Vec3& rot) override
	{
		BoneAttachment& attachment = m_bone_attachments[entity];
		Vec3 euler = rot;
		euler.x = clamp(euler.x, -PI * 0.5f, PI * 0.5f);
		attachment.relative_transform.rot.fromEuler(euler);
		m_is_updating_attachments = true;
		updateBoneAttachment(attachment);
		m_is_updating_attachments = false;
	}


	void setBoneAttachmentRotationQuat(EntityRef entity, const Quat& rot) override
	{
		BoneAttachment& attachment = m_bone_attachments[entity];
		attachment.relative_transform.rot = rot;
		m_is_updating_attachments = true;
		updateBoneAttachment(attachment);
		m_is_updating_attachments = false;
	}


	int getBoneAttachmentBone(EntityRef entity) override
	{
		return m_bone_attachments[entity].bone_index;
	}


	void setBoneAttachmentBone(EntityRef entity, int value) override
	{
		BoneAttachment& ba = m_bone_attachments[entity];
		ba.bone_index = value;
		updateRelativeMatrix(ba);
	}


	void setBoneAttachmentParent(EntityRef entity, EntityPtr parent) override
	{
		BoneAttachment& ba = m_bone_attachments[entity];
		ba.parent_entity = parent;
		if (parent.isValid() && parent.index < m_model_instances.size())
		{
			ModelInstance& mi = m_model_instances[parent.index];
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

		if (m_is_game_running && !paused)
		{
			for (auto* emitter : m_particle_emitters)
			{
				emitter->update(dt);
			}
		}
	}


	int getVersion() const override { return (int)RenderSceneVersion::LATEST; }


	void serializeBoneAttachments(OutputMemoryStream& serializer)
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

	void serializeCameras(OutputMemoryStream& serializer)
	{
		serializer.write((i32)m_cameras.size());
		for (Camera& camera : m_cameras)
		{
			serializer.write(camera);
		}
	}

	void serializeLights(OutputMemoryStream& serializer)
	{
		serializer.write((i32)m_point_lights.size());
		for (const PointLight& pl : m_point_lights) {
			serializer.write(pl);
		}

		serializer.write((i32)m_environments.size());
		for (const Environment& light : m_environments)
		{
			serializer.write(light);
		}
		serializer.write(m_active_global_light_entity);
	}

	void serializeModelInstances(OutputMemoryStream& serializer)
	{
		serializer.write((i32)m_model_instances.size());
		for (auto& r : m_model_instances)
		{
			serializer.write(r.flags.base);
			if(r.flags.isSet(ModelInstance::VALID)) {
				serializer.writeString(r.model ? r.model->getPath().c_str() : "");
				serializer.writeString(r.custom_material ? r.custom_material->getPath().c_str() : "");
			}
		}
	}

	void serializeTerrains(OutputMemoryStream& serializer)
	{
		serializer.write((i32)m_terrains.size());
		for (auto* terrain : m_terrains)
		{
			serializer.write(terrain->getEntity());
			terrain->serialize(serializer);
		}
	}

	void serializeFurs(OutputMemoryStream& serializer) {
		serializer.write(m_furs.size());
		for (auto iter = m_furs.begin(); iter.isValid(); ++iter) {
			serializer.write(iter.key());
			serializer.write(iter.value());
		}
	}

	void deserializeFurs(InputMemoryStream& serializer, const EntityMap& entity_map) {
		u32 count;
		serializer.read(count);
		m_furs.reserve(count + m_furs.size());
		for (u32 i = 0; i < count; ++i) {
			EntityRef e;
			serializer.read(e);
			e = entity_map.get(e);
			FurComponent fur;
			serializer.read(fur);
			m_furs.insert(e, fur);
			m_universe.onComponentCreated(e, FUR_TYPE, this);
		}
	}

	void deserializeDecals(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version)
	{
		u32 count;
		serializer.read(count);
		m_decals.reserve(count + m_decals.size());
		for (u32 i = 0; i < count; ++i) {
			Decal decal;
			serializer.read(decal.entity);
			decal.entity = entity_map.get(decal.entity);
			serializer.read(decal.half_extents);
			decal.uv_scale = Vec2(1);
			if (version > (i32)RenderSceneVersion::DECAL_UV_SCALE) {
				serializer.read(decal.uv_scale);
			}
			const char* tmp = serializer.readString();
			updateDecalInfo(decal);
			m_decals.insert(decal.entity, decal);
			setDecalMaterialPath(decal.entity, Path(tmp));
			m_universe.onComponentCreated(decal.entity, DECAL_TYPE, this);
		}
	}


	void serializeDecals(OutputMemoryStream& serializer)
	{
		serializer.write(m_decals.size());
		for (auto& decal : m_decals)
		{
			serializer.write(decal.entity);
			serializer.write(decal.half_extents);
			serializer.write(decal.uv_scale);
			serializer.writeString(decal.material ? decal.material->getPath().c_str() : "");
		}
	}

	void serializeEnvironmentProbes(OutputMemoryStream& serializer)
	{
		i32 count = m_environment_probes.size();
		serializer.write(count);
		for (int i = 0; i < count; ++i)
		{
			EntityRef entity = m_environment_probes.getKey(i);
			serializer.write(entity);
			const EnvironmentProbe& probe = m_environment_probes.at(i);
			serializer.write(probe);
		}
	}

	void serializeReflectionProbes(OutputMemoryStream& serializer)
	{
		i32 count = m_reflection_probes.size();
		serializer.write(count);
		for (int i = 0; i < count; ++i) {
			EntityRef entity = m_reflection_probes.getKey(i);
			serializer.write(entity);
			const ReflectionProbe& probe = m_reflection_probes.at(i);
			serializer.write(probe.guid);
			serializer.write(probe.flags.base);
			serializer.write(probe.size);
			serializer.write(probe.half_extents);
		}
	}

	void deserializeReflectionProbes(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		u32 count;
		serializer.read(count);
		m_reflection_probes.reserve(count + m_reflection_probes.size());
		ResourceManagerHub& manager = m_engine.getResourceManager();
		for (u32 i = 0; i < count; ++i) {
			EntityRef entity;
			serializer.read(entity);
			entity = entity_map.get(entity);
			ReflectionProbe& probe = m_reflection_probes.insert(entity);
			serializer.read(probe.guid);
			serializer.read(probe.flags.base);
			serializer.read(probe.size);
			serializer.read(probe.half_extents);
			load(probe);

			m_universe.onComponentCreated(entity, REFLECTION_PROBE_TYPE, this);
		}
	}

	void load(ReflectionProbe& probe) {
		if (probe.texture_id == 0xffFFffFF) {
			u32 mask = 0;
			for (auto& p : m_reflection_probes) {
				if (p.texture_id != 0xffFFffFF) mask |= 1 << p.texture_id;
			}

			for (u32 i = 0; i < 32; ++i) {
				if ((mask & (1 << i)) == 0) {
					probe.texture_id = i;
					break;
				}
			}
		}

		StaticString<LUMIX_MAX_PATH> path_str("universes/probes/", probe.guid, ".dds");
		if (probe.texture_id == 0xffFFffFF) {
			logError("There's not enough space for ", path_str);
			return;
		}
		
		struct Job : Renderer::RenderJob {
			Job(IAllocator& allocator) : data(allocator) {}

			void setup() override {}
				
			void execute() override {
				gpu::loadLayers(tex, layer, data.data(), (int)data.size(), "reflection probe");
			}

			u32 layer;
			OutputMemoryStream data;
			gpu::TextureHandle tex;
		};
			
		Job& job = m_renderer.createJob<Job>(m_allocator);
		// TODO async load
		if (!m_engine.getFileSystem().getContentSync(Path(path_str), Ref(job.data))) {
			logError("Could not load ", path_str);
		}
		job.layer = probe.texture_id;
		job.tex = m_reflection_probes_texture;
		m_renderer.queue(job, 0);
	}

	void deserializeEnvironmentProbes(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		u32 count;
		serializer.read(count);
		m_environment_probes.reserve(count + m_environment_probes.size());
		ResourceManagerHub& manager = m_engine.getResourceManager();
		StaticString<LUMIX_MAX_PATH> probe_dir("universes/probes/");
		for (u32 i = 0; i < count; ++i) {
			EntityRef entity;
			serializer.read(entity);
			entity = entity_map.get(entity);
			EnvironmentProbe& probe = m_environment_probes.insert(entity);
			serializer.read(probe);

			m_universe.onComponentCreated(entity, ENVIRONMENT_PROBE_TYPE, this);
		}
	}


	void deserializeBoneAttachments(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		u32 count;
		serializer.read(count);
		m_bone_attachments.reserve(count + m_bone_attachments.size());
		for (u32 i = 0; i < count; ++i) {
			BoneAttachment bone_attachment;
			serializer.read(bone_attachment.bone_index);
			serializer.read(bone_attachment.entity);
			bone_attachment.entity = entity_map.get(bone_attachment.entity);
			serializer.read(bone_attachment.parent_entity);
			bone_attachment.parent_entity = entity_map.get(bone_attachment.parent_entity);
			serializer.read(bone_attachment.relative_transform);
			m_bone_attachments.insert(bone_attachment.entity, bone_attachment);
			m_universe.onComponentCreated(bone_attachment.entity, BONE_ATTACHMENT_TYPE, this);
		}
	}


	void deserializeParticleEmitters(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		const u32 count = serializer.read<u32>();
		m_particle_emitters.reserve(count + m_particle_emitters.size());
		for (u32 i = 0; i < count; ++i) {
			ParticleEmitter* emitter = LUMIX_NEW(m_allocator, ParticleEmitter)(INVALID_ENTITY, m_allocator);
			emitter->deserialize(serializer, m_engine.getResourceManager());
			emitter->m_entity = entity_map.get(emitter->m_entity);
			if(emitter->m_entity.isValid()) {
				m_particle_emitters.insert((EntityRef)emitter->m_entity, emitter);
				m_universe.onComponentCreated((EntityRef)emitter->m_entity, PARTICLE_EMITTER_TYPE, this);
			}
			else {
				LUMIX_DELETE(m_allocator, emitter);
			}
		}
	}


	void serializeParticleEmitters(OutputMemoryStream& serializer)
	{
		serializer.write(m_particle_emitters.size());
		for (auto* emitter : m_particle_emitters)
		{
			emitter->serialize(serializer);
		}
	}


	void serialize(OutputMemoryStream& serializer) override
	{
		serializeCameras(serializer);
		serializeModelInstances(serializer);
		serializeLights(serializer);
		serializeTerrains(serializer);
		serializeParticleEmitters(serializer);
		serializeBoneAttachments(serializer);
		serializeEnvironmentProbes(serializer);
		serializeReflectionProbes(serializer);
		serializeDecals(serializer);
		serializeFurs(serializer);
	}


	void deserializeCameras(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		u32 size;
		serializer.read(size);
		m_cameras.reserve(size + m_cameras.size());
		for (u32 i = 0; i < size; ++i)
		{
			Camera camera;
			serializer.read(camera);
			camera.entity = entity_map.get(camera.entity);

			m_cameras.insert(camera.entity, camera);
			m_universe.onComponentCreated(camera.entity, CAMERA_TYPE, this);
			if (!m_active_camera.isValid()) m_active_camera = camera.entity;
		}
	}

	void deserializeModelInstances(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		u32 size = 0;
		serializer.read(size);
		m_model_instances.reserve(nextPow2(size + m_model_instances.size()));
		m_mesh_sort_data.reserve(nextPow2(size + m_mesh_sort_data.size()));
		for (u32 i = 0; i < size; ++i) {
			FlagSet<ModelInstance::Flags, u8> flags;
			serializer.read(flags);

			if(flags.isSet(ModelInstance::VALID)) {
				const EntityRef e = entity_map.get(EntityRef{(i32)i});

				while (e.index >= m_model_instances.size()) {
					auto& r = m_model_instances.emplace();
					r.flags.clear();
					r.flags.set(ModelInstance::VALID, false);
					r.model = nullptr;
					r.pose = nullptr;
				}

				ModelInstance& r = m_model_instances[e.index];
				r.flags = flags;
				r.model = nullptr;
				r.pose = nullptr;
				r.meshes = nullptr;
				r.mesh_count = 0;

				const char* path = serializer.readString();
				if (path[0] != 0) {
					Model* model = m_engine.getResourceManager().load<Model>(Path(path));
					setModel(e, model);
				}

				const char* mat_path = serializer.readString();
				if (mat_path[0] != 0) {
					setModelInstanceMaterialOverride(e, Path(mat_path));
				}

				m_universe.onComponentCreated(e, MODEL_INSTANCE_TYPE, this);
			}
		}
	}

	void deserializeLights(IInputStream& serializer, const EntityMap& entity_map)
	{
		u32 size = 0;
		serializer.read(size);
		m_point_lights.reserve(size + m_point_lights.size());
		for (u32 i = 0; i < size; ++i) {
			PointLight light;
			serializer.read(light);
			light.entity = entity_map.get(light.entity);
			m_point_lights.insert(light.entity, light);
			const DVec3 pos = m_universe.getPosition(light.entity);
			m_culling_system->add(light.entity, (u8)RenderableTypes::LOCAL_LIGHT, pos, light.range);
			m_universe.onComponentCreated(light.entity, POINT_LIGHT_TYPE, this);
		}

		serializer.read(size);
		for (u32 i = 0; i < size; ++i) {
			Environment light;
			serializer.read(light);
			light.entity = entity_map.get(light.entity);
			m_environments.insert(light.entity, light);
			m_universe.onComponentCreated(light.entity, ENVIRONMENT_TYPE, this);
		}
		
		EntityPtr tmp;
		serializer.read(tmp);
		if (!m_active_global_light_entity.isValid()) {
			m_active_global_light_entity = entity_map.get(tmp);
		}
	}

	void deserializeTerrains(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		i32 size = 0;
		serializer.read(size);
		for (int i = 0; i < size; ++i)
		{
			EntityRef entity;
			serializer.read(entity);
			entity = entity_map.get(entity);
			auto* terrain = LUMIX_NEW(m_allocator, Terrain)(m_renderer, entity, *this, m_allocator);
			terrain->deserialize(entity, serializer, m_universe, *this);
			m_terrains.insert(entity, terrain);
		}
	}


	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override
	{
		deserializeCameras(serializer, entity_map);
		deserializeModelInstances(serializer, entity_map);
		deserializeLights(serializer, entity_map);
		deserializeTerrains(serializer, entity_map);
		deserializeParticleEmitters(serializer, entity_map);
		deserializeBoneAttachments(serializer, entity_map);
		deserializeEnvironmentProbes(serializer, entity_map);
		deserializeReflectionProbes(serializer, entity_map);
		deserializeDecals(serializer, entity_map, version);
		deserializeFurs(serializer, entity_map);
	}


	void destroyBoneAttachment(EntityRef entity)
	{
		const BoneAttachment& bone_attachment = m_bone_attachments[entity];
		const EntityPtr parent_entity = bone_attachment.parent_entity;
		if (parent_entity.isValid() && parent_entity.index < m_model_instances.size())
		{
			ModelInstance& mi = m_model_instances[bone_attachment.parent_entity.index];
			mi.flags.unset(ModelInstance::IS_BONE_ATTACHMENT_PARENT);
		}
		m_bone_attachments.erase(entity);
		m_universe.onComponentDestroyed(entity, BONE_ATTACHMENT_TYPE, this);
	}
	
	void destroyReflectionProbe(EntityRef entity)
	{
		ReflectionProbe& probe = m_reflection_probes[entity];
		m_reflection_probes.erase(entity);
		m_universe.onComponentDestroyed(entity, REFLECTION_PROBE_TYPE, this);
	}

	void destroyEnvironmentProbe(EntityRef entity)
	{
		m_environment_probes.erase(entity);
		m_universe.onComponentDestroyed(entity, ENVIRONMENT_PROBE_TYPE, this);
	}


	void destroyModelInstance(EntityRef entity)
	{
		setModel(entity, nullptr);
		auto& model_instance = m_model_instances[entity.index];
		LUMIX_DELETE(m_allocator, model_instance.pose);
		model_instance.pose = nullptr;
		model_instance.flags.clear();
		model_instance.flags.set(ModelInstance::VALID, false);
		if (model_instance.custom_material) model_instance.custom_material->decRefCount();
		model_instance.custom_material = nullptr;
		m_universe.onComponentDestroyed(entity, MODEL_INSTANCE_TYPE, this);
	}

	void destroyEnvironment(EntityRef entity)
	{
		m_universe.onComponentDestroyed(entity, ENVIRONMENT_TYPE, this);

		if ((EntityPtr)entity == m_active_global_light_entity)
		{
			m_active_global_light_entity = INVALID_ENTITY;
		}
		m_environments.erase(entity);
	}

	void destroyFur(EntityRef entity) {
		m_furs.erase(entity);
		m_universe.onComponentDestroyed(entity, FUR_TYPE, this);
	}

	void destroyDecal(EntityRef entity)
	{
		m_culling_system->remove(entity);
		m_decals.erase(entity);
		m_universe.onComponentDestroyed(entity, DECAL_TYPE, this);
	}


	void destroyPointLight(EntityRef entity)
	{
		m_point_lights.erase(entity);
		m_culling_system->remove(entity);
		m_universe.onComponentDestroyed(entity, POINT_LIGHT_TYPE, this);
	}


	void destroyCamera(EntityRef entity)
	{
		m_cameras.erase(entity);
		m_universe.onComponentDestroyed(entity, CAMERA_TYPE, this);
		if (m_active_camera == entity) m_active_camera = INVALID_ENTITY;
	}


	void destroyTerrain(EntityRef entity)
	{
		LUMIX_DELETE(m_allocator, m_terrains[entity]);
		m_terrains.erase(entity);
		m_universe.onComponentDestroyed(entity, TERRAIN_TYPE, this);
	}


	void destroyParticleEmitter(EntityRef entity)
	{
		auto* emitter = m_particle_emitters[entity];
		m_universe.onComponentDestroyed((EntityRef)emitter->m_entity, PARTICLE_EMITTER_TYPE, this);
		m_particle_emitters.erase((EntityRef)emitter->m_entity);
		LUMIX_DELETE(m_allocator, emitter);
	}


	void createFur(EntityRef entity) {
		m_furs.insert(entity, {});
		m_universe.onComponentCreated(entity, FUR_TYPE, this);
	}


	void createCamera(EntityRef entity)
	{
		Camera camera;
		camera.is_ortho = false;
		camera.ortho_size = 10;
		camera.entity = entity;
		camera.fov = degreesToRadians(60);
		camera.screen_width = 800;
		camera.screen_height = 600;
		camera.near = 0.1f;
		camera.far = 10000.0f;
		m_cameras.insert(entity, camera);
		m_universe.onComponentCreated(entity, CAMERA_TYPE, this);

		if (!m_active_camera.isValid()) m_active_camera = entity;
	}


	void createTerrain(EntityRef entity)
	{
		Terrain* terrain = LUMIX_NEW(m_allocator, Terrain)(m_renderer, entity, *this, m_allocator);
		m_terrains.insert(entity, terrain);
		m_universe.onComponentCreated(entity, TERRAIN_TYPE, this);
	}


	void createParticleEmitter(EntityRef entity)
	{
		m_particle_emitters.insert(entity, LUMIX_NEW(m_allocator, ParticleEmitter)(entity, m_allocator));
		m_universe.onComponentCreated(entity, PARTICLE_EMITTER_TYPE, this);
	}

	bool getEnvironmentCastShadows(EntityRef entity) override {
		return m_environments[entity].flags.isSet(Environment::CAST_SHADOWS);
	}
	
	void setEnvironmentCastShadows(EntityRef entity, bool enable) override {
		m_environments[entity].flags.set(Environment::CAST_SHADOWS, enable);
	}

	Environment& getEnvironment(EntityRef entity) override
	{
		return m_environments[entity];
	}


	const HashMap<EntityRef, PointLight>& getPointLights() override
	{
		return m_point_lights;
	}

	PointLight& getPointLight(EntityRef entity) override
	{
		return m_point_lights[entity];
	}
	
	
	const MeshSortData* getMeshSortData() const override
	{
		return m_mesh_sort_data.empty() ? nullptr : m_mesh_sort_data.begin();
	}


	Span<const ModelInstance> getModelInstances() const override
	{
		return m_model_instances;
	}


	Span<ModelInstance> getModelInstances() override
	{
		return m_model_instances;
	}


	ModelInstance* getModelInstance(EntityRef entity) override
	{
		return &m_model_instances[entity.index];
	}


	Vec3 getPoseBonePosition(EntityRef model_instance, int bone_index)
	{
		Pose* pose = m_model_instances[model_instance.index].pose;
		return pose->positions[bone_index];
	}


	void onEntityDestroyed(EntityRef entity)
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


	void onEntityMoved(EntityRef entity)
	{
		const u64 cmp_mask = m_universe.getComponentsMask(entity);
		if ((cmp_mask & m_render_cmps_mask) == 0) {
			return;
		}

		if (m_culling_system->isAdded(entity)) {
			if (m_universe.hasComponent(entity, MODEL_INSTANCE_TYPE)) {
				const Transform& tr = m_universe.getTransform(entity);
				const Model* model = m_model_instances[entity.index].model;
				ASSERT(model);
				const float bounding_radius = model->getOriginBoundingRadius();
				m_culling_system->set(entity, tr.pos, bounding_radius * tr.scale);
			}
			else if (m_universe.hasComponent(entity, DECAL_TYPE)) {
				auto iter = m_decals.find(entity);
				updateDecalInfo(iter.value());
				const DVec3 position = m_universe.getPosition(entity);
				m_culling_system->setPosition(entity, position);
			}
			else if (m_universe.hasComponent(entity, POINT_LIGHT_TYPE)) {
				const DVec3 pos = m_universe.getPosition(entity);
				m_culling_system->setPosition(entity, pos);
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
		
		if(m_universe.hasComponent(entity, BONE_ATTACHMENT_TYPE)) {
			for (auto& attachment : m_bone_attachments)
			{
				if (attachment.entity == entity)
				{
					updateRelativeMatrix(attachment);
					break;
				}
			}
		}
	}


	Engine& getEngine() const override { return m_engine; }

	const HashMap<EntityRef, Terrain*>& getTerrains() override {
		return m_terrains;
	}

	Terrain* getTerrain(EntityRef entity) override
	{
		return m_terrains[entity];
	}


	IVec2 getTerrainResolution(EntityRef entity) override
	{
		auto* terrain = m_terrains[entity];
		return IVec2(terrain->getWidth(), terrain->getHeight());
	}


	EntityPtr getFirstTerrain() override
	{
		if (m_terrains.empty()) return INVALID_ENTITY;
		auto iter = m_terrains.begin();
		return iter.value()->getEntity();
	}


	EntityPtr getNextTerrain(EntityRef entity) override
	{
		auto iter = m_terrains.find(entity);
		++iter;
		if (!iter.isValid()) return INVALID_ENTITY;
		return iter.value()->getEntity();
	}


	Vec3 getTerrainNormalAt(EntityRef entity, float x, float z) override
	{
		return m_terrains[entity]->getNormal(x, z);
	}


	float getTerrainHeightAt(EntityRef entity, float x, float z) override
	{
		return m_terrains[entity]->getHeight(x, z);
	}


	AABB getTerrainAABB(EntityRef entity) override
	{
		return m_terrains[entity]->getAABB();
	}


	Vec2 getTerrainSize(EntityRef entity) override
	{
		return m_terrains[entity]->getSize();
	}


	void setTerrainMaterialPath(EntityRef entity, const Path& path) override
	{
		if (path.isValid())
		{
			Material* material = m_engine.getResourceManager().load<Material>(path);
			m_terrains[entity]->setMaterial(material);
		}
		else
		{
			m_terrains[entity]->setMaterial(nullptr);
		}
	}


	Material* getTerrainMaterial(EntityRef entity) override { return m_terrains[entity]->getMaterial(); }


	void setDecalHalfExtents(EntityRef entity, const Vec3& value) override
	{
		Decal& decal = m_decals[entity];
		decal.half_extents = value;
		if (decal.material && decal.material->isReady()) {
			m_culling_system->setRadius(entity, length(value));
		}
		updateDecalInfo(decal);
	}


	Vec3 getDecalHalfExtents(EntityRef entity) override
	{
		return m_decals[entity].half_extents;
	}

	void setDecalUVScale(EntityRef entity, const Vec2& value) override {
		m_decals[entity].uv_scale = value;
	}

	Vec2 getDecalUVScale(EntityRef entity) override {
		return m_decals[entity].uv_scale;
	}

	void setDecalMaterialPath(EntityRef entity, const Path& path) override
	{
		Decal& decal = m_decals[entity];
		if (decal.material) {
			removeFromMaterialDecalMap(decal.material, entity);
			decal.material->decRefCount();
		}

		if (path.isValid()) {
			decal.material = m_engine.getResourceManager().load<Material>(path);
			addToMaterialDecalMap(decal.material, entity);

			if (decal.material->isReady()) {
				const float radius = length(m_decals[entity].half_extents);
				const DVec3 pos = m_universe.getPosition(entity);
				m_culling_system->add(entity, (u8)RenderableTypes::DECAL, pos, radius);
			}
		}
		else {
			decal.material = nullptr;
		}
	}

	Material* getDecalMaterial(EntityRef entity) const override
	{
		auto iter = m_decals.find(entity);
		return iter.value().material;
	}

	Path getDecalMaterialPath(EntityRef entity) override
	{
		Decal& decal = m_decals[entity];
		return decal.material ? decal.material->getPath() : Path("");
	}


	Path getTerrainMaterialPath(EntityRef entity) override
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
	

	void setTerrainXZScale(EntityRef entity, float scale) override
	{
		m_terrains[entity]->setXZScale(scale);
	}


	float getTerrainXZScale(EntityRef entity) override { return m_terrains[entity]->getXZScale(); }


	void setTerrainYScale(EntityRef entity, float scale) override
	{
		m_terrains[entity]->setYScale(scale);
	}


	float getTerrainYScale(EntityRef entity) override { return m_terrains[entity]->getYScale(); }


	Pose* lockPose(EntityRef entity) override { return m_model_instances[entity.index].pose; }
	void unlockPose(EntityRef entity, bool changed) override
	{
		if (!changed) return;
		if (entity.index < m_model_instances.size()
			&& (m_model_instances[entity.index].flags.isSet(ModelInstance::IS_BONE_ATTACHMENT_PARENT)) == 0)
		{
			return;
		}

		EntityRef parent = entity;
		for (BoneAttachment& ba : m_bone_attachments)
		{
			if (ba.parent_entity != parent) continue;
			m_is_updating_attachments = true;
			updateBoneAttachment(ba);
			m_is_updating_attachments = false;
		}
	}


	Model* getModelInstanceModel(EntityRef entity) override { return m_model_instances[entity.index].model; }


	bool isModelInstanceEnabled(EntityRef entity) override
	{
		ModelInstance& model_instance = m_model_instances[entity.index];
		return model_instance.flags.isSet(ModelInstance::ENABLED);
	}


	void enableModelInstance(EntityRef entity, bool enable) override
	{
		ModelInstance& model_instance = m_model_instances[entity.index];
		model_instance.flags.set(ModelInstance::ENABLED, enable);
		if (enable)
		{
			if (!model_instance.model || !model_instance.model->isReady()) return;

			const DVec3 pos = m_universe.getPosition(entity);
			const float radius = model_instance.model->getOriginBoundingRadius() * m_universe.getScale(entity);
			if (!m_culling_system->isAdded(entity)) {
				const RenderableTypes type = getRenderableType(*model_instance.model, model_instance.custom_material);
				m_culling_system->add(entity, (u8)type, pos, radius);
			}
		}
		else
		{
			m_culling_system->remove(entity);
		}
	}

	void setModelInstanceMaterialOverride(EntityRef entity, const Path& path) override {
		ModelInstance& mi = m_model_instances[entity.index];
		if (mi.custom_material) {
			if (mi.custom_material->getPath() == path) return;
			
			mi.custom_material->decRefCount();
			mi.custom_material = nullptr;
		}

		if (mi.mesh_count > 0 && mi.meshes[0].material->getPath() == path) return;

		if (path.isValid()) {
			Material* material = m_engine.getResourceManager().load<Material>(path);
			mi.custom_material = material;
		}

		if (!mi.model || !mi.model->isReady()) return;

		if (m_culling_system->isAdded(entity)) {
			m_culling_system->remove(entity);
		}
		const RenderableTypes type = getRenderableType(*mi.model, mi.custom_material);
		const DVec3 pos = m_universe.getPosition(entity);
		const float radius = mi.model->getOriginBoundingRadius() * m_universe.getScale(entity);
		m_culling_system->add(entity, (u8)type, pos, radius);
	}

	Path getModelInstanceMaterialOverride(EntityRef entity) override {
		return m_model_instances[entity.index].custom_material ? m_model_instances[entity.index].custom_material->getPath() : Path("");
	}

	Path getModelInstancePath(EntityRef entity) override
	{
		return m_model_instances[entity.index].model ? m_model_instances[entity.index].model->getPath() : Path("");
	}

	void setModelInstanceLOD(EntityRef entity, u32 lod) override {
		m_model_instances[entity.index].lod = float(lod);
	}

	void setModelInstancePath(EntityRef entity, const Path& path) override
	{
		if (path.isValid()) {
			Model* model = m_engine.getResourceManager().load<Model>(path);
			setModel(entity, model);
		}
		else {
			setModel(entity, nullptr);
		}
	}


	void getTerrainInfos(Array<TerrainInfo>& infos) override
	{
		PROFILE_FUNCTION();
		infos.reserve(m_terrains.size());
		for (auto* terrain : m_terrains) {
			const TerrainInfo info = terrain->getInfo();
			if (info.terrain) infos.push(info);
		}
	}


	static int LUA_castCameraRay(lua_State* L)
	{
		auto* scene = LuaWrapper::checkArg<RenderSceneImpl*>(L, 1);
		EntityRef camera_entity = LuaWrapper::checkArg<EntityRef>(L, 2);
		float x, y;
		if (lua_gettop(L) > 3) {
			x = LuaWrapper::checkArg<float>(L, 3);
			y = LuaWrapper::checkArg<float>(L, 4);
		}
		else {
			x = scene->getCameraScreenWidth(camera_entity) * 0.5f;
			y = scene->getCameraScreenHeight(camera_entity) * 0.5f;
		}

		DVec3 origin;
		Vec3 dir;
		scene->getRay(camera_entity, {x, y}, origin, dir);

		RayCastModelHit hit = scene->castRay(origin, dir, INVALID_ENTITY);
		LuaWrapper::push(L, hit.is_hit);
		LuaWrapper::push(L, hit.is_hit ? hit.origin + hit.dir * hit.t : DVec3(0));

		return 2;
	}


	void setTerrainHeightAt(EntityRef entity, int x, int z, float height)
	{
		m_terrains[entity]->setHeight(x, z, height);
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


	static void LUA_makeScreenshot(RenderSceneImpl* scene, const char* path)
	{
		scene->m_renderer.makeScreenshot(Path(path));
	}


	int getGrassRotationMode(EntityRef entity, int index) override
	{
		return (int)m_terrains[entity]->getGrassTypeRotationMode(index);
	}


	void setGrassRotationMode(EntityRef entity, int index, int value) override
	{
		m_terrains[entity]->setGrassTypeRotationMode(index, (Terrain::GrassType::RotationMode)value);
	}


	float getGrassDistance(EntityRef entity, int index) override
	{
		return m_terrains[entity]->getGrassTypeDistance(index);
	}


	void setGrassDistance(EntityRef entity, int index, float value) override
	{
		m_terrains[entity]->setGrassTypeDistance(index, value);
	}


	void setGrassSpacing(EntityRef entity, int index, float spacing) override
	{
		m_terrains[entity]->setGrassTypeSpacing(index, spacing);
	}


	float getGrassSpacing(EntityRef entity, int index) override
	{
		return m_terrains[entity]->getGrassTypeSpacing(index);
	}


	void setGrassPath(EntityRef entity, int index, const Path& path) override
	{
		m_terrains[entity]->setGrassTypePath(index, path);
	}


	Path getGrassPath(EntityRef entity, int index) override
	{
		return m_terrains[entity]->getGrassTypePath(index);
	}


	int getGrassCount(EntityRef entity) override
	{
		return m_terrains[entity]->getGrassTypeCount();
	}


	void addGrass(EntityRef entity, int index) override
	{
		m_terrains[entity]->addGrassType(index);
	}


	void removeGrass(EntityRef entity, int index) override
	{
		m_terrains[entity]->removeGrassType(index);
	}


	EntityPtr getFirstModelInstance() override
	{
		return getNextModelInstance(INVALID_ENTITY);
	}


	EntityPtr getNextModelInstance(EntityPtr entity) override
	{
		for(int i = entity.index + 1; i < m_model_instances.size(); ++i)
		{
			if (m_model_instances[i].flags.isSet(ModelInstance::VALID)) return EntityPtr{i};
		}
		return INVALID_ENTITY;
	}


	float getCameraLODMultiplier(EntityRef camera)
	{
		float lod_multiplier;
		if (getCamera(camera).is_ortho)
		{
			lod_multiplier = 1;
		}
		else
		{
			lod_multiplier = getCamera(camera).fov / degreesToRadians(60);
			lod_multiplier *= lod_multiplier;
		}
		return lod_multiplier;
	}


	CullResult* getRenderables(const ShiftedFrustum& frustum, RenderableTypes type) const override
	{
		return m_culling_system->cull(frustum, static_cast<u8>(type));
	}


	CullResult* getRenderables(const ShiftedFrustum& frustum) const override
	{
		return m_culling_system->cull(frustum);
	}


	float getCameraScreenWidth(EntityRef camera) override { return m_cameras[camera].screen_width; }
	float getCameraScreenHeight(EntityRef camera) override { return m_cameras[camera].screen_height; }


	void setGlobalLODMultiplier(float multiplier) override { m_lod_multiplier = multiplier; }
	float getGlobalLODMultiplier() const override { return m_lod_multiplier; }

	Camera& getCamera(EntityRef entity) override { return m_cameras[entity]; }

	Matrix getCameraProjection(EntityRef entity) override
	{
		const Camera& camera = m_cameras[entity];
		Matrix mtx;
		const float ratio = camera.screen_height > 0 ? camera.screen_width / camera.screen_height : 1;
		if (camera.is_ortho) {
			mtx.setOrtho(-camera.ortho_size * ratio,
				camera.ortho_size * ratio,
				-camera.ortho_size,
				camera.ortho_size,
				camera.near,
				camera.far,
				true);
		}
		else {
			mtx.setPerspective(camera.fov, ratio, camera.near, camera.far, true);
		}
		return mtx;
	}


	void setCameraScreenSize(EntityRef camera, int w, int h) override
	{
		auto& cam = m_cameras[{camera.index}];
		cam.screen_width = (float)w;
		cam.screen_height = (float)h;
	}


	Vec2 getCameraScreenSize(EntityRef camera) override
	{
		auto& cam = m_cameras[{camera.index}];
		return Vec2(cam.screen_width, cam.screen_height);
	}

	FurComponent& getFur(EntityRef e) override {
		return m_furs[e];
	}

	HashMap<EntityRef, FurComponent>& getFurs() override {
		return m_furs;
	}

	void clearDebugLines() override { m_debug_lines.clear(); }
	void clearDebugTriangles() override { m_debug_triangles.clear(); }

	const Array<DebugTriangle>& getDebugTriangles() const override { return m_debug_triangles; }
	const Array<DebugLine>& getDebugLines() const override { return m_debug_lines; }


	void addDebugHalfSphere(const RigidTransform& transform, float radius, bool top, u32 color)
	{
		const DVec3 center = transform.pos;
		const Vec3 x_vec = transform.rot * Vec3(1, 0, 0);
		const Vec3 y_vec = transform.rot * Vec3(0, top ? 1.f : -1.f, 0);
		const Vec3 z_vec = transform.rot * Vec3(0, 0, 1);
		static const int COLS = 36;
		static const int ROWS = COLS >> 1;
		static const float STEP = degreesToRadians(360.0f) / COLS;
		for (int y = 0; y < ROWS >> 1; ++y)
		{
			float cy = cosf(y * STEP);
			float cy1 = cosf((y + 1) * STEP);
			float sy = sinf(y * STEP);
			float sy1 = sinf((y + 1) * STEP);
			float prev_ci = cosf(-STEP);
			float prev_si = sinf(-STEP);

			Vec3 y_offset = y_vec * sy;
			Vec3 y_offset1 = y_vec * sy1;

			for (int i = 0; i < COLS; ++i)
			{
				float ci = cosf(i * STEP);
				float si = sinf(i * STEP);

				addDebugLine(
					center + radius * (x_vec * ci * cy + z_vec * si * cy + y_offset),
					center + radius * (x_vec * prev_ci * cy + z_vec * prev_si * cy + y_offset),
					color);
				addDebugLine(
					center + radius * (x_vec * ci * cy + z_vec * si * cy + y_offset),
					center + radius * (x_vec * ci * cy1 + z_vec * si * cy1 + y_offset1),
					color);
				prev_ci = ci;
				prev_si = si;
			}
		}
	}


	void addDebugHalfSphere(const DVec3& center, float radius, bool top, u32 color)
	{
		static const int COLS = 36;
		static const int ROWS = COLS >> 1;
		static const float STEP = (PI / 180.0f) * 360.0f / COLS;
		int p2 = COLS >> 1;
		int yfrom = top ? 0 : -(ROWS >> 1);
		int yto = top ? ROWS >> 1 : 0;
		for (int y = yfrom; y < yto; ++y)
		{
			float cy = cosf(y * STEP);
			float cy1 = cosf((y + 1) * STEP);
			float sy = sinf(y * STEP);
			float sy1 = sinf((y + 1) * STEP);
			float prev_ci = cosf((-p2 - 1) * STEP);
			float prev_si = sinf((-p2 - 1) * STEP);

			for (int i = -p2; i < p2; ++i)
			{
				float ci = cosf(i * STEP);
				float si = sinf(i * STEP);
				addDebugLine(DVec3(center.x + radius * ci * cy,
					center.y + radius * sy,
					center.z + radius * si * cy),
					DVec3(center.x + radius * ci * cy1,
					center.y + radius * sy1,
					center.z + radius * si * cy1),
					color);
				addDebugLine(DVec3(center.x + radius * ci * cy,
					center.y + radius * sy,
					center.z + radius * si * cy),
					DVec3(center.x + radius * prev_ci * cy,
					center.y + radius * sy,
					center.z + radius * prev_si * cy),
					color);
				addDebugLine(DVec3(center.x + radius * prev_ci * cy1,
					center.y + radius * sy1,
					center.z + radius * prev_si * cy1),
					DVec3(center.x + radius * ci * cy1,
					center.y + radius * sy1,
					center.z + radius * si * cy1),
					color);
				prev_ci = ci;
				prev_si = si;
			}
		}
	}


	void addDebugTriangle(const DVec3& p0,
		const DVec3& p1,
		const DVec3& p2,
		u32 color) override
	{
		DebugTriangle& tri = m_debug_triangles.emplace();
		tri.p0 = p0;
		tri.p1 = p1;
		tri.p2 = p2;
		tri.color = ARGBToABGR(color);
	}


	void addDebugCube(const DVec3& pos,
		const Vec3& dir,
		const Vec3& up,
		const Vec3& right,
		u32 color) override
	{
		addDebugLine(pos + dir + up + right, pos + dir + up - right, color);
		addDebugLine(pos - dir + up + right, pos - dir + up - right, color);
		addDebugLine(pos + dir + up + right, pos - dir + up + right, color);
		addDebugLine(pos + dir + up - right, pos - dir + up - right, color);

		addDebugLine(pos + dir - up + right, pos + dir - up - right, color);
		addDebugLine(pos - dir - up + right, pos - dir - up - right, color);
		addDebugLine(pos + dir - up + right, pos - dir - up + right, color);
		addDebugLine(pos + dir - up - right, pos - dir - up - right, color);

		addDebugLine(pos + dir + up + right, pos + dir - up + right, color);
		addDebugLine(pos + dir + up - right, pos + dir - up - right, color);
		addDebugLine(pos - dir + up + right, pos - dir - up + right, color);
		addDebugLine(pos - dir + up - right, pos - dir - up - right, color);

	}


	void addDebugCubeSolid(const DVec3& min,
		const DVec3& max,
		u32 color) override
	{
		DVec3 a = min;
		DVec3 b = min;
		DVec3 c = max;

		b.x = max.x;
		c.z = min.z;
		addDebugTriangle(a, c, b, color);
		b.x = min.x;
		b.y = max.y;
		addDebugTriangle(a, b, c, color);

		b = max;
		c = max;
		a.z = max.z;
		b.y = min.y;
		addDebugTriangle(a, b, c, color);
		b.x = min.x;
		b.y = max.y;
		addDebugTriangle(a, c, b, color);

		a = min;
		b = min;
		c = max;

		b.x = max.x;
		c.y = min.y;
		addDebugTriangle(a, b, c, color);
		b.x = min.x;
		b.z = max.z;
		addDebugTriangle(a, c, b, color);

		b = max;
		c = max;
		a.y = max.y;
		b.z = min.z;
		addDebugTriangle(a, c, b, color);
		b.x = min.x;
		b.z = max.z;
		addDebugTriangle(a, b, c, color);

		a = min;
		b = min;
		c = max;

		b.y = max.y;
		c.x = min.x;
		addDebugTriangle(a, c, b, color);
		b.y = min.y;
		b.z = max.z;
		addDebugTriangle(a, b, c, color);

		b = max;
		c = max;
		a.x = max.x;
		b.z = min.z;
		addDebugTriangle(a, b, c, color);
		b.y = min.y;
		b.z = max.z;
		addDebugTriangle(a, c, b, color);
	}



	void addDebugCube(const DVec3& min,
		const DVec3& max,
		u32 color) override
	{
		DVec3 a = min;
		DVec3 b = min;
		b.x = max.x;
		addDebugLine(a, b, color);
		a = DVec3(b.x, b.y, max.z);
		addDebugLine(a, b, color);
		b = DVec3(min.x, a.y, a.z);
		addDebugLine(a, b, color);
		a = DVec3(b.x, b.y, min.z);
		addDebugLine(a, b, color);

		a = min;
		a.y = max.y;
		b = a;
		b.x = max.x;
		addDebugLine(a, b, color);
		a = DVec3(b.x, b.y, max.z);
		addDebugLine(a, b, color);
		b = DVec3(min.x, a.y, a.z);
		addDebugLine(a, b, color);
		a = DVec3(b.x, b.y, min.z);
		addDebugLine(a, b, color);

		a = min;
		b = a;
		b.y = max.y;
		addDebugLine(a, b, color);
		a.x = max.x;
		b.x = max.x;
		addDebugLine(a, b, color);
		a.z = max.z;
		b.z = max.z;
		addDebugLine(a, b, color);
		a.x = min.x;
		b.x = min.x;
		addDebugLine(a, b, color);
	}


	void addDebugCross(const DVec3& center, float size, u32 color) override
	{
		addDebugLine(center, DVec3(center.x - size, center.y, center.z), color);
		addDebugLine(center, DVec3(center.x + size, center.y, center.z), color);
		addDebugLine(center, DVec3(center.x, center.y - size, center.z), color);
		addDebugLine(center, DVec3(center.x, center.y + size, center.z), color);
		addDebugLine(center, DVec3(center.x, center.y, center.z - size), color);
		addDebugLine(center, DVec3(center.x, center.y, center.z + size), color);
	}


	static u32 ARGBToABGR(u32 color)
	{
		return ((color & 0xff) << 16) | (color & 0xff00) | ((color & 0xff0000) >> 16) | (color & 0xff000000);
	}


	void addDebugLine(const DVec3& from, const DVec3& to, u32 color) override 
	{
		DebugLine& line = m_debug_lines.emplace();
		line.from = from;
		line.to = to;
		line.color = ARGBToABGR(color);
	}

	DebugTriangle* addDebugTriangles(int count) override
	{
		const u32 new_size = m_debug_triangles.size() + count;
		if (new_size > m_debug_triangles.capacity()) {
			m_debug_triangles.reserve(maximum(new_size, m_debug_triangles.capacity() * 3 / 2));
		}
		m_debug_triangles.resize(new_size);
		return &m_debug_triangles[new_size - count];
	}


	DebugLine* addDebugLines(int count) override
	{
		const u32 new_size = m_debug_lines.size() + count;
		if (new_size > m_debug_lines.capacity()) {
			m_debug_lines.reserve(maximum(new_size, m_debug_lines.capacity() * 3 / 2));
		}
		m_debug_lines.resize(new_size);
		return &m_debug_lines[new_size - count];
	}


	RayCastModelHit castRayTerrain(EntityRef entity, const DVec3& origin, const Vec3& dir) override
	{
		RayCastModelHit hit;
		hit.is_hit = false;
		auto iter = m_terrains.find(entity);
		if (!iter.isValid()) return hit;

		Terrain* terrain = iter.value();
		hit = terrain->castRay(origin, dir);
		hit.component_type = TERRAIN_TYPE;
		hit.entity = terrain->getEntity();
		return hit;
	}

	RayCastModelHit castRay(const DVec3& origin, const Vec3& dir, EntityPtr ignored_model_instance) override
	{
		PROFILE_FUNCTION();
		RayCastModelHit hit;
		hit.is_hit = false;
		double cur_dist = DBL_MAX;
		const Universe& universe = getUniverse();
		for (int i = 0; i < m_model_instances.size(); ++i) {
			auto& r = m_model_instances[i];
			if (ignored_model_instance.index == i || !r.model) continue;
			if (!r.flags.isSet(ModelInstance::ENABLED)) continue;

			const EntityRef entity{i};
			const DVec3& pos = universe.getPosition(entity);
			float scale = universe.getScale(entity);
			float radius = r.model->getOriginBoundingRadius() * scale;
			const double dist = squaredLength(pos - origin);
			if (dist - radius > cur_dist) continue;
			
			float intersection_t;
			Vec3 rel_pos = Vec3(origin - pos);
			if (getRaySphereIntersection(rel_pos, dir, Vec3::ZERO, radius, Ref(intersection_t)) && intersection_t >= 0) {
				Vec3 aabb_hit;
				const Quat rot = universe.getRotation(entity).conjugated();
				const Vec3 rel_dir = rot.rotate(dir);
				const AABB& aabb = r.model->getAABB();
				rel_pos = rot.rotate(rel_pos / scale);
				if (getRayAABBIntersection(rel_pos, rel_dir, aabb.min, aabb.max - aabb.min, Ref(aabb_hit))) {
					RayCastModelHit new_hit = r.model->castRay(rel_pos, rel_dir, r.pose);
					if (new_hit.is_hit && (!hit.is_hit || new_hit.t * scale < hit.t)) {
						new_hit.entity = entity;
						new_hit.component_type = MODEL_INSTANCE_TYPE;
						hit = new_hit;
						hit.t *= scale;
						hit.is_hit = true;
						cur_dist = length(dir) * hit.t;
					}
				}
			}
		}

		for (auto* terrain : m_terrains) {
			RayCastModelHit terrain_hit = terrain->castRay(origin, dir);
			if (terrain_hit.is_hit && (!hit.is_hit || terrain_hit.t < hit.t)) {
				terrain_hit.component_type = TERRAIN_TYPE;
				terrain_hit.entity = terrain->getEntity();
				terrain_hit.mesh = nullptr;
				hit = terrain_hit;
			}
		}

		hit.origin = origin;
		hit.dir = dir;
		return hit;
	}
	
	Vec4 getShadowmapCascades(EntityRef entity) override
	{
		return m_environments[entity].cascades;
	}


	void setShadowmapCascades(EntityRef entity, const Vec4& value) override
	{
		Vec4 valid_value = value;
		valid_value.x = maximum(valid_value.x, 0.02f);
		valid_value.y = maximum(valid_value.x + 0.01f, valid_value.y);
		valid_value.z = maximum(valid_value.y + 0.01f, valid_value.z);
		valid_value.w = maximum(valid_value.z + 0.01f, valid_value.w);

		m_environments[entity].cascades = valid_value;
	}


	float getLightRange(EntityRef entity) override
	{
		return m_point_lights[entity].range;
	}

	bool getPointLightCastShadows(EntityRef entity) override {
		return m_point_lights[entity].flags.isSet(PointLight::CAST_SHADOWS);
	}

	void setPointLightCastShadows(EntityRef entity, bool value) override {
		m_point_lights[entity].flags.set(PointLight::CAST_SHADOWS, value);
	}

	bool getPointLightDynamic(EntityRef entity) override {
		return m_point_lights[entity].flags.isSet(PointLight::DYNAMIC);
	}

	void setPointLightDynamic(EntityRef entity, bool value) override {
		m_point_lights[entity].flags.set(PointLight::DYNAMIC, value);
	}

	void setLightRange(EntityRef entity, float value) override
	{
		m_point_lights[entity].range = value;
		m_culling_system->setRadius(entity, value);
	}


	void setActiveEnvironment(EntityRef entity) override
	{
		m_active_global_light_entity = entity;
	}


	EntityPtr getActiveEnvironment() override
	{
		return m_active_global_light_entity;
	}

	ReflectionProbe& getReflectionProbe(EntityRef entity) override {
		return m_reflection_probes[entity];
	}

	void enableReflectionProbe(EntityRef entity, bool enable) override {
		m_reflection_probes[entity].flags.set(ReflectionProbe::ENABLED, enable);
	}

	bool isReflectionProbeEnabled(EntityRef entity) override {
		return m_reflection_probes[entity].flags.isSet(ReflectionProbe::ENABLED);
	}
	
	Span<const ReflectionProbe> getReflectionProbes() override {
		return m_reflection_probes.values();
	}
	
	gpu::TextureHandle getReflectionProbesTexture() override {
		return m_reflection_probes_texture;
	}

	void reloadReflectionProbes() {
		for (ReflectionProbe& probe : m_reflection_probes) {
			load(probe);
		}
	}


	Span<const EnvironmentProbe> getEnvironmentProbes() override {
		return m_environment_probes.values();
	}
	
	Span<EntityRef> getEnvironmentProbesEntities() override {
		return m_environment_probes.keys();
	}
	
	Span<EntityRef> getReflectionProbesEntities() override {
		return m_reflection_probes.keys();
	}

	EnvironmentProbe& getEnvironmentProbe(EntityRef entity) override
	{
		return m_environment_probes[entity];
	}

	
	void enableEnvironmentProbe(EntityRef entity, bool enable) override
	{
		m_environment_probes[entity].flags.set(EnvironmentProbe::ENABLED, enable);
	}


	bool isEnvironmentProbeEnabled(EntityRef entity) override
	{
		return m_environment_probes[entity].flags.isSet(EnvironmentProbe::ENABLED);
	}


	float getTime() const override { return m_time; }


	void modelUnloaded(Model*, EntityRef entity)
	{
		auto& r = m_model_instances[entity.index];
		r.meshes = nullptr;
		r.mesh_count = 0;
		LUMIX_DELETE(m_allocator, r.pose);
		r.pose = nullptr;

		m_culling_system->remove(entity);
	}


	void modelLoaded(Model* model, EntityRef entity)
	{
		auto& r = m_model_instances[entity.index];

		float bounding_radius = r.model->getOriginBoundingRadius();
		float scale = m_universe.getScale(entity);
		const DVec3 pos = m_universe.getPosition(entity);
		const float radius = bounding_radius * scale;
		if(r.flags.isSet(ModelInstance::ENABLED)) {
			const RenderableTypes type = getRenderableType(*model, r.custom_material);
			m_culling_system->add(entity, (u8)type, pos, radius);
		}
		ASSERT(!r.pose);
		if (model->getBoneCount() > 0)
		{
			r.pose = LUMIX_NEW(m_allocator, Pose)(m_allocator);
			r.pose->resize(model->getBoneCount());
			model->getPose(*r.pose);
		}
		r.meshes = &r.model->getMesh(0);
		r.mesh_count = r.model->getMeshCount();

		if (r.flags.isSet(ModelInstance::IS_BONE_ATTACHMENT_PARENT)) {
			for (auto& attachment : m_bone_attachments) {
				if (attachment.parent_entity == entity) {
					updateBoneAttachment(attachment);
				}
			}
		}

		for (i32 i = 3; i >= 0; --i) {
			if (r.model->getLODIndices()[i].to != -1) {
				r.lod = float(i);
				break;
			}
		}

		while (m_mesh_sort_data.size() < m_model_instances.size()) {
			m_mesh_sort_data.emplace();
		}
		m_mesh_sort_data[entity.index].layer = r.meshes[0].layer;
		m_mesh_sort_data[entity.index].sort_key = r.meshes[0].sort_key;
	}


	void modelUnloaded(Model* model)
	{
		for (int i = 0, c = m_model_instances.size(); i < c; ++i)
		{
			if (m_model_instances[i].flags.isSet(ModelInstance::VALID) && m_model_instances[i].model == model)
			{
				modelUnloaded(model, {i});
			}
		}
	}


	void modelLoaded(Model* model)
	{
		auto map_iter = m_model_entity_map.find(model);
		EntityPtr e = map_iter.value();
		while(e.isValid()) {
			modelLoaded(model, (EntityRef)e);
			e = m_model_instances[e.index].next_model;
		}
	}

	
	void addToMaterialDecalMap(Material* material, EntityRef entity)
	{
		Decal& d = m_decals[entity];
		d.prev_decal = INVALID_ENTITY;
		auto map_iter = m_material_decal_map.find(material);
		if(map_iter.isValid()) {
			d.next_decal = map_iter.value();
			m_material_decal_map[material] = entity;
		}
		else {
			d.next_decal = INVALID_ENTITY;
			m_material_decal_map.insert(material, entity);
			material->getObserverCb().bind<&RenderSceneImpl::decalMaterialStateChanged>(this);
		}
	}

	
	void addToModelEntityMap(Model* model, EntityRef entity)
	{
		ModelInstance& r = m_model_instances[entity.index];
		r.prev_model = INVALID_ENTITY;
		auto map_iter = m_model_entity_map.find(model);
		if(map_iter.isValid()) {
			r.next_model = map_iter.value();
			m_model_instances[r.next_model.index].prev_model = entity;
			m_model_entity_map[model] = entity;
		}
		else {
			r.next_model = INVALID_ENTITY;
			m_model_entity_map.insert(model, entity);
			model->getObserverCb().bind<&RenderSceneImpl::modelStateChanged>(this);
		}
	}


	void removeFromModelEntityMap(Model* model, EntityRef entity)
	{
		ModelInstance& r = m_model_instances[entity.index];
		if(r.prev_model.isValid()) {
			m_model_instances[r.prev_model.index].next_model = r.next_model;
		}
		if(r.next_model.isValid()) {
			m_model_instances[r.next_model.index].prev_model = r.prev_model;
		}
		auto map_iter = m_model_entity_map.find(model);
		if(map_iter.value() == entity) {
			if(r.next_model.isValid()) {
				m_model_entity_map[model] = (EntityRef)r.next_model;
			}
			else {
				m_model_entity_map.erase(model);
				model->getObserverCb().unbind<&RenderSceneImpl::modelStateChanged>(this);
			}
		}
	}
	

	void removeFromMaterialDecalMap(Material* material, EntityRef entity)
	{
		Decal& d = m_decals[entity];
		if(d.prev_decal.isValid()) {
			m_decals[(EntityRef)d.prev_decal].next_decal = d.next_decal;
		}
		if(d.next_decal.isValid()) {
			m_decals[(EntityRef)d.next_decal].prev_decal = d.prev_decal;
		}
		auto map_iter = m_material_decal_map.find(material);
		if(map_iter.value() == entity) {
			if(d.next_decal.isValid()) {
				m_material_decal_map[material] = (EntityRef)d.next_decal;
			}
			else {
				m_material_decal_map.erase(material);
				material->getObserverCb().unbind<&RenderSceneImpl::decalMaterialStateChanged>(this);
			}
		}
	}

	void setModel(EntityRef entity, Model* model)
	{
		auto& model_instance = m_model_instances[entity.index];
		ASSERT(model_instance.flags.isSet(ModelInstance::VALID));
		Model* old_model = model_instance.model;
		bool no_change = model == old_model && old_model;
		if (no_change)
		{
			old_model->decRefCount();
			return;
		}
		if (old_model)
		{
			removeFromModelEntityMap(old_model, entity);

			if (old_model->isReady())
			{
				m_culling_system->remove(entity);
			}
			old_model->decRefCount();
		}
		model_instance.model = model;
		model_instance.meshes = nullptr;
		model_instance.mesh_count = 0;
		LUMIX_DELETE(m_allocator, model_instance.pose);
		model_instance.pose = nullptr;
		if (model)
		{
			addToModelEntityMap(model, entity);

			if (model->isReady())
			{
				modelLoaded(model, entity);
			}
		}
	}

	IAllocator& getAllocator() override { return m_allocator; }

	void createEnvironment(EntityRef entity)
	{
		Environment light;
		light.flags.set(Environment::CAST_SHADOWS);
		light.entity = entity;
		light.light_color = Vec3(1, 1, 1);
		light.direct_intensity = 1;
		light.indirect_intensity = 1;
		light.cascades.set(3, 8, 20, 60);

		if (m_environments.empty()) m_active_global_light_entity = entity;

		m_environments.insert(entity, light);
		m_universe.onComponentCreated(entity, ENVIRONMENT_TYPE, this);
	}


	void createPointLight(EntityRef entity)
	{
		PointLight light;
		light.entity = entity;
		light.color = Vec3(1, 1, 1);
		light.intensity = 1;
		light.fov = degreesToRadians(360);
		light.flags.base = 0;
		light.attenuation_param = 2;
		light.range = 10;
		light.guid = randGUID();
		const DVec3 pos = m_universe.getPosition(entity);
		m_point_lights.insert(entity, light);
		m_culling_system->add(entity, (u8)RenderableTypes::LOCAL_LIGHT, pos, light.range);

		m_universe.onComponentCreated(entity, POINT_LIGHT_TYPE, this);
	}


	void updateDecalInfo(Decal& decal) const
	{
		decal.radius = length(decal.half_extents);
		decal.transform = m_universe.getTransform(decal.entity);
	}


	void createDecal(EntityRef entity)
	{
		m_decals.insert(entity, Decal());
		Decal& decal = m_decals[entity];
		decal.material = nullptr;
		decal.entity = entity;
		decal.half_extents = Vec3(1, 1, 1);
		decal.uv_scale = Vec2(1);
		updateDecalInfo(decal);

		m_universe.onComponentCreated(entity, DECAL_TYPE, this);
	}


	void createEnvironmentProbe(EntityRef entity)
	{
		EnvironmentProbe& probe = m_environment_probes.insert(entity);

		probe.outer_range = Vec3(9001.f);
		probe.inner_range = Vec3(4500.f);
		probe.flags.set(EnvironmentProbe::ENABLED);
		memset(probe.sh_coefs, 0, sizeof(probe.sh_coefs));
		probe.sh_coefs[0] = Vec3(0.5f, 0.5f, 0.5f);

		m_universe.onComponentCreated(entity, ENVIRONMENT_PROBE_TYPE, this);
	}
	

	void createReflectionProbe(EntityRef entity)
	{
		ReflectionProbe& probe = m_reflection_probes.insert(entity);
		probe.guid = randGUID();

		StaticString<LUMIX_MAX_PATH> path;
		probe.flags.set(ReflectionProbe::ENABLED);

		m_universe.onComponentCreated(entity, REFLECTION_PROBE_TYPE, this);
	}

	void createBoneAttachment(EntityRef entity)
	{
		BoneAttachment& attachment = m_bone_attachments.emplace(entity);
		attachment.entity = entity;
		attachment.parent_entity = INVALID_ENTITY;
		attachment.bone_index = -1;

		m_universe.onComponentCreated(entity, BONE_ATTACHMENT_TYPE, this);
	}


	void createModelInstance(EntityRef entity)
	{
		while(entity.index >= m_model_instances.size())
		{
			auto& r = m_model_instances.emplace();
			r.flags.clear();
			r.flags.set(ModelInstance::VALID, false);
			r.model = nullptr;
			r.pose = nullptr;
		}
		auto& r = m_model_instances[entity.index];
		r.model = nullptr;
		r.meshes = nullptr;
		r.pose = nullptr;
		r.flags.clear();
		r.flags.set(ModelInstance::VALID);
		r.flags.set(ModelInstance::ENABLED);
		r.mesh_count = 0;
		m_universe.onComponentCreated(entity, MODEL_INSTANCE_TYPE, this);
	}

	void updateParticleEmitter(EntityRef entity, float dt) override {
		if (!m_particle_emitters[entity]) return;
		m_particle_emitters[entity]->update(dt);
	}

	void setParticleEmitterPath(EntityRef entity, const Path& path) override
	{
		if (!m_particle_emitters[entity]) return;

		ParticleEmitterResource* res = m_engine.getResourceManager().load<ParticleEmitterResource>(path);
		m_particle_emitters[entity]->setResource(res);
	}


	Path getParticleEmitterPath(EntityRef entity) override
	{
		ParticleEmitter* emitter = m_particle_emitters[entity];
		if (!emitter) return Path("");
		if (!emitter->getResource()) return Path("");

		return emitter->getResource()->getPath();
	}

	void setParticleEmitterRate(EntityRef entity, u32 value) override {
		ParticleEmitter* emitter = m_particle_emitters[entity];
		emitter->m_emit_rate = value;
	}

	u32 getParticleEmitterRate(EntityRef entity) override {
		return m_particle_emitters[entity]->m_emit_rate;
	}

	const AssociativeArray<EntityRef, ParticleEmitter*>& getParticleEmitters() const override
	{
		return m_particle_emitters;
	}

private:
	IAllocator& m_allocator;
	Universe& m_universe;
	Renderer& m_renderer;
	Engine& m_engine;
	UniquePtr<CullingSystem> m_culling_system;
	u64 m_render_cmps_mask;

	EntityPtr m_active_global_light_entity;
	HashMap<EntityRef, PointLight> m_point_lights;

	HashMap<EntityRef, Decal> m_decals;
	Array<ModelInstance> m_model_instances;
	Array<MeshSortData> m_mesh_sort_data;
	HashMap<EntityRef, Environment> m_environments;
	HashMap<EntityRef, Camera> m_cameras;
	EntityPtr m_active_camera = INVALID_ENTITY;
	AssociativeArray<EntityRef, BoneAttachment> m_bone_attachments;
	AssociativeArray<EntityRef, EnvironmentProbe> m_environment_probes;
	AssociativeArray<EntityRef, ReflectionProbe> m_reflection_probes;
	HashMap<EntityRef, Terrain*> m_terrains;
	AssociativeArray<EntityRef, ParticleEmitter*> m_particle_emitters;
	gpu::TextureHandle m_reflection_probes_texture = gpu::INVALID_TEXTURE;

	Array<DebugTriangle> m_debug_triangles;
	Array<DebugLine> m_debug_lines;
	HashMap<EntityRef, FurComponent> m_furs;

	float m_time;
	float m_lod_multiplier;
	bool m_is_updating_attachments;
	bool m_is_game_running;

	HashMap<Model*, EntityRef> m_model_entity_map;
	HashMap<Material*, EntityRef> m_material_decal_map;
};


void RenderScene::reflect() {
	using namespace reflection;

	struct RotationModeEnum : reflection::EnumAttribute {
		u32 count(ComponentUID cmp) const override { return 2; }
		const char* name(ComponentUID cmp, u32 idx) const override {
			switch((Terrain::GrassType::RotationMode)idx) {
				case Terrain::GrassType::RotationMode::ALL_RANDOM: return "All random";
				case Terrain::GrassType::RotationMode::Y_UP: return "Y up";
				default: ASSERT(false); return "N/A";
			}
		}
	};

	struct BoneEnum : reflection::EnumAttribute {
		u32 count(ComponentUID cmp) const override {
			RenderScene* render_scene = static_cast<RenderScene*>(cmp.scene);
			EntityPtr model_instance = getModelInstance(render_scene, (EntityRef)cmp.entity);
			if (!model_instance.isValid()) return 0;

			auto* model = render_scene->getModelInstanceModel((EntityRef)model_instance);
			if (!model || !model->isReady()) return 0;

			return model->getBoneCount();
		}

		const char* name(ComponentUID cmp, u32 idx) const override {
			RenderScene* render_scene = static_cast<RenderScene*>(cmp.scene);
			EntityPtr model_instance = getModelInstance(render_scene, (EntityRef)cmp.entity);
			if (!model_instance.isValid()) return "";

			auto* model = render_scene->getModelInstanceModel((EntityRef)model_instance);
			if (!model) return "";

			return idx < (u32)model->getBoneCount() ? model->getBone(idx).name.c_str() : "N/A";
		}


		EntityPtr getModelInstance(RenderScene* render_scene, EntityRef bone_attachment) const {
			EntityPtr parent_entity = render_scene->getBoneAttachmentParent(bone_attachment);
			if (!parent_entity.isValid()) return INVALID_ENTITY;
			return render_scene->getUniverse().hasComponent((EntityRef)parent_entity, MODEL_INSTANCE_TYPE) ? parent_entity : INVALID_ENTITY;
		}
	};

	LUMIX_SCENE(RenderSceneImpl, "renderer",
		LUMIX_FUNC(RenderScene::setGlobalLODMultiplier),
		LUMIX_FUNC(RenderScene::getGlobalLODMultiplier),
		LUMIX_FUNC(RenderScene::addDebugCross),
		LUMIX_FUNC(RenderScene::addDebugLine),
		LUMIX_FUNC(RenderScene::addDebugTriangle),
		LUMIX_CMP(BoneAttachment, "bone_attachment", "Render / Bone attachment",
			icon(ICON_FA_BONE),
			LUMIX_PROP(BoneAttachmentParent, "Parent"),
			LUMIX_PROP(BoneAttachmentPosition, "Relative position"),
			LUMIX_PROP(BoneAttachmentRotation, "Relative rotation", RadiansAttribute()),
			LUMIX_PROP(BoneAttachmentBone, "Bone", BoneEnum()) 
		),
		LUMIX_CMP(Fur, "fur", "Render / Fur",
			var_property("Layers", &RenderScene::getFur, &FurComponent::layers),
			var_property("Scale", &RenderScene::getFur, &FurComponent::scale),
			var_property("Gravity", &RenderScene::getFur, &FurComponent::gravity),
			var_property("Enabled", &RenderScene::getFur, &FurComponent::enabled)
		),
		LUMIX_CMP(EnvironmentProbe, "environment_probe", "Render / Environment probe",
			property("Enabled", &RenderScene::isEnvironmentProbeEnabled, &RenderScene::enableEnvironmentProbe),
			var_property("Inner range", &RenderScene::getEnvironmentProbe, &EnvironmentProbe::inner_range),
			var_property("Outer range", &RenderScene::getEnvironmentProbe, &EnvironmentProbe::outer_range)
		),
		LUMIX_CMP(ReflectionProbe, "reflection_probe", "Render / Reflection probe",
			property("Enabled", &RenderScene::isReflectionProbeEnabled, &RenderScene::enableReflectionProbe),
			var_property("size", &RenderScene::getReflectionProbe, &ReflectionProbe::size),
			var_property("half_extents", &RenderScene::getReflectionProbe, &ReflectionProbe::half_extents)
		),
		LUMIX_CMP(ParticleEmitter, "particle_emitter", "Render / Particle emitter",
			LUMIX_PROP(ParticleEmitterRate, "Emit rate"),
			LUMIX_PROP(ParticleEmitterPath, "Source", ResourceAttribute(ParticleEmitterResource::TYPE))
		),
		LUMIX_CMP(Camera, "camera", "Render / Camera",
			icon(ICON_FA_CAMERA),
			var_property("FOV", &RenderScene::getCamera, &Camera::fov, RadiansAttribute()),
			var_property("Near", &RenderScene::getCamera, &Camera::near, MinAttribute(0)),
			var_property("Far", &RenderScene::getCamera, &Camera::far, MinAttribute(0)),
			var_property("Orthographic", &RenderScene::getCamera, &Camera::is_ortho),
			var_property("Orthographic size", &RenderScene::getCamera, &Camera::ortho_size, MinAttribute(0))
		),
		LUMIX_CMP(ModelInstance, "model_instance", "Render / Mesh",
			LUMIX_FUNC_EX(RenderScene::getModelInstanceModel, "getModel"),
			property("Enabled", &RenderScene::isModelInstanceEnabled, &RenderScene::enableModelInstance),
			property("Material", &RenderScene::getModelInstanceMaterialOverride,&RenderScene::setModelInstanceMaterialOverride, NoUIAttribute()),
			LUMIX_PROP(ModelInstancePath, "Source", ResourceAttribute(Model::TYPE))
		),
		LUMIX_CMP(Environment, "environment", "Render / Environment",
			icon(ICON_FA_GLOBE),
			var_property("Color", &RenderScene::getEnvironment, &Environment::light_color, ColorAttribute()),
			var_property("Intensity", &RenderScene::getEnvironment, &Environment::direct_intensity, MinAttribute(0)),
			var_property("Indirect intensity", &RenderScene::getEnvironment, &Environment::indirect_intensity, MinAttribute(0)),
			LUMIX_PROP(ShadowmapCascades, "Shadow cascades"),
			LUMIX_PROP(EnvironmentCastShadows, "Cast shadows")
		),
		LUMIX_CMP(PointLight, "point_light", "Render / Point light",
			icon(ICON_FA_LIGHTBULB),
			LUMIX_PROP(PointLightCastShadows, "Cast shadows"),
			LUMIX_PROP(PointLightDynamic, "Dynamic"),
			var_property("Intensity", &RenderScene::getPointLight, &PointLight::intensity, MinAttribute(0)),
			var_property("FOV", &RenderScene::getPointLight, &PointLight::fov, ClampAttribute(0, 360), RadiansAttribute()),
			var_property("Attenuation", &RenderScene::getPointLight, &PointLight::attenuation_param, ClampAttribute(0, 100)),
			var_property("Color", &RenderScene::getPointLight, &PointLight::color, ColorAttribute()),
			LUMIX_PROP(LightRange, "Range", MinAttribute(0))
		),
		LUMIX_CMP(Decal, "decal", "Render / Decal",
			LUMIX_PROP(DecalMaterialPath, "Material", ResourceAttribute(Material::TYPE)),
			LUMIX_PROP(DecalHalfExtents, "Half extents", MinAttribute(0)),
			LUMIX_PROP(DecalUVScale, "UV scale", MinAttribute(0))
		),
		LUMIX_CMP(Terrain, "terrain", "Render / Terrain",
			LUMIX_FUNC(RenderScene::getTerrainNormalAt),
			LUMIX_FUNC(RenderScene::getTerrainHeightAt),
			LUMIX_PROP(TerrainMaterialPath, "Material", ResourceAttribute(Material::TYPE)),
			LUMIX_PROP(TerrainXZScale, "XZ scale", MinAttribute(0)),
			LUMIX_PROP(TerrainYScale, "Height scale", MinAttribute(0)),
			array("grass", &RenderScene::getGrassCount, &RenderScene::addGrass, &RenderScene::removeGrass,
				LUMIX_PROP(GrassPath, "Mesh", ResourceAttribute(Model::TYPE)),
				LUMIX_PROP(GrassDistance, "Distance", MinAttribute(1)),
				LUMIX_PROP(GrassSpacing, "Spacing"),
				LUMIX_PROP(GrassRotationMode, "Mode", RotationModeEnum())
			)
		)
	);
}

RenderSceneImpl::RenderSceneImpl(Renderer& renderer,
	Engine& engine,
	Universe& universe,
	IAllocator& allocator)
	: m_engine(engine)
	, m_universe(universe)
	, m_renderer(renderer)
	, m_allocator(allocator)
	, m_model_entity_map(m_allocator)
	, m_model_instances(m_allocator)
	, m_cameras(m_allocator)
	, m_terrains(m_allocator)
	, m_point_lights(m_allocator)
	, m_environments(m_allocator)
	, m_decals(m_allocator)
	, m_debug_triangles(m_allocator)
	, m_debug_lines(m_allocator)
	, m_active_global_light_entity(INVALID_ENTITY)
	, m_active_camera(INVALID_ENTITY)
	, m_is_game_running(false)
	, m_particle_emitters(m_allocator)
	, m_bone_attachments(m_allocator)
	, m_environment_probes(m_allocator)
	, m_reflection_probes(m_allocator)
	, m_lod_multiplier(1.0f)
	, m_time(0)
	, m_is_updating_attachments(false)
	, m_material_decal_map(m_allocator)
	, m_mesh_sort_data(m_allocator)
	, m_furs(m_allocator)
{

	m_universe.entityTransformed().bind<&RenderSceneImpl::onEntityMoved>(this);
	m_universe.entityDestroyed().bind<&RenderSceneImpl::onEntityDestroyed>(this);
	m_culling_system = CullingSystem::create(m_allocator, engine.getPageAllocator());
	m_model_instances.reserve(5000);
	m_mesh_sort_data.reserve(5000);

	m_render_cmps_mask = 0;

	Renderer::MemRef mem;
	m_reflection_probes_texture = renderer.createTexture(128, 128, 32, gpu::TextureFormat::BC3, gpu::TextureFlags::IS_CUBE, mem, "reflection_probes");

	const u32 hash = crc32("renderer");
	for (const reflection::RegisteredComponent& cmp : reflection::getComponents()) {
		if (cmp.scene == hash) {
			m_render_cmps_mask |= (u64)1 << cmp.cmp->component_type.index;
		}
	}
}


UniquePtr<RenderScene> RenderScene::createInstance(Renderer& renderer,
	Engine& engine,
	Universe& universe,
	IAllocator& allocator)
{
	return UniquePtr<RenderSceneImpl>::create(allocator, renderer, engine, universe, allocator);
}


void RenderScene::registerLuaAPI(lua_State* L)
{
	#define REGISTER_FUNCTION(F)\
		do { \
			auto f = &LuaWrapper::wrapMethod<&RenderSceneImpl::F>; \
			LuaWrapper::createSystemFunction(L, "Renderer", #F, f); \
		} while(false) \

	REGISTER_FUNCTION(getTerrainMaterial);
	REGISTER_FUNCTION(getPoseBonePosition);

	#undef REGISTER_FUNCTION

	#define REGISTER_FUNCTION(F)\
		do { \
		auto f = &LuaWrapper::wrap<&RenderSceneImpl::LUA_##F>; \
		LuaWrapper::createSystemFunction(L, "Renderer", #F, f); \
		} while(false) \

	REGISTER_FUNCTION(getModelBoneIndex);
	REGISTER_FUNCTION(makeScreenshot);

	LuaWrapper::createSystemFunction(L, "Renderer", "castCameraRay", &RenderSceneImpl::LUA_castCameraRay);

	#undef REGISTER_FUNCTION
}


} // namespace Lumix