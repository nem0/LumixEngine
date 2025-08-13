#include "render_module.h"

#include "core/array.h"
#include "core/associative_array.h"
#include "core/crt.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "core/geometry.h"
#include "core/hash.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/math.h"
#include "core/page_allocator.h"
#include "core/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "core/stack_array.h"
#include "core/stream.h"
#include "engine/world.h"
#include "imgui/IconsFontAwesome5.h"
#include "renderer/culling_system.h"
#include "renderer/draw_stream.h"
#include "renderer/font.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/particle_system.h"
#include "renderer/pipeline.h"
#include "renderer/pose.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"


namespace Lumix
{

static const ComponentType INSTANCED_MODEL_TYPE = reflection::getComponentType("instanced_model");
static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
static const ComponentType DECAL_TYPE = reflection::getComponentType("decal");
static const ComponentType CURVE_DECAL_TYPE = reflection::getComponentType("curve_decal");
static const ComponentType POINT_LIGHT_TYPE = reflection::getComponentType("point_light");
static const ComponentType PARTICLE_EMITTER_TYPE = reflection::getComponentType("particle_emitter");
static const ComponentType ENVIRONMENT_TYPE = reflection::getComponentType("environment");
static const ComponentType CAMERA_TYPE = reflection::getComponentType("camera");
static const ComponentType TERRAIN_TYPE = reflection::getComponentType("terrain");
static const ComponentType BONE_ATTACHMENT_TYPE = reflection::getComponentType("bone_attachment");
static const ComponentType ENVIRONMENT_PROBE_TYPE = reflection::getComponentType("environment_probe");
static const ComponentType REFLECTION_PROBE_TYPE = reflection::getComponentType("reflection_probe");
static const ComponentType FUR_TYPE = reflection::getComponentType("fur");
static const ComponentType PROCEDURAL_GEOM_TYPE = reflection::getComponentType("procedural_geom");


struct BoneAttachment
{
	EntityRef entity;
	EntityPtr parent_entity;
	int bone_index;
	LocalRigidTransform relative_transform;
};

u32 ProceduralGeometry::getVertexCount() const {
	return vertex_decl.getStride() ? u32(vertex_data.size() / vertex_decl.getStride()) : 0;
}

u32 ProceduralGeometry::getIndexCount() const {
	return u32(index_data.size() / (index_type == gpu::DataType::U16 ? 2 : 4));
}

static RenderableTypes getRenderableType(const Model& model) {
	ASSERT(model.isReady());
	return model.isSkinned() ? RenderableTypes::SKINNED : RenderableTypes::MESH;
}

struct ReflectionProbe::LoadJob {
	LoadJob(struct RenderModuleImpl& module, EntityRef probe, IAllocator& allocator)
		: m_module(module)
		, m_allocator(allocator)
		, m_entity(probe)
	{}

	~LoadJob();

	void callback(Span<const u8> data, bool success);

	IAllocator& m_allocator;
	RenderModuleImpl& m_module;
	EntityRef m_entity;
	FileSystem::AsyncHandle m_handle = FileSystem::AsyncHandle::invalid();
};

struct RenderModuleImpl final : RenderModule {
	RenderModuleImpl(Renderer& renderer,
		Engine& engine,
		World& world,
		IAllocator& allocator);

	void getInstancedModelBlob(EntityRef entity, OutputMemoryStream& value) {
		const Array<InstancedModel::InstanceData>& instances = m_instanced_models[entity].instances;
		value.write(instances.size());
		value.write(instances.begin(), instances.byte_size());
	}

	void setInstancedModelBlob(EntityRef entity, InputMemoryStream& value) {
		const i32 size = value.read<i32>();
		InstancedModel& im = beginInstancedModelEditing(entity);
		im.instances.resize(size);
		value.read(im.instances.begin(), im.instances.byte_size());
		endInstancedModelEditing(entity);
	}

	void decalMaterialStateChanged(Resource::State old_state, Resource::State new_state, Resource& resource)
	{
		Material& material = static_cast<Material&>(resource);
		
		if (new_state == Resource::State::READY) {
			auto map_iter = m_material_decal_map.find(&material);
			EntityPtr e = map_iter.value();
			while(e.isValid()) {
				const float radius = length(m_decals[(EntityRef)e].half_extents);
				const DVec3 pos = m_world.getPosition((EntityRef)e);
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


	void curveDecalMaterialStateChanged(Resource::State old_state, Resource::State new_state, Resource& resource)
	{
		Material& material = static_cast<Material&>(resource);
		
		if (new_state == Resource::State::READY) {
			auto map_iter = m_material_curve_decal_map.find(&material);
			EntityPtr e = map_iter.value();
			while(e.isValid()) {
				const float radius = length(m_curve_decals[(EntityRef)e].half_extents);
				const DVec3 pos = m_world.getPosition((EntityRef)e);
				m_culling_system->add((EntityRef)e, (u8)RenderableTypes::CURVE_DECAL, pos, radius);
				e = m_curve_decals[(EntityRef)e].next_decal;
			}
			return;
		}
		
		if (old_state == Resource::State::READY) {
			auto map_iter = m_material_curve_decal_map.find(&material);
			EntityPtr e = map_iter.value();
			while(e.isValid()) {
				m_culling_system->remove((EntityRef)e);
				e = m_curve_decals[(EntityRef)e].next_decal;
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


	~RenderModuleImpl() {
		m_world.componentTransformed(MODEL_INSTANCE_TYPE).unbind<&RenderModuleImpl::onModelInstanceMoved>(this);
		m_world.componentTransformed(DECAL_TYPE).unbind<&RenderModuleImpl::onDecalMoved>(this);
		m_world.componentTransformed(CURVE_DECAL_TYPE).unbind<&RenderModuleImpl::onCurveDecalMoved>(this);
		m_world.componentTransformed(PARTICLE_EMITTER_TYPE).unbind<&RenderModuleImpl::onParticleEmitterMoved>(this);
		m_world.componentTransformed(POINT_LIGHT_TYPE).unbind<&RenderModuleImpl::onPointLightMoved>(this);
		m_world.componentTransformed(BONE_ATTACHMENT_TYPE).unbind<&RenderModuleImpl::onBoneAttachmentMoved>(this);

		for (Decal& decal : m_decals) {
			if (decal.material) decal.material->decRefCount();
		}

		for (ProceduralGeometry& pg : m_procedural_geometries) {
			if (pg.material) pg.material->decRefCount();
			if (pg.vertex_buffer) m_renderer.getEndFrameDrawStream().destroy(pg.vertex_buffer);
			if (pg.index_buffer) m_renderer.getEndFrameDrawStream().destroy(pg.index_buffer);
		}

		for (auto* terrain : m_terrains) {
			LUMIX_DELETE(m_allocator, terrain);
		}

		for (InstancedModel& im : m_instanced_models) {
			if (im.model) im.model->decRefCount();
			if (im.gpu_data) m_renderer.getEndFrameDrawStream().destroy(im.gpu_data);
		}

		for (ModelInstance& r : m_model_instances) {
			if (!isFlagSet(r.flags, ModelInstance::VALID)) continue;
			
			if (!r.model || !r.model->isReady() || r.mesh_materials.begin() != &r.model->getMeshMaterial(0)) {
				for (MeshMaterial& m : r.mesh_materials) {
					m.material->decRefCount();
					m_renderer.freeSortKey(m.sort_key);
				}
				m_allocator.deallocate(r.mesh_materials.begin());
			}
			
			if (r.model) r.model->decRefCount();
			LUMIX_DELETE(m_allocator, r.pose);
		}
		
		for(auto iter : m_model_entity_map.iterated()) {
			Model* model = iter.key();
			model->getObserverCb().unbind<&RenderModuleImpl::modelStateChanged>(this);
		}

		for(auto iter : m_material_decal_map.iterated()) {
			Material* mat = iter.key();
			mat->getObserverCb().unbind<&RenderModuleImpl::decalMaterialStateChanged>(this);
		}

		for(auto iter : m_material_curve_decal_map.iterated()) {
			Material* mat = iter.key();
			mat->getObserverCb().unbind<&RenderModuleImpl::curveDecalMaterialStateChanged>(this);
		}

		for (const ReflectionProbe& probe : m_reflection_probes) {
			LUMIX_DELETE(m_allocator, probe.load_job);
		}

		m_renderer.getEndFrameDrawStream().destroy(m_reflection_probes_texture);
		m_world.entityDestroyed().unbind<&RenderModuleImpl::onEntityDestroyed>(this);
		m_culling_system.reset();
	}


	World& getWorld() override { return m_world; }


	ISystem& getSystem() const override { return m_renderer; }


	Ray getCameraRay(EntityRef camera_entity, const Vec2& screen_pos) override
	{
		Ray out;
		Camera& camera = m_cameras[camera_entity];
		out.origin = m_world.getPosition(camera_entity);

		float width = camera.screen_width;
		float height = camera.screen_height;
		if (width <= 0 || height <= 0)
		{
			out.dir = m_world.getRotation(camera_entity).rotate(Vec3(0, 0, 1));
			return out;
		}

		float nx = 2 * (screen_pos.x / width) - 1;
		float ny = 2 * ((height - screen_pos.y) / height) - 1;

		const Matrix projection_matrix = getCameraProjection(camera_entity);
		const Transform view = m_world.getTransform(camera_entity);

		if (camera.is_ortho) {
			const float ratio = camera.screen_height > 0 ? camera.screen_width / camera.screen_height : 1;
			out.origin += view.rot * Vec3(1, 0, 0) * nx * camera.ortho_size * ratio
				+ view.rot * Vec3(0, 1, 0) * ny * camera.ortho_size;
		}

		const Matrix inv_projection = projection_matrix.inverted();

		Vec4 p0 = inv_projection * Vec4(nx, ny, -1, 1);
		Vec4 p1 = inv_projection * Vec4(nx, ny, 1, 1);
		p0 *= 1 / p0.w;
		p1 *= 1 / p1.w;
		out.dir = normalize((p1 - p0).xyz());
		out.dir = view.rot * out.dir;
		return out;
	}

	void setActiveCamera(EntityRef camera) override { m_active_camera = camera; }
	EntityPtr getActiveCamera() const override { return m_active_camera; }

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
		vp.pos = m_world.getPosition(entity);
		vp.rot = m_world.getRotation(entity);
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
		const Transform tr = m_world.getTransform(entity);
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
		const Transform tr = m_world.getTransform(entity);
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
		if (!m_world.hasComponent(model_instance, MODEL_INSTANCE_TYPE)) return;
		const Pose* parent_pose = lockPose(model_instance);
		if (!parent_pose) return;

		Transform parent_entity_transform = m_world.getTransform((EntityRef)bone_attachment.parent_entity);
		int idx = bone_attachment.bone_index;
		if (idx < 0 || idx >= (int)parent_pose->count) {
			unlockPose(model_instance, false);
			return;
		}
		Vec3 original_scale = m_world.getScale(bone_attachment.entity);
		const LocalRigidTransform bone_transform = {parent_pose->positions[idx], parent_pose->rotations[idx] };
		const LocalRigidTransform relative_transform = { bone_attachment.relative_transform.pos, bone_attachment.relative_transform.rot };
		Transform result = parent_entity_transform.compose(bone_transform * relative_transform);
		result.scale = original_scale;
		m_world.setTransform(bone_attachment.entity, result);
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
		if (!m_world.hasComponent(model_instance, MODEL_INSTANCE_TYPE)) return;
		const Pose* pose = lockPose(model_instance);
		if (!pose) return;

		ASSERT(pose->is_absolute);
		if (attachment.bone_index >= (int)pose->count) {
			unlockPose(model_instance, false);
			return;
		}
		const LocalRigidTransform bone_transform = {pose->positions[attachment.bone_index], pose->rotations[attachment.bone_index]};

		const EntityRef parent = (EntityRef)attachment.parent_entity;
		Transform parent_transform = m_world.getTransform(parent).compose(bone_transform);
		const Transform child_transform = m_world.getTransform(attachment.entity);
		const Transform res = Transform::computeLocal(parent_transform, child_transform);
		attachment.relative_transform = {Vec3(res.pos), res.rot};
		unlockPose(model_instance, false);
	}

	Vec3 getBoneAttachmentPosition(EntityRef entity) override {
		return m_bone_attachments[entity].relative_transform.pos;
	}

	void setBoneAttachmentPosition(EntityRef entity, const Vec3& pos) override {
		BoneAttachment& attachment = m_bone_attachments[entity];
		attachment.relative_transform.pos = pos;
		EntityPtr backup = m_updating_attachment;
		m_updating_attachment = entity;
		updateBoneAttachment(attachment);
		m_updating_attachment = backup;
	}

	Vec3 getBoneAttachmentRotation(EntityRef entity) override {
		return m_bone_attachments[entity].relative_transform.rot.toEuler();
	}

	void setBoneAttachmentRotation(EntityRef entity, const Vec3& rot) override {
		BoneAttachment& attachment = m_bone_attachments[entity];
		Vec3 euler = rot;
		euler.x = clamp(euler.x, -PI * 0.5f, PI * 0.5f);
		attachment.relative_transform.rot.fromEuler(euler);
		EntityPtr backup = m_updating_attachment;
		m_updating_attachment = entity;
		updateBoneAttachment(attachment);
		m_updating_attachment = backup;
	}

	void setBoneAttachmentRotationQuat(EntityRef entity, const Quat& rot) override {
		BoneAttachment& attachment = m_bone_attachments[entity];
		attachment.relative_transform.rot = rot;
		EntityPtr backup = m_updating_attachment;
		m_updating_attachment = entity;
		updateBoneAttachment(attachment);
		m_updating_attachment = backup;
	}

	int getBoneAttachmentBone(EntityRef entity) override {
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
			mi.flags |= ModelInstance::IS_BONE_ATTACHMENT_PARENT;
		}
		updateRelativeMatrix(ba);
	}

	void startGame() override { m_is_game_running = true; }
	void stopGame() override { m_is_game_running = false; }

	void endFrame() override {
		for (EntityRef e : m_moved_instances) {
			if (!m_world.hasEntity(e)) continue;

			m_model_instances[e.index].flags &= ~ModelInstance::MOVED;
			m_model_instances[e.index].prev_frame_transform = m_world.getTransform(e);
		}
		m_moved_instances.clear();
	}

	void update(float dt) override {
		PROFILE_FUNCTION();

		if (!m_is_game_running) return;

		StackArray<EntityRef, 16> to_delete(m_allocator);
		jobs::Mutex mutex;
		ParticleSystem::Stats stats = {};
		// TODO move to parallel update?
		jobs::forEach(m_particle_emitters.capacity(), 1, [&](i32 idx, i32){
			ParticleSystem* ps = m_particle_emitters.getFromIndex(idx);
			if (!ps) return;

			if (ps->update(dt, m_engine.getPageAllocator())) {
				jobs::enter(&mutex);
				to_delete.push(*ps->m_entity);
				jobs::exit(&mutex);
			}

			stats.emitted.add(ps->m_last_update_stats.emitted);
			stats.killed.add(ps->m_last_update_stats.killed);
			stats.processed.add(ps->m_last_update_stats.processed);
		});

		static u32 emitted_particles_stat = profiler::createCounter("Emitted particles", 0);
		static u32 killed_particles_stat = profiler::createCounter("Killed particles", 0);
		static u32 processed_particles_stat = profiler::createCounter("Processed particles", 0);

		profiler::pushCounter(emitted_particles_stat, (float)stats.emitted);
		profiler::pushCounter(killed_particles_stat, (float)stats.killed);
		profiler::pushCounter(processed_particles_stat, (float)stats.processed);

		for (EntityRef e : to_delete) {
			m_world.destroyEntity(e);
		}
	}

	int getVersion() const override { return (int)RenderModuleVersion::LATEST; }

	const char* getName() const override { return "renderer"; }

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
		for (Camera& camera : m_cameras) {
			serializer.write(camera.entity);
			serializer.write(camera.fov);
			serializer.write(camera.near);
			serializer.write(camera.far);
			serializer.write(camera.ortho_size);
			serializer.write(camera.screen_width);
			serializer.write(camera.screen_height);
			serializer.write(camera.is_ortho);
			serializer.write(camera.film_grain_intensity);
			serializer.write(camera.dof_enabled);
			serializer.write(camera.dof_distance);
			serializer.write(camera.dof_range);
			serializer.write(camera.dof_max_blur_size);
			serializer.write(camera.dof_sharp_range);
		}
	}

	void serializeLights(OutputMemoryStream& serializer) {
		serializer.write((i32)m_point_lights.size());
		for (const PointLight& pl : m_point_lights) {
			serializer.write(pl);
		}

		serializer.write((i32)m_environments.size());
		for (const Environment& env : m_environments) {
			serializer.write(env.light_color);
			serializer.write(env.direct_intensity);
			serializer.write(env.indirect_intensity);
			serializer.write(env.entity);
			serializer.write(env.cascades);
			serializer.write(env.flags);
			serializer.writeString(env.cubemap_sky ? env.cubemap_sky->getPath().c_str() : "");
			serializer.write(env.sky_intensity);
			serializer.write(env.scatter_rayleigh);
			serializer.write(env.scatter_mie);
			serializer.write(env.absorb_mie);
			serializer.write(env.sunlight_color);
			serializer.write(env.fog_scattering);
			serializer.write(env.sunlight_strength);
			serializer.write(env.height_distribution_rayleigh);
			serializer.write(env.height_distribution_mie);
			serializer.write(env.ground_r);
			serializer.write(env.atmo_r);
			serializer.write(env.fog_top);
			serializer.write(env.atmo_enabled);
			serializer.write(env.godrays_enabled);
			serializer.write(env.clouds_enabled);
			serializer.write(env.clouds_top);
			serializer.write(env.clouds_bottom);
			serializer.write(env.fog_density);
		}
		serializer.write(m_active_global_light_entity);
	}

	void serializeModelInstances(OutputMemoryStream& serializer) {
		u32 len = 0;
		HashMap<Model*, u32> offsets(m_allocator);
		for (auto iter : m_model_entity_map.iterated()) {
			offsets.insert(iter.key(), len);
			len += iter.key()->getPath().length() + 1;
		}

		serializer.write(len);
		for (auto iter : m_model_entity_map.iterated()) {
			serializer.writeString(iter.key()->getPath());
		}

		serializer.write((i32)m_model_instances.size());
		for (const ModelInstance& r : m_model_instances) {
			serializer.write(r.flags);
			if(r.flags & ModelInstance::VALID) {
				serializer.write(u32(r.model ? offsets[r.model] : 0xffFFffFF));
				if (hasMaterialOverride(r)) {
					serializer.write((u32)r.mesh_materials.size());
					for (const MeshMaterial& m : r.mesh_materials) {
						serializer.writeString(m.material ? m.material->getPath().c_str() : "");
					}
				}
				else {
					serializer.write(u32(0));
				}
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

	void serializeInstancedModels(OutputMemoryStream& serializer) {
		serializer.write(m_instanced_models.size());
		for (auto iter : m_instanced_models.iterated()) {
			serializer.write(iter.key());
			const InstancedModel& im = iter.value();
			serializer.writeString(im.model ? im.model->getPath() : Path());
			serializer.write(im.instances.size());
			serializer.write(im.instances.begin(), im.instances.byte_size());
		}
	}

	void deserializeInstancedModels(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) {
		if (version <= (i32)RenderModuleVersion::INSTANCED_MODEL) return;
		u32 count;
		serializer.read(count);
		m_instanced_models.reserve(count + m_instanced_models.size());
		ResourceManagerHub& rm = m_engine.getResourceManager();
		for (u32 i = 0; i < count; ++i) {
			EntityRef e;
			serializer.read(e);
			e = entity_map.get(e);
			InstancedModel im(m_allocator);
			const char* path = serializer.readString();
			im.model = path[0] ? rm.load<Model>(Path(path)) : nullptr;
			u32 size;
			serializer.read(size);
			im.instances.resize(size);
			serializer.read(im.instances.begin(), im.instances.byte_size());
			m_instanced_models.insert(e, static_cast<InstancedModel&&>(im));
			initInstancedModelGPUData(e);
			m_world.onComponentCreated(e, INSTANCED_MODEL_TYPE, this);
		}
	}

	void serializeFurs(OutputMemoryStream& serializer) {
		serializer.write(m_furs.size());
		for (auto iter : m_furs.iterated()) {
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
			m_world.onComponentCreated(e, FUR_TYPE, this);
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
			if (version > (i32)RenderModuleVersion::DECAL_UV_SCALE) {
				serializer.read(decal.uv_scale);
			}
			const char* tmp = serializer.readString();
			updateDecalInfo(decal);
			m_decals.insert(decal.entity, decal);
			setDecalMaterialPath(decal.entity, Path(tmp));
			m_world.onComponentCreated(decal.entity, DECAL_TYPE, this);
		}
	}
	
	void deserializeCurveDecals(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version)
	{
		if (version <= (i32)RenderModuleVersion::CURVE_DECALS) return;
		
		u32 count;
		serializer.read(count);
		m_curve_decals.reserve(count + m_decals.size());
		for (u32 i = 0; i < count; ++i) {
			CurveDecal decal;
			serializer.read(decal.entity);
			decal.entity = entity_map.get(decal.entity);
			serializer.read(decal.uv_scale);
			serializer.read(decal.half_extents.y);
			serializer.read(decal.bezier_p0);
			serializer.read(decal.bezier_p2);
			const char* tmp = serializer.readString();
			updateDecalInfo(decal);
			m_curve_decals.insert(decal.entity, decal);
			setCurveDecalMaterialPath(decal.entity, Path(tmp));
			m_world.onComponentCreated(decal.entity, CURVE_DECAL_TYPE, this);
		}
	}

	void serializeDecals(OutputMemoryStream& serializer)
	{
		serializer.write(m_decals.size());
		for (Decal& decal : m_decals) {
			serializer.write(decal.entity);
			serializer.write(decal.half_extents);
			serializer.write(decal.uv_scale);
			serializer.writeString(decal.material ? decal.material->getPath() : Path());
		}
	}

	void serializeCurveDecals(OutputMemoryStream& serializer)
	{
		serializer.write(m_curve_decals.size());
		for (CurveDecal& decal : m_curve_decals) {
			serializer.write(decal.entity);
			serializer.write(decal.uv_scale);
			serializer.write(decal.half_extents.y);
			serializer.write(decal.bezier_p0);
			serializer.write(decal.bezier_p2);
			serializer.writeString(decal.material ? decal.material->getPath() : Path());
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
			serializer.write(probe.flags);
			serializer.write(probe.size);
			serializer.write(probe.half_extents);
		}
	}

	void deserializeReflectionProbes(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		u32 count;
		serializer.read(count);
		m_reflection_probes.reserve(count + m_reflection_probes.size());
		for (u32 i = 0; i < count; ++i) {
			EntityRef entity;
			serializer.read(entity);
			entity = entity_map.get(entity);
			ReflectionProbe& probe = m_reflection_probes.insert(entity);
			serializer.read(probe.guid);
			serializer.read(probe.flags);
			serializer.read(probe.size);
			serializer.read(probe.half_extents);
			load(probe, entity);

			m_world.onComponentCreated(entity, REFLECTION_PROBE_TYPE, this);
		}
	}

	void load(ReflectionProbe& probe, EntityRef entity) {
		ASSERT(!probe.load_job);

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

		const Path path("probes/", probe.guid, ".lbc");
		if (probe.texture_id == 0xffFFffFF) {
			logError("There's not enough space for ", path);
			return;
		}
		
		probe.load_job = LUMIX_NEW(m_allocator, ReflectionProbe::LoadJob)(*this, entity, m_allocator);
		FileSystem::ContentCallback cb = makeDelegate<&ReflectionProbe::LoadJob::callback>(probe.load_job);
		probe.load_job->m_handle = m_engine.getFileSystem().getContent(path, cb);
	}

	void deserializeEnvironmentProbes(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		u32 count;
		serializer.read(count);
		m_environment_probes.reserve(count + m_environment_probes.size());
		const Path probe_dir("probes/");
		for (u32 i = 0; i < count; ++i) {
			EntityRef entity;
			serializer.read(entity);
			entity = entity_map.get(entity);
			EnvironmentProbe& probe = m_environment_probes.insert(entity);
			serializer.read(probe);

			m_world.onComponentCreated(entity, ENVIRONMENT_PROBE_TYPE, this);
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
			m_world.onComponentCreated(bone_attachment.entity, BONE_ATTACHMENT_TYPE, this);
		}
	}


	void deserializeParticleSystems(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) {
		const u32 count = serializer.read<u32>();
		m_particle_emitters.reserve(count + m_particle_emitters.size());
		for (u32 i = 0; i < count; ++i) {
			ParticleSystem emitter(INVALID_ENTITY, m_world, m_allocator);
			bool has_autodestroy = version > (i32)RenderModuleVersion::AUTODESTROY_EMITTER;
			bool emit_rate_removed = version > (i32)RenderModuleVersion::EMIT_RATE_REMOVED;
			emitter.deserialize(serializer, has_autodestroy, emit_rate_removed, m_engine.getResourceManager());
			emitter.m_entity = entity_map.get(emitter.m_entity);
			if (emitter.m_entity.isValid()) {
				EntityRef e = *emitter.m_entity;
				m_particle_emitters.insert(e, static_cast<ParticleSystem&&>(emitter));
				m_world.onComponentCreated(e, PARTICLE_EMITTER_TYPE, this);
			}
		}
	}


	void serializeParticleSystems(OutputMemoryStream& serializer) {
		serializer.write(m_particle_emitters.size());
		for (const ParticleSystem& emitter : m_particle_emitters) {
			emitter.serialize(serializer);
		}
	}

	void serializeProceduralGeometries(OutputMemoryStream& blob) {
		blob.write(m_procedural_geometries.size());
		for (auto iter : m_procedural_geometries.iterated()) {
			blob.write(iter.key());
			const ProceduralGeometry& pg = iter.value();
			blob.writeString(pg.material ? pg.material->getPath() : Path());
			blob.write((u32)pg.vertex_data.size());
			blob.write(pg.vertex_data.data(), pg.vertex_data.size());
			blob.write(pg.vertex_decl.attributes_count);
			blob.write(pg.vertex_decl.attributes, sizeof(pg.vertex_decl.attributes[0]) * pg.vertex_decl.attributes_count);
			blob.write(pg.vertex_decl.primitive_type);

			blob.write((u32)pg.index_data.size());
			if (pg.index_data.size() > 0) blob.write(pg.index_data.data(), pg.index_data.size());
			blob.write(pg.index_type);
		}
	}

	void serialize(OutputMemoryStream& serializer) override
	{
		serializeCameras(serializer);
		serializeModelInstances(serializer);
		serializeLights(serializer);
		serializeTerrains(serializer);
		serializeParticleSystems(serializer);
		serializeBoneAttachments(serializer);
		serializeEnvironmentProbes(serializer);
		serializeReflectionProbes(serializer);
		serializeDecals(serializer);
		serializeCurveDecals(serializer);
		serializeFurs(serializer);
		serializeInstancedModels(serializer);
		serializeProceduralGeometries(serializer);
	}


	void deserializeCameras(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version)
	{
		u32 size;
		serializer.read(size);
		m_cameras.reserve(size + m_cameras.size());
		for (u32 i = 0; i < size; ++i)
		{
			Camera camera;
			serializer.read(camera.entity);
			serializer.read(camera.fov);
			serializer.read(camera.near);
			serializer.read(camera.far);
			serializer.read(camera.ortho_size);
			serializer.read(camera.screen_width);
			serializer.read(camera.screen_height);
			serializer.read(camera.is_ortho);
			if (version > (i32)RenderModuleVersion::POSTPROCESS) {
				serializer.read(camera.film_grain_intensity);
				serializer.read(camera.dof_enabled);
				serializer.read(camera.dof_distance);
				serializer.read(camera.dof_range);
				serializer.read(camera.dof_max_blur_size);
				serializer.read(camera.dof_sharp_range);
			}
			else {
				u8 padding[3];
				serializer.read(padding);
			}

			camera.entity = entity_map.get(camera.entity);

			m_cameras.insert(camera.entity, camera);
			m_world.onComponentCreated(camera.entity, CAMERA_TYPE, this);
			if (!m_active_camera.isValid()) m_active_camera = camera.entity;
		}
	}

	void deserializeModelInstancesOld(InputMemoryStream& serializer, const EntityMap& entity_map)
	{
		PROFILE_FUNCTION();
		u32 size = 0;
		serializer.read(size);
		m_model_instances.reserve(nextPow2(size + m_model_instances.size()));
		for (u32 i = 0; i < size; ++i) {
			ModelInstance::Flags flags;
			serializer.read(flags);

			if(flags & ModelInstance::VALID) {
				const EntityRef e = entity_map.get(EntityRef{(i32)i});

				while (e.index >= m_model_instances.size()) {
					m_model_instances.emplace();
				}

				ModelInstance& r = m_model_instances[e.index];
				r.flags = flags;

				const char* path = serializer.readString();
				if (path[0] != 0) {
					Model* model = m_engine.getResourceManager().load<Model>(Path(path));
					setModel(e, model);
				}

				const char* mat_path = serializer.readString();
				if (mat_path[0] != 0) {
					setModelInstanceMaterialOverride(e, 0, Path(mat_path));
				}

				m_world.onComponentCreated(e, MODEL_INSTANCE_TYPE, this);
			}
		}
	}
	void deserializeModelInstances(InputMemoryStream& serializer, const EntityMap& entity_map, RenderModuleVersion version)
	{
		PROFILE_FUNCTION();
		u32 size = 0;
		serializer.read(size);
		const char* paths = (const char*)serializer.skip(size);

		serializer.read(size);
		m_model_instances.reserve(nextPow2(size + m_model_instances.size()));
		for (u32 i = 0; i < size; ++i) {
			ModelInstance::Flags flags;
			serializer.read(flags);

			if(flags & ModelInstance::VALID) {
				const EntityRef e = entity_map.get(EntityRef{(i32)i});

				while (e.index >= m_model_instances.size()) {
					m_model_instances.emplace();
				}

				ModelInstance& r = m_model_instances[e.index];
				r.flags = flags;

				const u32 path_offset = serializer.read<u32>();
				if (path_offset != 0xffFFffFF) {
					Model* model = m_engine.getResourceManager().load<Model>(Path(paths + path_offset));
					setModel(e, model);
				}

				if (version > RenderModuleVersion::MATERIAL_OVERRIDE) {
					u32 num_elems = serializer.read<u32>();
					for (u32 mesh_idx = 0; mesh_idx < num_elems; ++mesh_idx) {
						const char* path = serializer.readString();
						setModelInstanceMaterialOverride(e, mesh_idx, Path(path));
					}
				}
				else {
					const char* mat_path = serializer.readString();
					if (mat_path[0] != 0) {
						setModelInstanceMaterialOverride(e, 0, Path(mat_path));
					}
				}

				m_world.onComponentCreated(e, MODEL_INSTANCE_TYPE, this);
			}
		}
	}

	void deserializeLights(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) {
		u32 size = 0;
		serializer.read(size);
		m_point_lights.reserve(size + m_point_lights.size());
		for (u32 i = 0; i < size; ++i) {
			PointLight light;
			serializer.read(light);
			light.entity = entity_map.get(light.entity);
			m_point_lights.insert(light.entity, light);
			const DVec3 pos = m_world.getPosition(light.entity);
			m_culling_system->add(light.entity, (u8)RenderableTypes::LOCAL_LIGHT, pos, light.range);
			m_world.onComponentCreated(light.entity, POINT_LIGHT_TYPE, this);
		}

		serializer.read(size);
		for (u32 i = 0; i < size; ++i) {
			Environment env;
			serializer.read(env.light_color);
			serializer.read(env.direct_intensity);
			serializer.read(env.indirect_intensity);
			serializer.read(env.entity);
			serializer.read(env.cascades);
			serializer.read(env.flags);
			if (version > (i32)RenderModuleVersion::POSTPROCESS) {
				const char* sky_path = serializer.readString();
				if (sky_path[0]) {
					env.cubemap_sky = m_engine.getResourceManager().load<Texture>(Path(sky_path));
				}
				serializer.read(env.sky_intensity);
				serializer.read(env.scatter_rayleigh);
				serializer.read(env.scatter_mie);
				serializer.read(env.absorb_mie);
				serializer.read(env.sunlight_color);
				serializer.read(env.fog_scattering);
				serializer.read(env.sunlight_strength);
				serializer.read(env.height_distribution_rayleigh);
				serializer.read(env.height_distribution_mie);
				serializer.read(env.ground_r);
				serializer.read(env.atmo_r);
				serializer.read(env.fog_top);
				serializer.read(env.atmo_enabled);
				if (version <= (i32)RenderModuleVersion::FOG_DENSITY) {
					bool fog_enabled;
					serializer.read(fog_enabled);
					env.fog_density = fog_enabled ? 1.f : 0.0f;
				}
				serializer.read(env.godrays_enabled);
				if (version > (i32)RenderModuleVersion::CLOUDS) {
					serializer.read(env.clouds_enabled);
					serializer.read(env.clouds_top);
					serializer.read(env.clouds_bottom);
				}
				if (version > (i32)RenderModuleVersion::FOG_DENSITY) {
					serializer.read(env.fog_density);
				}
			}

			env.entity = entity_map.get(env.entity);
			m_environments.insert(env.entity, env);
			m_world.onComponentCreated(env.entity, ENVIRONMENT_TYPE, this);
		}
		
		EntityPtr tmp;
		serializer.read(tmp);
		if (!m_active_global_light_entity.isValid()) {
			m_active_global_light_entity = entity_map.get(tmp);
		}
	}

	void deserializeProceduralGeometries(InputMemoryStream& blob, const EntityMap& entity_map, i32 version) {
		if (version <= (i32)RenderModuleVersion::SPLINES) return;

		const u32 count = blob.read<u32>();
		for (u32 i = 0; i < count; ++i) {
			EntityRef e = blob.read<EntityRef>();
			e = entity_map.get(e);
			ProceduralGeometry pg(m_allocator);
			const char* mat_path = blob.readString();
			pg.material = mat_path[0] ? m_engine.getResourceManager().load<Material>(Path(mat_path)) : nullptr;
			const u32 data_size = blob.read<u32>();
			pg.vertex_data.resize(data_size);
			blob.read(pg.vertex_data.getMutableData(), pg.vertex_data.size());
			blob.read(pg.vertex_decl.attributes_count);
			blob.read(pg.vertex_decl.attributes, pg.vertex_decl.attributes_count * sizeof(pg.vertex_decl.attributes[0]));
			if (version > (i32)RenderModuleVersion::PROCEDURAL_GEOMETRY_PRIMITIVE_TYPE) {
				blob.read(pg.vertex_decl.primitive_type);
			}
			if (version > (i32)RenderModuleVersion::PROCEDURAL_GEOMETRY_INDEX_BUFFER) {
				u32 index_buffer_size;
				blob.read(index_buffer_size);
				if (index_buffer_size > 0) {
					pg.index_data.resize(index_buffer_size);
					blob.read(pg.index_data.getMutableData(), pg.index_data.size());
				}
				blob.read(pg.index_type);
			}
			pg.vertex_decl.computeHash();
			if (!pg.vertex_data.empty()) {
				const Renderer::MemRef mem = m_renderer.copy(pg.vertex_data.data(), (u32)pg.vertex_data.size());
				pg.vertex_buffer = m_renderer.createBuffer(mem, gpu::BufferFlags::IMMUTABLE, "pg_vb");			
			}
			if (!pg.index_data.empty()) {
				const Renderer::MemRef mem = m_renderer.copy(pg.index_data.data(), (u32)pg.index_data.size());
				pg.index_buffer = m_renderer.createBuffer(mem, gpu::BufferFlags::IMMUTABLE, "pg_ib");			
			}
			computeAABB(pg);
			m_procedural_geometries.insert(e, static_cast<ProceduralGeometry&&>(pg));
			m_world.onComponentCreated(e, PROCEDURAL_GEOM_TYPE, this);
		}
	}

	void deserializeTerrains(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) {
		i32 size = 0;
		serializer.read(size);
		for (int i = 0; i < size; ++i)
		{
			EntityRef entity;
			serializer.read(entity);
			entity = entity_map.get(entity);
			auto* terrain = LUMIX_NEW(m_allocator, Terrain)(m_renderer, entity, *this, m_allocator);
			terrain->deserialize(entity, serializer, m_world, *this, version);
			m_terrains.insert(entity, terrain);
		}
	}


	void deserialize(InputMemoryStream& serializer, const EntityMap& entity_map, i32 version) override
	{
		deserializeCameras(serializer, entity_map, version);
		if (version > (i32)RenderModuleVersion::SMALLER_MODEL_INSTANCES) {
			deserializeModelInstances(serializer, entity_map, (RenderModuleVersion)version);
		}
		else {
			deserializeModelInstancesOld(serializer, entity_map);
		}
		deserializeLights(serializer, entity_map, version);
		deserializeTerrains(serializer, entity_map, version);
		deserializeParticleSystems(serializer, entity_map, version);
		deserializeBoneAttachments(serializer, entity_map);
		deserializeEnvironmentProbes(serializer, entity_map);
		deserializeReflectionProbes(serializer, entity_map);
		deserializeDecals(serializer, entity_map, version);
		deserializeCurveDecals(serializer, entity_map, version);
		deserializeFurs(serializer, entity_map);
		deserializeInstancedModels(serializer, entity_map, version);
		if (version <= (i32)RenderModuleVersion::REMOVED_SPLINE_GEOMETRY && version > (i32)RenderModuleVersion::SPLINES) {
			u32 count;
			serializer.read(count);
			ASSERT(count == 0);
		}
		deserializeProceduralGeometries(serializer, entity_map, version);
	}


	void destroyBoneAttachment(EntityRef entity)
	{
		const BoneAttachment& bone_attachment = m_bone_attachments[entity];
		const EntityPtr parent_entity = bone_attachment.parent_entity;
		if (parent_entity.isValid() && parent_entity.index < m_model_instances.size())
		{
			ModelInstance& mi = m_model_instances[bone_attachment.parent_entity.index];
			mi.flags &= ~ModelInstance::IS_BONE_ATTACHMENT_PARENT;
		}
		m_bone_attachments.erase(entity);
		m_world.onComponentDestroyed(entity, BONE_ATTACHMENT_TYPE, this);
	}
	
	void destroyReflectionProbe(EntityRef entity)
	{
		ReflectionProbe& probe = m_reflection_probes[entity];
		LUMIX_DELETE(m_allocator, probe.load_job);
		m_reflection_probes.erase(entity);
		m_world.onComponentDestroyed(entity, REFLECTION_PROBE_TYPE, this);
	}

	void destroyEnvironmentProbe(EntityRef entity)
	{
		m_environment_probes.erase(entity);
		m_world.onComponentDestroyed(entity, ENVIRONMENT_PROBE_TYPE, this);
	}

	InstancedModel& beginInstancedModelEditing(EntityRef entity) override {
		return m_instanced_models[entity];
	}

	void endInstancedModelEditing(EntityRef entity) override {
		m_instanced_models[entity].dirty = true;
	}

	void initInstancedModelGPUData(EntityRef entity) override {
		PROFILE_FUNCTION();
		InstancedModel& im = m_instanced_models[entity];
		if (im.gpu_data) {
			if (im.gpu_capacity < (u32)im.instances.size()) {
				m_renderer.getEndFrameDrawStream().destroy(im.gpu_data);
				im.gpu_data = gpu::INVALID_BUFFER;
				im.gpu_capacity = 0;
			}
		}

		// grid aabb
		im.grid.aabb = AABB(Vec3(FLT_MAX), Vec3(-FLT_MAX));
		for (const InstancedModel::InstanceData& id : im.instances) {
			im.grid.aabb.addPoint(id.pos);
		}

		// cells aabb
		const Vec2 cell_size = (im.grid.aabb.max.xz() - im.grid.aabb.min.xz()) * 0.25f;
		for (u32 j = 0; j < 4; ++j) {
			for (u32 i = 0; i < 4; ++i) {
				InstancedModel::Grid::Cell& cell = im.grid.cells[i + j * 4];
				cell.from_instance = 0;
				cell.instance_count = 0;
				cell.aabb.min.x = im.grid.aabb.min.x + cell_size.x * i;
				cell.aabb.min.y = im.grid.aabb.min.y;
				cell.aabb.min.z = im.grid.aabb.min.z + cell_size.y * j;
				cell.aabb.max.x = cell.aabb.min.x + cell_size.x;
				cell.aabb.max.y = im.grid.aabb.max.y;
				cell.aabb.max.z = cell.aabb.min.z + cell_size.y;
				cell.aabb.shrink(-0.01f);
			}
		}

		// count
		for (const InstancedModel::InstanceData& id : im.instances) {
			for (u32 i = 0; i < 16; ++i) {
				if (im.grid.cells[i].aabb.contains(id.pos)) {
					++im.grid.cells[i].instance_count;
					break;
				}
			}
		}

		// offsets
		for (u32 i = 1; i < 16; ++i) {
			im.grid.cells[i].from_instance = im.grid.cells[i - 1].instance_count + im.grid.cells[i - 1].from_instance;
		}

		for (u32 i = 0; i < 16; ++i) im.grid.cells[i].instance_count = 0;

		// scatter
		Array<InstancedModel::InstanceData> tmp(m_allocator);
		tmp.resize(im.instances.size());
		for (const InstancedModel::InstanceData& id : im.instances) {
			for (u32 i = 0; i < 16; ++i) {
				if (im.grid.cells[i].aabb.contains(id.pos)) {
					tmp[im.grid.cells[i].instance_count + im.grid.cells[i].from_instance] = id;
					++im.grid.cells[i].instance_count;
					break;
				}
			}
		}
		im.instances.swap(tmp);

		if (!im.instances.empty()) {
			if (im.gpu_data) {
				Renderer::MemRef mem = m_renderer.copy(im.instances.begin(), im.instances.byte_size());
				DrawStream& stream = m_renderer.getDrawStream();
				stream.update(im.gpu_data, mem.data, mem.size);
				stream.freeMemory(mem.data, m_renderer.getAllocator());
			}
			else {
				Renderer::MemRef mem = m_renderer.copy(im.instances.begin(), im.instances.capacity() * sizeof(im.instances[0]));
				im.gpu_data = m_renderer.createBuffer(mem, gpu::BufferFlags::SHADER_BUFFER, "instances");
				im.gpu_capacity = im.instances.capacity();
			}
		}

		im.dirty = false;
	}

	void destroyInstancedModel(EntityRef entity) {
		Model* m = m_instanced_models[entity].model;
		if (m) m->decRefCount();
		if (m_instanced_models[entity].gpu_data) m_renderer.getEndFrameDrawStream().destroy(m_instanced_models[entity].gpu_data);
		m_instanced_models.erase(entity);
		m_world.onComponentDestroyed(entity, INSTANCED_MODEL_TYPE, this);
	}

	void destroyModelInstance(EntityRef entity) {
		auto& model_instance = m_model_instances[entity.index];
		setModel(entity, nullptr);
		model_instance = {};
		m_world.onComponentDestroyed(entity, MODEL_INSTANCE_TYPE, this);
	}

	void destroyEnvironment(EntityRef entity) {
		if ((EntityPtr)entity == m_active_global_light_entity) {
			m_active_global_light_entity = INVALID_ENTITY;
		}

		Environment& env = m_environments[entity];
		if (env.cubemap_sky) env.cubemap_sky->decRefCount();
		m_environments.erase(entity);

		m_world.onComponentDestroyed(entity, ENVIRONMENT_TYPE, this);
	}

	void destroyFur(EntityRef entity) {
		m_furs.erase(entity);
		m_world.onComponentDestroyed(entity, FUR_TYPE, this);
	}

	void destroyDecal(EntityRef entity)
	{
		m_culling_system->remove(entity);
		m_decals.erase(entity);
		m_world.onComponentDestroyed(entity, DECAL_TYPE, this);
	}

	void destroyCurveDecal(EntityRef entity)
	{
		m_culling_system->remove(entity);
		m_curve_decals.erase(entity);
		m_world.onComponentDestroyed(entity, CURVE_DECAL_TYPE, this);
	}

	void destroyPointLight(EntityRef entity)
	{
		m_point_lights.erase(entity);
		m_culling_system->remove(entity);
		m_world.onComponentDestroyed(entity, POINT_LIGHT_TYPE, this);
	}


	void destroyCamera(EntityRef entity)
	{
		m_cameras.erase(entity);
		m_world.onComponentDestroyed(entity, CAMERA_TYPE, this);
		if (m_active_camera == entity) m_active_camera = INVALID_ENTITY;
	}


	void destroyTerrain(EntityRef entity)
	{
		LUMIX_DELETE(m_allocator, m_terrains[entity]);
		m_terrains.erase(entity);
		m_world.onComponentDestroyed(entity, TERRAIN_TYPE, this);
	}


	void destroyParticleSystem(EntityRef entity)
	{
		const ParticleSystem& emitter = m_particle_emitters[entity];
		m_world.onComponentDestroyed(*emitter.m_entity, PARTICLE_EMITTER_TYPE, this);
		m_particle_emitters.erase(*emitter.m_entity);
	}


	void createFur(EntityRef entity) {
		m_furs.insert(entity, {});
		m_world.onComponentCreated(entity, FUR_TYPE, this);
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
		m_world.onComponentCreated(entity, CAMERA_TYPE, this);

		if (!m_active_camera.isValid()) m_active_camera = entity;
	}


	void createTerrain(EntityRef entity)
	{
		Terrain* terrain = LUMIX_NEW(m_allocator, Terrain)(m_renderer, entity, *this, m_allocator);
		m_terrains.insert(entity, terrain);
		m_world.onComponentCreated(entity, TERRAIN_TYPE, this);
	}


	void createParticleSystem(EntityRef entity)
	{
		m_particle_emitters.insert(entity, ParticleSystem(entity, m_world, m_allocator));
		m_world.onComponentCreated(entity, PARTICLE_EMITTER_TYPE, this);
	}

	Path getSkyTexturePath(EntityRef entity)	const {
		return m_environments[entity].cubemap_sky ? m_environments[entity].cubemap_sky->getPath() : Path();
	}

	void setSkyTexturePath(EntityRef entity, const Path& path) {
		auto& env = m_environments[entity];
		if (env.cubemap_sky) env.cubemap_sky->decRefCount();
		env.cubemap_sky = path.isEmpty() ? nullptr : m_engine.getResourceManager().load<Texture>(path);
	}

	bool getEnvironmentCastShadows(EntityRef entity) override {
		return m_environments[entity].flags & Environment::CAST_SHADOWS;
	}
	
	void setEnvironmentCastShadows(EntityRef entity, bool enable) override {
		setFlag(m_environments[entity].flags, Environment::CAST_SHADOWS, enable);
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


	void onEntityDestroyed(EntityRef entity) {
		for (auto& i : m_bone_attachments) {
			if (i.parent_entity == entity) {
				i.parent_entity = INVALID_ENTITY;
				break;
			}
		}
	}

	void onBoneAttachmentMoved(EntityRef entity) {
		updateRelativeMatrix(m_bone_attachments[entity]);
// TODO update bont attachment's relative matrix if the attachment is moved not by moving its parent
#if 0
		if (m_is_updating_attachments || m_is_game_running) return;
		
		if(m_world.hasComponent(entity, BONE_ATTACHMENT_TYPE)) {
			for (auto& attachment : m_bone_attachments)
			{
				if (attachment.entity == entity)
				{
					updateRelativeMatrix(attachment);
					break;
				}
			}
		}
#endif
	}

	void onModelInstanceMoved(EntityRef entity) {
		if (!m_culling_system->isAdded(entity)) return;
		
		const Transform& tr = m_world.getTransform(entity);
		ModelInstance& mi = m_model_instances[entity.index];
		m_moved_instances.push(entity);
		mi.flags |= ModelInstance::MOVED;
		const Model* model = mi.model;
		ASSERT(model);
		const float bounding_radius = model->getOriginBoundingRadius();
		m_culling_system->set(entity, tr.pos, bounding_radius * maximum(tr.scale.x, tr.scale.y, tr.scale.z));

		if (mi.flags & ModelInstance::IS_BONE_ATTACHMENT_PARENT) {
			for (auto& attachment : m_bone_attachments) {
				if (attachment.parent_entity == entity) {
					EntityPtr backup = m_updating_attachment;
					m_updating_attachment = attachment.entity;
					updateBoneAttachment(attachment);
					m_updating_attachment = backup;
				}
			}
		}
	}

	void onDecalMoved(EntityRef entity) {
		if (!m_culling_system->isAdded(entity)) return;

		auto iter = m_decals.find(entity);
		updateDecalInfo(iter.value());
		const DVec3 position = m_world.getPosition(entity);
		m_culling_system->setPosition(entity, position);
	}


	void onCurveDecalMoved(EntityRef entity) {
		if (!m_culling_system->isAdded(entity)) return;

		auto iter = m_curve_decals.find(entity);
		updateDecalInfo(iter.value());
		const DVec3 position = m_world.getPosition(entity);
		m_culling_system->setPosition(entity, position);
	}

	void onPointLightMoved(EntityRef entity) {
		if (!m_culling_system->isAdded(entity)) return;

		const DVec3 pos = m_world.getPosition(entity);
		m_culling_system->setPosition(entity, pos);
	}

	void onParticleEmitterMoved(EntityRef entity) {
		m_particle_emitters[entity].applyTransform(m_world.getTransform(entity));
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
		if (path.isEmpty()) {
			m_terrains[entity]->setMaterial(nullptr);
		} else {
			Material* material = m_engine.getResourceManager().load<Material>(path);
			m_terrains[entity]->setMaterial(material);
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

	Decal& getDecal(EntityRef entity) override {
		return m_decals[entity];
	}

	CurveDecal& getCurveDecal(EntityRef entity) override {
		return m_curve_decals[entity];
	}

	void setCurveDecalMaterialPath(EntityRef entity, const Path& path) override {
		CurveDecal& decal = m_curve_decals[entity];
		if (decal.material) {
			removeFromMaterialCurveDecalMap(decal.material, entity);
			decal.material->decRefCount();
		}

		m_culling_system->remove(entity);
		if (path.isEmpty()) {
			decal.material = nullptr;
		}
		else {
			decal.material = m_engine.getResourceManager().load<Material>(path);
			addToMaterialCurveDecalMap(decal.material, entity);

			if (decal.material->isReady()) {
				const float radius = length(m_curve_decals[entity].half_extents);
				const DVec3 pos = m_world.getPosition(entity);
				m_culling_system->add(entity, (u8)RenderableTypes::CURVE_DECAL, pos, radius);
			}
		}
	}

	Path getCurveDecalMaterialPath(EntityRef entity) override {
		CurveDecal& decal = m_curve_decals[entity];
		return decal.material ? decal.material->getPath() : Path("");
	}
	
	void setCurveDecalHalfExtents(EntityRef entity, float value) override
	{
		CurveDecal& decal = m_curve_decals[entity];
		decal.half_extents.y = value;
		updateDecalInfo(decal);
		if (decal.material && decal.material->isReady()) {
			m_culling_system->setRadius(entity, decal.radius);
		}
	}

	void setCurveDecalBezierP0(EntityRef entity, const Vec2& value) override {
		CurveDecal& decal = m_curve_decals[entity];
		decal.bezier_p0 = value;
		updateDecalInfo(decal);
		if (decal.material && decal.material->isReady()) {
			m_culling_system->setRadius(entity, decal.radius);
		}
	}

	void setCurveDecalUVScale(EntityRef entity, const Vec2& value) override {
		m_curve_decals[entity].uv_scale = value;
		updateDecalInfo(m_curve_decals[entity]);
	}

	Vec2 getCurveDecalBezierP0(EntityRef entity) override {
		return m_curve_decals[entity].bezier_p0;
	}

	void setCurveDecalBezierP2(EntityRef entity, const Vec2& value) override {
		CurveDecal& decal = m_curve_decals[entity];
		decal.bezier_p2 = value;
		updateDecalInfo(decal);
		if (decal.material && decal.material->isReady()) {
			m_culling_system->setRadius(entity, decal.radius);
		}
	}

	Vec2 getCurveDecalBezierP2(EntityRef entity) override {
		return m_curve_decals[entity].bezier_p2;
	}

	Vec2 getCurveDecalUVScale(EntityRef entity) override {
		return m_curve_decals[entity].uv_scale;
	}

	float getCurveDecalHalfExtents(EntityRef entity) override
	{
		return m_curve_decals[entity].half_extents.y;
	}

	void setDecalMaterialPath(EntityRef entity, const Path& path) override
	{
		Decal& decal = m_decals[entity];
		if (decal.material) {
			removeFromMaterialDecalMap(decal.material, entity);
			decal.material->decRefCount();
		}

		m_culling_system->remove(entity);
		if (path.isEmpty()) {
			decal.material = nullptr;
		}
		else {
			decal.material = m_engine.getResourceManager().load<Material>(path);
			addToMaterialDecalMap(decal.material, entity);

			if (decal.material->isReady()) {
				const float radius = length(m_decals[entity].half_extents);
				const DVec3 pos = m_world.getPosition(entity);
				m_culling_system->add(entity, (u8)RenderableTypes::DECAL, pos, radius);
			}
		}
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

	void setTerrainBaseGridResolution(EntityRef entity, u32 value) override { 
		m_terrains[entity]->m_base_grid_res = (maximum(8, value) + 1) & ~1;
	}

	u32 getTerrainBaseGridResolution(EntityRef entity) override { 
		return m_terrains[entity]->m_base_grid_res; 
	}

	void setTerrainTesselation(EntityRef entity, u32 value) override {
		m_terrains[entity]->m_tesselation = maximum(1, value);
	}

	u32 getTerrainTesselation(EntityRef entity) override {
		return m_terrains[entity]->m_tesselation;
	}

	void setTerrainYScale(EntityRef entity, float scale) override
	{
		m_terrains[entity]->setYScale(scale);
	}


	float getTerrainYScale(EntityRef entity) override { return m_terrains[entity]->getYScale(); }

	void getProceduralGeometryBlob(EntityRef entity, OutputMemoryStream& value) {
		const ProceduralGeometry& pg = m_procedural_geometries[entity];
		value.write(pg.vertex_decl);
		value.write(pg.index_type);
		value.write((u32)pg.vertex_data.size());
		if (!pg.vertex_data.empty()) value.write(pg.vertex_data.data(), pg.vertex_data.size());
		value.write((u32)pg.index_data.size());
		if (!pg.index_data.empty()) value.write(pg.index_data.data(), pg.index_data.size());
	}

	void setProceduralGeometryBlob(EntityRef entity, InputMemoryStream& value) {
		ProceduralGeometry& pg = m_procedural_geometries[entity];
		pg.index_data.clear();
		pg.vertex_data.clear();
		if (pg.vertex_buffer) {
			m_renderer.getEndFrameDrawStream().destroy(pg.vertex_buffer);
			pg.vertex_buffer = gpu::INVALID_BUFFER;
		}
		if (pg.index_buffer) {
			m_renderer.getEndFrameDrawStream().destroy(pg.index_buffer);
			pg.index_buffer = gpu::INVALID_BUFFER;
		}
		
		value.read(pg.vertex_decl);
		value.read(pg.index_type);
		u32 size = value.read<u32>();
		if (size > 0) {
			pg.vertex_data.resize(size);
			value.read(pg.vertex_data.getMutableData(), pg.vertex_data.size());
			const Renderer::MemRef mem = m_renderer.copy(pg.vertex_data.data(), (u32)pg.vertex_data.size());
			pg.vertex_buffer = m_renderer.createBuffer(mem, gpu::BufferFlags::IMMUTABLE, "pg_vb");
		}

		size = value.read<u32>();
		if (size > 0) {
			pg.index_data.resize(size);
			value.read(pg.index_data.getMutableData(), pg.index_data.size());
			const Renderer::MemRef mem = m_renderer.copy(pg.index_data.data(), (u32)pg.index_data.size());
			pg.index_buffer = m_renderer.createBuffer(mem, gpu::BufferFlags::IMMUTABLE, "pg_ib");
		}
	}

	static void computeAABB(ProceduralGeometry& pg) {
		pg.aabb.min = Vec3(FLT_MAX);
		pg.aabb.max = Vec3(-FLT_MAX);

		if (pg.vertex_decl.attributes_count == 0) return;

		const u32 stride = pg.vertex_decl.getStride();
		const u32 vertex_count = u32(pg.vertex_data.size() / stride);
		const u8* data = pg.vertex_data.data();

		for (u32 i = 0; i < vertex_count; ++i) {
			Vec3 p;
			memcpy(&p, data + stride * i, sizeof(p));
			pg.aabb.addPoint(p);
		}
	}

	void setProceduralGeometry(EntityRef entity
		, Span<const u8> vertex_data
		, const gpu::VertexDecl& vertex_decl
		, Span<const u8> indices
		, gpu::DataType index_type) override
	{
		PROFILE_FUNCTION();
		ProceduralGeometry& pg = m_procedural_geometries[entity];
		pg.vertex_decl = vertex_decl;
		pg.vertex_data.clear();
		pg.index_data.clear();
		pg.index_type = index_type;
		pg.vertex_data.write(vertex_data.begin(), vertex_data.length());
		
		if (pg.index_buffer) m_renderer.getEndFrameDrawStream().destroy(pg.index_buffer);
		if (pg.vertex_buffer) m_renderer.getEndFrameDrawStream().destroy(pg.vertex_buffer);
		
		if (indices.length() > 0) {
			pg.index_data.write(indices.begin(), indices.length());
			const Renderer::MemRef mem = m_renderer.copy(indices.begin(), indices.length());
			pg.index_buffer = m_renderer.createBuffer(mem, gpu::BufferFlags::IMMUTABLE, "pg_ib");
		}
		
		const Renderer::MemRef mem = m_renderer.copy(vertex_data.begin(), vertex_data.length());
		pg.vertex_buffer = m_renderer.createBuffer(mem, gpu::BufferFlags::IMMUTABLE, "pg_vb");
		computeAABB(pg);
	}
	
	ProceduralGeometry& getProceduralGeometry(EntityRef e) override {
		return m_procedural_geometries[e];
	}
	
	const HashMap<EntityRef, ProceduralGeometry>& getProceduralGeometries() override {
		return m_procedural_geometries;
	}

	Path getProceduralGeometryMaterial(EntityRef entity) override {
		Material* mat = m_procedural_geometries[entity].material;
		return mat ? mat->getPath() : Path();
	}

	void setProceduralGeometryMaterial(EntityRef entity, const Path& path) override {
		ProceduralGeometry& pg = m_procedural_geometries[entity];
		if (pg.material) {
			if (pg.material->getPath() == path) return;
			pg.material->decRefCount();
			pg.material = nullptr;
		}

		pg.material = path.isEmpty() ? nullptr : m_engine.getResourceManager().load<Material>(path);
	}

	Pose* lockPose(EntityRef entity) override { return m_model_instances[entity.index].pose; }

	void unlockPose(EntityRef entity, bool changed) override {
		if (!changed) return;
		if (entity.index < m_model_instances.size()
			&& (m_model_instances[entity.index].flags & ModelInstance::IS_BONE_ATTACHMENT_PARENT) == 0)
		{
			return;
		}

		EntityRef parent = entity;
		for (BoneAttachment& ba : m_bone_attachments)
		{
			if (ba.parent_entity != parent) continue;
			EntityPtr backup = m_updating_attachment;
			m_updating_attachment = ba.entity;
			updateBoneAttachment(ba);
			m_updating_attachment = backup;
		}
	}

	bool overrideMaterialVec4(EntityRef entity, u32 mesh_index, const char* uniform_name, Vec4 value) {
		ModelInstance& inst = m_model_instances[entity.index];
		if (!inst.model->isReady()) return false;

		ensureMaterialDataSize(inst, mesh_index + 1);

		MeshMaterial& mat = inst.mesh_materials[mesh_index];
		if (!mat.material || !mat.material->isReady()) return false;
		
		if (mat.material_index == 0 || !isFlagSet(mat.flags, MeshMaterial::OWN_MATERIAL_INDEX)) {
			float tmp[Material::MAX_UNIFORMS_FLOATS];
			mat.material->getUniformData(Span(tmp));
			mat.material_index = m_renderer.createMaterialInstance(tmp);
			mat.flags = MeshMaterial::OWN_MATERIAL_INDEX;
		}

		Shader* shader = mat.material->getShader();
		for (const auto& u : shader->m_uniforms) {
			if (equalStrings(u.name, uniform_name)) {
				m_renderer.updateMaterialConstants(mat.material_index, Span(&value.x, 4), u.offset);
				return true;
			}
		}
		return false;
	}

	Model* getModelInstanceModel(EntityRef entity) override { return m_model_instances[entity.index].model; }

	bool isModelInstanceEnabled(EntityRef entity) override
	{
		ModelInstance& model_instance = m_model_instances[entity.index];
		return model_instance.flags & ModelInstance::ENABLED;
	}


	void enableModelInstance(EntityRef entity, bool enable) override
	{
		ModelInstance& model_instance = m_model_instances[entity.index];
		setFlag(model_instance.flags, ModelInstance::ENABLED, enable);
		if (enable)
		{
			if (!model_instance.model || !model_instance.model->isReady()) return;

			const DVec3 pos = m_world.getPosition(entity);
			const Vec3& scale = m_world.getScale(entity);
			const float radius = model_instance.model->getOriginBoundingRadius() * maximum(scale.x, scale.y, scale.z);
			if (!m_culling_system->isAdded(entity)) {
				const RenderableTypes type = getRenderableType(*model_instance.model);
				m_culling_system->add(entity, (u8)type, pos, radius);
			}
		}
		else
		{
			m_culling_system->remove(entity);
		}
	}

	static bool hasMaterialOverride(const ModelInstance& m) {
		if (!m.model->isReady()) return m.mesh_materials.size() > 0;
		return &m.model->getMeshMaterial(0) != m.mesh_materials.begin();
	}

	void ensureMaterialDataSize(ModelInstance& m, u32 size) {
		bool has = hasMaterialOverride(m);
		const u32 num_existing = m.mesh_materials.size();
		if (has && num_existing >= size) return;
		
		m.dirty = true;
		MeshMaterial* new_data = (MeshMaterial*)m_allocator.allocate(sizeof(MeshMaterial) * size, alignof(MeshMaterial));
		if (has) {
			memcpy(new_data, m.mesh_materials.begin(), sizeof(new_data[0]) * num_existing);
			memset(&new_data[num_existing], 0, sizeof(new_data[0]) * (size - num_existing));
			m_allocator.deallocate(m.mesh_materials.begin());
		}
		else {
			memset(new_data, 0, sizeof(new_data[0]) * size);
			for (u32 i = 0; i < num_existing; ++i) {
				new_data[i].material = m.mesh_materials[i].material;
				new_data[i].material->incRefCount();
			}
		}
		m.mesh_materials = Span(new_data, size);
	}

	void setModelInstanceMaterialOverride(EntityRef entity, u32 mesh_idx, const Path& path) override {
		ModelInstance& mi = m_model_instances[entity.index];

		ensureMaterialDataSize(mi, mesh_idx + 1);

		MeshMaterial& mat = mi.mesh_materials[mesh_idx];
		Material* prev_mat = mat.material;
		if (mat.sort_key) m_renderer.freeSortKey(mat.sort_key);
		if (mat.flags & MeshMaterial::OWN_MATERIAL_INDEX) m_renderer.destroyMaterialConstants(mat.material_index);
		
		mat.material = path.isEmpty() ? nullptr : m_engine.getResourceManager().load<Material>(path);
		
		if (prev_mat) prev_mat->decRefCount();
		mat.sort_key = 0;
		mat.material_index = 0;
		mi.dirty = true;
	}

	Path getModelInstanceMaterialOverride(EntityRef entity, u32 mesh_idx) override {
		ModelInstance& mi = m_model_instances[entity.index];
		if (mesh_idx >= mi.mesh_materials.size() || !mi.mesh_materials[mesh_idx].material) {
			if (!mi.model->isReady()) return Path("");
			return mi.model->getMeshMaterial(mesh_idx).material->getPath();
		}

		return mi.mesh_materials[mesh_idx].material->getPath();
	}

	Path getInstancedModelPath(EntityRef entity) override {
		Model* m = m_instanced_models[entity].model;
		return m ? m->getPath() : Path();
	}

	void setInstancedModelPath(EntityRef entity, const Path& path) override {
		InstancedModel& im = m_instanced_models[entity];
		if (im.model) {
			im.model->decRefCount();
			im.model = nullptr;
		}
		if (!path.isEmpty()) {
			im.model = m_engine.getResourceManager().load<Model>(path);
		}
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
		if (path.isEmpty()) {
			setModel(entity, nullptr);
		}
		else {
			Model* model = m_engine.getResourceManager().load<Model>(path);
			setModel(entity, model);
		}
	}


	void setTerrainHeightAt(EntityRef entity, int x, int z, float height)
	{
		m_terrains[entity]->setHeight(x, z, height);
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
		Terrain* terrain = m_terrains[entity];
		terrain->setGrassTypeSpacing(index, spacing);
		terrain->setGrassDirty();
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
		Terrain* terrain = m_terrains[entity];
		terrain->removeGrassType(index);
		terrain->setGrassDirty();
	}


	EntityPtr getFirstModelInstance() override
	{
		return getNextModelInstance(INVALID_ENTITY);
	}


	EntityPtr getNextModelInstance(EntityPtr entity) override
	{
		for(int i = entity.index + 1; i < m_model_instances.size(); ++i)
		{
			if (m_model_instances[i].flags & ModelInstance::VALID) return EntityPtr{i};
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
			mtx.setPerspective(camera.fov, ratio, camera.near);
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
		Color color) override
	{
		DebugTriangle& tri = m_debug_triangles.emplace();
		tri.p0 = p0;
		tri.p1 = p1;
		tri.p2 = p2;
		tri.color = color;
	}


	void addDebugBone(const DVec3& pos,
		const Vec3& dir,
		const Vec3& up,
		const Vec3& right,
		Color color) override
	{
		addDebugLine(pos + up + right, pos + up - right, color);
		addDebugLine(pos + up + right, pos - up + right, color);
		addDebugLine(pos - up - right, pos + up - right, color);
		addDebugLine(pos - up - right, pos - up + right, color);

		addDebugLine(pos + up + right, pos + dir, color);
		addDebugLine(pos + up - right, pos + dir, color);
		addDebugLine(pos - up + right, pos + dir, color);
		addDebugLine(pos - up - right, pos + dir, color);
	}

	void addDebugCube(const DVec3& pos,
		const Vec3& dir,
		const Vec3& up,
		const Vec3& right,
		Color color) override
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
		Color color) override
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
		Color color) override
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


	void addDebugCross(const DVec3& center, float size, Color color) override
	{
		addDebugLine(center, DVec3(center.x - size, center.y, center.z), color);
		addDebugLine(center, DVec3(center.x + size, center.y, center.z), color);
		addDebugLine(center, DVec3(center.x, center.y - size, center.z), color);
		addDebugLine(center, DVec3(center.x, center.y + size, center.z), color);
		addDebugLine(center, DVec3(center.x, center.y, center.z - size), color);
		addDebugLine(center, DVec3(center.x, center.y, center.z + size), color);
	}

	void addDebugLine(const DVec3& from, const DVec3& to, Color color) override 
	{
		DebugLine& line = m_debug_lines.emplace();
		line.from = from;
		line.to = to;
		line.color = color;
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


	RayCastModelHit castRayTerrain(const Ray& ray) override
	{
		RayCastModelHit hit;
		hit.is_hit = false;
		for (Terrain* terrain : m_terrains) {
			hit = terrain->castRay(ray);
			hit.component_type = TERRAIN_TYPE;
			hit.entity = terrain->getEntity();
			if (hit.is_hit) break;
		}
		return hit;
	}

	RayCastModelHit castRay(const Ray& ray, EntityPtr ignored_model_instance) override {
		return castRay(ray, [&](const RayCastModelHit& hit) -> bool {
			return hit.entity != ignored_model_instance || !ignored_model_instance.isValid();
		});
	}
	
	RayCastModelHit castRayInstancedModels(const Ray& ray, const RayCastModelHit::Filter& filter) override {
		RayCastModelHit hit;
		hit.is_hit = false;
		for (auto iter : m_instanced_models.iterated()) {
			const EntityRef e = iter.key();
			const Transform tr = m_world.getTransform(e);
			const InstancedModel& im = iter.value();
			if (!im.model || !im.model->isReady()) continue;
			
			const float model_radius = im.model->getOriginBoundingRadius();
			auto getInstanceQuat = [](Vec3 q) {
				Quat res;
				res.x = q.x;
				res.y = q.y;
				res.z = q.z;
				res.w = sqrtf(1 - (q.x * q.x + q.y * q.y + q.z * q.z));
				return res;
			};
			for (const InstancedModel::InstanceData& id : im.instances) {
				Vec3 rel_pos = Vec3(ray.origin - tr.pos) - id.pos;
				const float radius = model_radius * id.scale;
				float intersection_t;
				if (getRaySphereIntersection(rel_pos, ray.dir, Vec3::ZERO, radius, intersection_t) && intersection_t >= 0) {
					const Quat rot = getInstanceQuat(id.rot_quat);
					const Vec3 rel_dir = rot.conjugated().rotate(ray.dir);
					rel_pos = rot.conjugated().rotate(rel_pos / id.scale);
					RayCastModelHit new_hit = im.model->castRay(rel_pos, rel_dir, nullptr, e, &filter);
					if (new_hit.is_hit && (!hit.is_hit || new_hit.t * id.scale < hit.t)) {
						new_hit.entity = e;
						new_hit.component_type = INSTANCED_MODEL_TYPE;
						hit = new_hit;
						hit.t *= id.scale;
						hit.is_hit = true;
						hit.subindex = u32(&id - im.instances.begin());
					}
				}
			}
		}
		return hit;
	}
	
	RayCastModelHit castRayProceduralGeometry(const Ray& ray, const RayCastModelHit::Filter& filter) {
		RayCastModelHit hit;
		hit.is_hit = false;
		for (auto iter : m_procedural_geometries.iterated()) {
			const ProceduralGeometry& pg = iter.value();
			if (pg.vertex_data.empty()) continue;
			if (pg.vertex_decl.primitive_type != gpu::PrimitiveType::TRIANGLES) continue;

			const u32 stride = pg.vertex_decl.getStride();
			const u8* data = pg.vertex_data.data();
			Vec3 a, b, c;
			RayCastModelHit pg_hit;

			const Transform& tr = m_world.getTransform(iter.key());
			
			const Vec3 rd = tr.invTransformVector(ray.dir);
 			Vec3 ro = Vec3(tr.invTransform(ray.origin));

			Vec3 dummy;
			if (!pg.aabb.contains(ro) && !getRayAABBIntersection(ro, rd, pg.aabb.min, pg.aabb.max - pg.aabb.min, dummy)) continue;

			const bool is_indexed = pg.index_data.size() != 0;
			const u32 triangles = (is_indexed ? pg.getIndexCount() : u32(pg.vertex_data.size() / stride)) / 3;
			const u16* indices16 = (const u16*)pg.index_data.data();
			const u32* indices32 = (const u32*)pg.index_data.data();
			for (u32 i = 0; i < triangles * 3; i += 3) {
				float t;

				u32 tindices[3];
				if (is_indexed) {
					if (pg.index_type == gpu::DataType::U16) {
						tindices[0] = indices16[i];
						tindices[1] = indices16[i + 1];
						tindices[2] = indices16[i + 2];
					}
					else {
						tindices[0] = indices32[i];
						tindices[1] = indices32[i + 1];
						tindices[2] = indices32[i + 2];
					}
				}
				else {
					tindices[0] = i;
					tindices[1] = i + 1;
					tindices[2] = i + 2;
				}

				memcpy(&a, data + tindices[0] * stride, sizeof(a));
				memcpy(&b, data + tindices[1] * stride, sizeof(b));
				memcpy(&c, data + tindices[2] * stride, sizeof(c));
				if (getRayTriangleIntersection(ro, rd, a, b, c, &t) && (t < hit.t || !hit.is_hit)) {
					pg_hit.is_hit = true;
					pg_hit.mesh = nullptr;
					pg_hit.entity = iter.key();
					pg_hit.t = t;
					if (filter.invoke(pg_hit)) hit = pg_hit;
				}
			}
		}
		hit.origin = ray.origin;
		hit.dir = ray.dir;
		return hit;
	}


	RayCastModelHit castRay(const Ray& ray, const Delegate<bool (const RayCastModelHit&)> filter) override {
		PROFILE_FUNCTION();
		
		RayCastModelHit hit = castRayInstancedModels(ray, filter);
		double cur_dist = hit.is_hit ? hit.t : DBL_MAX;

		const World& world = getWorld();
		for (int i = 0; i < m_model_instances.size(); ++i) {
			auto& r = m_model_instances[i];
			if ((r.flags & (ModelInstance::ENABLED | ModelInstance::VALID)) == 0) continue;
			if (!r.model) continue;

			const EntityRef entity{i};
			const Transform& tr = world.getTransform(entity);
			float radius = r.model->getOriginBoundingRadius();
			const double dist = length(tr.pos - ray.origin);
			if (dist - radius * maximum(tr.scale.x, tr.scale.y, tr.scale.z) > cur_dist) continue;
			
			const Vec3 ray_origin_model_space = Vec3(tr.invTransform(ray.origin));
			const Vec3 ray_dir_model_space = normalize(tr.invTransformVector(ray.dir));

			float intersection_t;
			if (getRaySphereIntersection(ray_origin_model_space, ray_dir_model_space, Vec3::ZERO, radius, intersection_t) && intersection_t >= 0) {
				Vec3 aabb_hit;
				const AABB& aabb = r.model->getAABB();
				if (getRayAABBIntersection(ray_origin_model_space, ray_dir_model_space, aabb.min, aabb.max - aabb.min, aabb_hit)) {
					RayCastModelHit new_hit = r.model->castRay(ray_origin_model_space, ray_dir_model_space, r.pose, entity, &filter);
					if (new_hit.is_hit) {
						const Vec3 hit_pos_model_space = Vec3(new_hit.origin + new_hit.dir * new_hit.t);
						const DVec3 new_hit_pos = tr.transform(hit_pos_model_space);
						const float new_t = (float)length(ray.origin - new_hit_pos);
						if (!hit.is_hit || new_t < hit.t) {
							new_hit.entity = entity;
							new_hit.component_type = MODEL_INSTANCE_TYPE;
							hit = new_hit;
							hit.origin = ray.origin;
							hit.dir = ray.dir;
							hit.t = new_t;
							hit.is_hit = true;
							cur_dist = hit.t;
						}
					}
				}
			}
		}

		const RayCastModelHit pg_hit = castRayProceduralGeometry(ray, filter);
		if (pg_hit.is_hit && (pg_hit.t < hit.t || !hit.is_hit)) {
			hit = pg_hit;
			hit.component_type = PROCEDURAL_GEOM_TYPE;
		}

		for (auto* terrain : m_terrains) {
			RayCastModelHit terrain_hit = terrain->castRay(ray);
			if (terrain_hit.is_hit && (!hit.is_hit || terrain_hit.t < hit.t)) {
				terrain_hit.component_type = TERRAIN_TYPE;
				terrain_hit.entity = terrain->getEntity();
				terrain_hit.mesh = nullptr;
				if (filter.invoke(terrain_hit)) hit = terrain_hit;
			}
		}

		hit.origin = ray.origin;
		hit.dir = ray.dir;
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
		return m_point_lights[entity].flags & PointLight::CAST_SHADOWS;
	}

	void setPointLightCastShadows(EntityRef entity, bool value) override {
		setFlag(m_point_lights[entity].flags, PointLight::CAST_SHADOWS, value);
	}

	bool getPointLightDynamic(EntityRef entity) override {
		return m_point_lights[entity].flags & PointLight::DYNAMIC;
	}

	void setPointLightDynamic(EntityRef entity, bool value) override {
		setFlag(m_point_lights[entity].flags, PointLight::DYNAMIC, value);
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
		setFlag(m_reflection_probes[entity].flags, ReflectionProbe::ENABLED, enable);
	}

	bool isReflectionProbeEnabled(EntityRef entity) override {
		return m_reflection_probes[entity].flags & ReflectionProbe::ENABLED;
	}
	
	Span<const ReflectionProbe> getReflectionProbes() override {
		return m_reflection_probes.values();
	}
	
	gpu::TextureHandle getReflectionProbesTexture() override {
		return m_reflection_probes_texture;
	}

	void reloadReflectionProbes() override {
		for (i32 i = 0; i < m_reflection_probes.size(); ++i) {
			ReflectionProbe& probe = m_reflection_probes.at(i);
			const EntityRef e = m_reflection_probes.getKey(i);
			load(probe, e);
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
		setFlag(m_environment_probes[entity].flags, EnvironmentProbe::ENABLED, enable);
	}


	bool isEnvironmentProbeEnabled(EntityRef entity) override
	{
		return m_environment_probes[entity].flags & EnvironmentProbe::ENABLED;
	}

	void modelUnloaded(Model*, EntityRef entity) {
		ModelInstance& r = m_model_instances[entity.index];
		r.meshes = nullptr;
		r.mesh_count = 0;
		LUMIX_DELETE(m_allocator, r.pose);
		r.pose = nullptr;

		m_culling_system->remove(entity);
	}


	void modelLoaded(Model* model, EntityRef entity) {
		ASSERT(model->isReady());
		auto& r = m_model_instances[entity.index];

		float bounding_radius = r.model->getOriginBoundingRadius();
		const Vec3& scale = m_world.getScale(entity);
		const DVec3 pos = m_world.getPosition(entity);
		const float radius = bounding_radius * maximum(scale.x, scale.y, scale.z);
		if(r.flags & ModelInstance::ENABLED) {
			const RenderableTypes type = getRenderableType(*model);
			m_culling_system->add(entity, (u8)type, pos, radius);
		}
		ASSERT(!r.pose);
		if (model->getBoneCount() > 0) {
			r.pose = LUMIX_NEW(m_allocator, Pose)(m_allocator);
			r.pose->resize(model->getBoneCount());
			model->getPose(*r.pose);
		}
		r.mesh_count = r.model->getMeshCount();
		r.meshes = r.mesh_count > 0 ? &r.model->getMesh(0) : nullptr;
		if (r.mesh_materials.size() == 0) {
			r.mesh_materials = Span(&r.model->getMeshMaterial(0), r.model->getMeshCount());
		}

		if (r.flags & ModelInstance::IS_BONE_ATTACHMENT_PARENT) {
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
		
		r.dirty = r.mesh_materials.begin() != &r.model->getMeshMaterial(0);
	}

	u32 computeSortKey(const Material& material, const Mesh& mesh) const override {
		ASSERT(material.isReady());
		RollingHasher hasher;
		const Shader* shader = material.getShader();
		const u32 define_mask = material.getDefineMask();
		hasher.begin();
		const Mesh* mesh_ptr = &mesh;
		hasher.update(&mesh_ptr, sizeof(mesh_ptr));
		hasher.update(&shader, sizeof(shader));
		hasher.update(&define_mask, sizeof(define_mask));
		hasher.update(&material.m_render_states, sizeof(material.m_render_states));
		const RuntimeHash32 hash = hasher.end();
		return m_renderer.allocSortKey(hash.getHashValue());
	}


	void modelUnloaded(Model* model)
	{
		for (int i = 0, c = m_model_instances.size(); i < c; ++i)
		{
			if ((m_model_instances[i].flags & ModelInstance::VALID) && m_model_instances[i].model == model)
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
			material->getObserverCb().bind<&RenderModuleImpl::decalMaterialStateChanged>(this);
		}
	}
	
	void addToMaterialCurveDecalMap(Material* material, EntityRef entity)
	{
		CurveDecal& d = m_curve_decals[entity];
		d.prev_decal = INVALID_ENTITY;
		auto map_iter = m_material_curve_decal_map.find(material);
		if(map_iter.isValid()) {
			d.next_decal = map_iter.value();
			m_material_curve_decal_map[material] = entity;
		}
		else {
			d.next_decal = INVALID_ENTITY;
			m_material_curve_decal_map.insert(material, entity);
			material->getObserverCb().bind<&RenderModuleImpl::curveDecalMaterialStateChanged>(this);
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
			model->getObserverCb().bind<&RenderModuleImpl::modelStateChanged>(this);
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
				model->getObserverCb().unbind<&RenderModuleImpl::modelStateChanged>(this);
			}
		}
	}
	

	void removeFromMaterialCurveDecalMap(Material* material, EntityRef entity)
	{
		CurveDecal& d = m_curve_decals[entity];
		if(d.prev_decal.isValid()) {
			m_curve_decals[(EntityRef)d.prev_decal].next_decal = d.next_decal;
		}
		if(d.next_decal.isValid()) {
			m_curve_decals[(EntityRef)d.next_decal].prev_decal = d.prev_decal;
		}
		auto map_iter = m_material_curve_decal_map.find(material);
		if(map_iter.value() == entity) {
			if(d.next_decal.isValid()) {
				m_material_curve_decal_map[material] = (EntityRef)d.next_decal;
			}
			else {
				m_material_curve_decal_map.erase(material);
				material->getObserverCb().unbind<&RenderModuleImpl::curveDecalMaterialStateChanged>(this);
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
				material->getObserverCb().unbind<&RenderModuleImpl::decalMaterialStateChanged>(this);
			}
		}
	}

	void setModel(EntityRef entity, Model* model) {
		auto& r = m_model_instances[entity.index];
		r.prev_frame_transform = m_world.getTransform(entity);
		ASSERT(r.flags & ModelInstance::VALID);

		Model* old_model = r.model;
		bool no_change = model == old_model && old_model;
		if (no_change) {
			old_model->decRefCount();
			return;
		}

		if (!old_model || !old_model->isReady() || r.mesh_materials.begin() != &old_model->getMeshMaterial(0)) {
			for (MeshMaterial& m : r.mesh_materials) {
				m.material->decRefCount();
				m_renderer.freeSortKey(m.sort_key);
			}
			m_allocator.deallocate(r.mesh_materials.begin());
		}
		r.mesh_materials = {};
		
		r.meshes = nullptr;
		r.mesh_count = 0;
		LUMIX_DELETE(m_allocator, r.pose);
		r.pose = nullptr;
		r.dirty = true;

		if (old_model) {
			removeFromModelEntityMap(old_model, entity);

			if (old_model->isReady()) {
				m_culling_system->remove(entity);
			}
			old_model->decRefCount();
		}

		r.model = model;
		if (model) {
			addToModelEntityMap(model, entity);

			if (model->isReady()) {
				modelLoaded(model, entity);
			}
		}
	}

	IAllocator& getAllocator() override { return m_allocator; }

	void createEnvironment(EntityRef entity)
	{
		Environment light;
		light.flags = Environment::CAST_SHADOWS;
		light.entity = entity;
		light.light_color = Vec3(1, 1, 1);
		light.direct_intensity = 1;
		light.indirect_intensity = 1;
		light.cascades = Vec4(3, 8, 20, 60);

		if (m_environments.empty()) m_active_global_light_entity = entity;

		m_environments.insert(entity, light);
		m_world.onComponentCreated(entity, ENVIRONMENT_TYPE, this);
	}


	void createPointLight(EntityRef entity)
	{
		PointLight light;
		light.entity = entity;
		light.color = Vec3(1, 1, 1);
		light.intensity = 1;
		light.fov = degreesToRadians(360);
		light.flags = PointLight::Flags::NONE;
		light.attenuation_param = 2;
		light.range = 10;
		light.guid = randGUID();
		const DVec3 pos = m_world.getPosition(entity);
		m_point_lights.insert(entity, light);
		m_culling_system->add(entity, (u8)RenderableTypes::LOCAL_LIGHT, pos, light.range);

		m_world.onComponentCreated(entity, POINT_LIGHT_TYPE, this);
	}


	void updateDecalInfo(Decal& decal) const
	{
		decal.radius = length(decal.half_extents);
		decal.transform = m_world.getTransform(decal.entity);
	}


	void updateDecalInfo(CurveDecal& decal) const
	{
		decal.half_extents.x = maximum(fabsf(decal.bezier_p0.x), fabsf(decal.bezier_p2.x)) + decal.uv_scale.x * 0.5f;
		decal.half_extents.z = maximum(fabsf(decal.bezier_p0.y), fabsf(decal.bezier_p2.y)) + decal.uv_scale.x * 0.5f;
		decal.radius = length(decal.half_extents);
		decal.transform = m_world.getTransform(decal.entity);
	}


	void createDecal(EntityRef entity)
	{
		Decal& decal = m_decals.insert(entity);
		decal.material = nullptr;
		decal.entity = entity;
		decal.half_extents = Vec3(1, 1, 1);
		decal.uv_scale = Vec2(1);
		updateDecalInfo(decal);

		m_world.onComponentCreated(entity, DECAL_TYPE, this);
	}

	void createCurveDecal(EntityRef entity)
	{
		CurveDecal& decal = m_curve_decals.insert(entity);
		decal.material = nullptr;
		decal.entity = entity;
		decal.uv_scale = Vec2(1);
		decal.half_extents = Vec3(10);
		decal.bezier_p0 = Vec2(-1, 0);
		decal.bezier_p2 = Vec2(1, 0);
		updateDecalInfo(decal);

		m_world.onComponentCreated(entity, CURVE_DECAL_TYPE, this);
	}

	void createEnvironmentProbe(EntityRef entity)
	{
		EnvironmentProbe& probe = m_environment_probes.insert(entity);

		probe.outer_range = Vec3(9001.f);
		probe.inner_range = Vec3(4500.f);
		probe.flags |= EnvironmentProbe::ENABLED;
		memset(probe.sh_coefs, 0, sizeof(probe.sh_coefs));
		probe.sh_coefs[0] = Vec3(0.5f, 0.5f, 0.5f);

		m_world.onComponentCreated(entity, ENVIRONMENT_PROBE_TYPE, this);
	}
	
	void destroyProceduralGeometry(EntityRef entity) {
		const ProceduralGeometry& pg = m_procedural_geometries[entity];
		if (pg.material) pg.material->decRefCount();
		if (pg.vertex_buffer) m_renderer.getEndFrameDrawStream().destroy(pg.vertex_buffer);
		if (pg.index_buffer) m_renderer.getEndFrameDrawStream().destroy(pg.index_buffer);
		m_procedural_geometries.erase(entity);
		m_world.onComponentDestroyed(entity, PROCEDURAL_GEOM_TYPE, this);
	}
	
	void createProceduralGeometry(EntityRef entity) {
		ASSERT(!m_procedural_geometries.find(entity).isValid());
		m_procedural_geometries.insert(entity, ProceduralGeometry(m_allocator));
		m_world.onComponentCreated(entity, PROCEDURAL_GEOM_TYPE, this);
	}

	void createReflectionProbe(EntityRef entity)
	{
		ReflectionProbe& probe = m_reflection_probes.insert(entity);
		probe.guid = randGUID();
		probe.flags |= ReflectionProbe::ENABLED;

		m_world.onComponentCreated(entity, REFLECTION_PROBE_TYPE, this);
	}

	void createBoneAttachment(EntityRef entity)
	{
		BoneAttachment& attachment = m_bone_attachments.emplace(entity);
		attachment.entity = entity;
		attachment.parent_entity = INVALID_ENTITY;
		attachment.bone_index = -1;

		m_world.onComponentCreated(entity, BONE_ATTACHMENT_TYPE, this);
	}

	const HashMap<EntityRef, InstancedModel>& getInstancedModels() const override {
		return m_instanced_models;
	}

	void createInstancedModel(EntityRef entity) {
		InstancedModel im(m_allocator);
		m_instanced_models.insert(entity, static_cast<InstancedModel&&>(im));

		initInstancedModelGPUData(entity);
		m_world.onComponentCreated(entity, INSTANCED_MODEL_TYPE, this);
	}

	void createModelInstance(EntityRef entity) {
		while(entity.index >= m_model_instances.size()) {
			m_model_instances.emplace();
		}
		auto& r = m_model_instances[entity.index];
		ASSERT(!r.model);
		r.flags = ModelInstance::VALID | ModelInstance::ENABLED;
		m_world.onComponentCreated(entity, MODEL_INSTANCE_TYPE, this);
	}

	void updateParticleSystem(EntityRef entity, float dt) override { m_particle_emitters[entity].update(dt, m_engine.getPageAllocator()); }

	void setParticleSystemPath(EntityRef entity, const Path& path) override {
		ParticleSystemResource* res = m_engine.getResourceManager().load<ParticleSystemResource>(path);
		m_particle_emitters[entity].setResource(res);
	}

	Path getParticleSystemPath(EntityRef entity) override {
		const ParticleSystem& emitter = m_particle_emitters[entity];
		if (!emitter.getResource()) return Path("");

		return emitter.getResource()->getPath();
	}

	ParticleSystem& getParticleSystem(EntityRef e) override {
		auto iter = m_particle_emitters.find(e);
		ASSERT(iter.isValid());
		return iter.value();
	}

	const HashMap<EntityRef, ParticleSystem>& getParticleSystems() const override { return m_particle_emitters; }

	TagAllocator m_allocator;
	World& m_world;
	Renderer& m_renderer;
	Engine& m_engine;
	UniquePtr<CullingSystem> m_culling_system;

	EntityPtr m_active_global_light_entity;
	HashMap<EntityRef, PointLight> m_point_lights;
	HashMap<EntityRef, Decal> m_decals;
	HashMap<EntityRef, CurveDecal> m_curve_decals;
	Array<ModelInstance> m_model_instances;
	Array<EntityRef> m_moved_instances;
	HashMap<EntityRef, InstancedModel> m_instanced_models;
	HashMap<EntityRef, Environment> m_environments;
	HashMap<EntityRef, Camera> m_cameras;
	EntityPtr m_active_camera = INVALID_ENTITY;
	AssociativeArray<EntityRef, BoneAttachment> m_bone_attachments;
	AssociativeArray<EntityRef, EnvironmentProbe> m_environment_probes;
	AssociativeArray<EntityRef, ReflectionProbe> m_reflection_probes;
	HashMap<EntityRef, ProceduralGeometry> m_procedural_geometries;
	HashMap<EntityRef, Terrain*> m_terrains;
	HashMap<EntityRef, ParticleSystem> m_particle_emitters;
	gpu::TextureHandle m_reflection_probes_texture = gpu::INVALID_TEXTURE;

	Array<DebugTriangle> m_debug_triangles;
	Array<DebugLine> m_debug_lines;
	HashMap<EntityRef, FurComponent> m_furs;

	EntityPtr m_updating_attachment = INVALID_ENTITY;
	bool m_is_game_running;

	HashMap<Model*, EntityRef> m_model_entity_map;
	HashMap<Material*, EntityRef> m_material_decal_map;
	HashMap<Material*, EntityRef> m_material_curve_decal_map;
};

ReflectionProbe::LoadJob::~LoadJob() {
	if (m_handle.isValid()) {
		m_module.m_engine.getFileSystem().cancel(m_handle);
	}
}

void ReflectionProbe::LoadJob::callback(Span<const u8> data, bool success) {
	ReflectionProbe& probe = m_module.m_reflection_probes[m_entity];
	probe.load_job = nullptr;
	m_handle = FileSystem::AsyncHandle::invalid();

	if (!success) {
		logError("Failed to load probe ", probe.guid);
		LUMIX_DELETE(m_allocator, this);
		return;
	}

	gpu::TextureDesc desc;
	const u8* image_data = Texture::getLBCInfo(data.begin(), desc);
	if (!image_data) return;

	ASSERT(desc.depth == 1);
	ASSERT(desc.is_cubemap);

	u32 layer = probe.texture_id;
	DrawStream& stream = m_module.m_renderer.getDrawStream();
	const u32 offset = u32(image_data - data.begin());
	const Renderer::MemRef mem = m_module.m_renderer.copy(image_data, data.length() - offset);
	InputMemoryStream blob(mem.data, data.length() - offset);
	for (u32 side = 0; side < 6; ++side) {
		for (u32 mip = 0; mip < desc.mips; ++mip) {
			u32 w = maximum(desc.width >> mip, 1);
			u32 h = maximum(desc.height >> mip, 1);
			const u32 mip_size_bytes = gpu::getSize(desc.format, w, h);
			stream.update(m_module.m_reflection_probes_texture, mip, 0, 0, layer * 6 + side, w, h, desc.format, blob.skip(mip_size_bytes), mip_size_bytes);
		}
	}
	stream.freeMemory(mem.data, m_module.m_renderer.getAllocator());
	LUMIX_DELETE(m_allocator, this);
}

void RenderModule::reflect() {
	using namespace reflection;

	struct RotationModeEnum : reflection::EnumAttribute {
		u32 count(ComponentUID cmp) const override { return 2; }
		const char* name(ComponentUID cmp, u32 idx) const override {
			switch((Terrain::GrassType::RotationMode)idx) {
				case Terrain::GrassType::RotationMode::ALL_RANDOM: return "All random";
				case Terrain::GrassType::RotationMode::Y_UP: return "Y up";
				case Terrain::GrassType::RotationMode::COUNT: break;
			}
			ASSERT(false);
			return "N/A";
		}
	};

	struct BoneEnum : reflection::EnumAttribute {
		u32 count(ComponentUID cmp) const override {
			RenderModule* render_module = static_cast<RenderModule*>(cmp.module);
			EntityPtr model_instance = getModelInstance(render_module, (EntityRef)cmp.entity);
			if (!model_instance.isValid()) return 0;

			auto* model = render_module->getModelInstanceModel((EntityRef)model_instance);
			if (!model || !model->isReady()) return 0;

			return model->getBoneCount();
		}

		const char* name(ComponentUID cmp, u32 idx) const override {
			RenderModule* render_module = static_cast<RenderModule*>(cmp.module);
			EntityPtr model_instance = getModelInstance(render_module, (EntityRef)cmp.entity);
			if (!model_instance.isValid()) return "";

			auto* model = render_module->getModelInstanceModel((EntityRef)model_instance);
			if (!model) return "";

			return idx < (u32)model->getBoneCount() ? model->getBone(idx).name.c_str() : "N/A";
		}


		EntityPtr getModelInstance(RenderModule* render_module, EntityRef bone_attachment) const {
			EntityPtr parent_entity = render_module->getBoneAttachmentParent(bone_attachment);
			if (!parent_entity.isValid()) return INVALID_ENTITY;
			return render_module->getWorld().hasComponent((EntityRef)parent_entity, MODEL_INSTANCE_TYPE) ? parent_entity : INVALID_ENTITY;
		}
	};

	reflection::structure<Ray>("Ray")
		.LUMIX_MEMBER(Ray::origin, "origin")
		.LUMIX_MEMBER(Ray::dir, "dir");

	reflection::structure<RayCastModelHit>("RayCastModelHit")
		.LUMIX_MEMBER(RayCastModelHit::is_hit, "is_hit")
		.LUMIX_MEMBER(RayCastModelHit::t, "t")
		.LUMIX_MEMBER(RayCastModelHit::entity, "entity");

	LUMIX_MODULE(RenderModuleImpl, "renderer")
		.LUMIX_FUNC(addDebugCross)
		.LUMIX_FUNC(addDebugLine)
		.LUMIX_FUNC(addDebugTriangle)
		.function<(RayCastModelHit (RenderModuleImpl::*)(const Ray&, EntityPtr))&RenderModuleImpl::castRay>("castRay", "RenderModuleImpl::castRay")
		.LUMIX_FUNC(setActiveCamera)
		.LUMIX_CMP(ProceduralGeometry, "procedural_geom", "Render / Procedural geometry")
			.LUMIX_PROP(ProceduralGeometryMaterial, "Material").resourceAttribute(Material::TYPE)
		.LUMIX_CMP(BoneAttachment, "bone_attachment", "Render / Bone attachment")
			.icon(ICON_FA_BONE)
			.LUMIX_PROP(BoneAttachmentParent, "Parent")
			.LUMIX_PROP(BoneAttachmentPosition, "Relative position")
			.LUMIX_PROP(BoneAttachmentRotation, "Relative rotation").radiansAttribute()
			.LUMIX_PROP(BoneAttachmentBone, "Bone").attribute<BoneEnum>() 
		.LUMIX_CMP(Fur, "fur", "Render / Fur")
			.var_prop<&RenderModule::getFur, &FurComponent::layers>("Layers")
			.var_prop<&RenderModule::getFur, &FurComponent::scale>("Scale")
			.var_prop<&RenderModule::getFur, &FurComponent::gravity>("Gravity")
			.var_prop<&RenderModule::getFur, &FurComponent::enabled>("Enabled")
		.LUMIX_CMP(EnvironmentProbe, "environment_probe", "Render / Environment probe")
			.prop<&RenderModule::isEnvironmentProbeEnabled, &RenderModule::enableEnvironmentProbe>("Enabled")
			.var_prop<&RenderModule::getEnvironmentProbe, &EnvironmentProbe::inner_range>("Inner range")
			.var_prop<&RenderModule::getEnvironmentProbe, &EnvironmentProbe::outer_range>("Outer range")
		.LUMIX_CMP(ReflectionProbe, "reflection_probe", "Render / Reflection probe")
			.prop<&RenderModule::isReflectionProbeEnabled, &RenderModule::enableReflectionProbe>("Enabled")
			.var_prop<&RenderModule::getReflectionProbe, &ReflectionProbe::size>("size")
			.var_prop<&RenderModule::getReflectionProbe, &ReflectionProbe::half_extents>("half_extents")
		.LUMIX_CMP(ParticleSystem, "particle_emitter", "Render / Particle emitter")
			.var_prop<&RenderModule::getParticleSystem, &ParticleSystem::m_autodestroy>("Autodestroy")
			.LUMIX_PROP(ParticleSystemPath, "Source").resourceAttribute(ParticleSystemResource::TYPE)
		.LUMIX_CMP(Camera, "camera", "Render / Camera")
			.icon(ICON_FA_CAMERA)
			.function<&RenderModule::getCameraRay>("getRay", "getCameraRay")
			.var_prop<&RenderModule::getCamera, &Camera::fov>("FOV").radiansAttribute()
			.var_prop<&RenderModule::getCamera, &Camera::near>("Near").minAttribute(0)
			.var_prop<&RenderModule::getCamera, &Camera::far>("Far").minAttribute(0)
			.var_prop<&RenderModule::getCamera, &Camera::is_ortho>("Orthographic")
			.var_prop<&RenderModule::getCamera, &Camera::ortho_size>("Orthographic size").minAttribute(0)

			.var_prop<&RenderModule::getCamera, &Camera::film_grain_intensity>("Film Grain intensity")
			.var_prop<&RenderModule::getCamera, &Camera::dof_enabled>("DOF enabled")
			.var_prop<&RenderModule::getCamera, &Camera::dof_distance>("DOF distance").minAttribute(0)
			.var_prop<&RenderModule::getCamera, &Camera::dof_range>("DOF range").minAttribute(0)
			.var_prop<&RenderModule::getCamera, &Camera::dof_max_blur_size>("DOF max blur size").minAttribute(0)
			.var_prop<&RenderModule::getCamera, &Camera::dof_sharp_range>("DOF sharp range").minAttribute(0)

			.var_prop<&RenderModule::getCamera, &Camera::bloom_enabled>("Bloom enabled")
			.var_prop<&RenderModule::getCamera, &Camera::bloom_tonemap_enabled>("Bloom tonemap enabled")
			.var_prop<&RenderModule::getCamera, &Camera::bloom_accomodation_speed>("Bloom accomodation speed")
			.var_prop<&RenderModule::getCamera, &Camera::bloom_avg_bloom_multiplier>("Bloom average bloom multiplier")
			.var_prop<&RenderModule::getCamera, &Camera::bloom_exposure>("Bloom exposure")

		.LUMIX_CMP(InstancedModel, "instanced_model", "Render / Instanced model")
			.LUMIX_PROP(InstancedModelPath, "Model").resourceAttribute(Model::TYPE)
			.blob_property<&RenderModuleImpl::getInstancedModelBlob, &RenderModuleImpl::setInstancedModelBlob>("Blob")
		.LUMIX_CMP(ModelInstance, "model_instance", "Render / Mesh")
			.LUMIX_FUNC_EX(RenderModule::getModelInstanceModel, "getModel")
			.LUMIX_FUNC_EX(RenderModuleImpl::overrideMaterialVec4, "overrideMaterialVec4")
			.prop<&RenderModule::isModelInstanceEnabled, &RenderModule::enableModelInstance>("Enabled")
			.LUMIX_PROP(ModelInstancePath, "Source").resourceAttribute(Model::TYPE)
		.LUMIX_CMP(Environment, "environment", "Render / Environment")
			.icon(ICON_FA_GLOBE)
			.var_prop<&RenderModule::getEnvironment, &Environment::light_color>("Color").colorAttribute()
			.var_prop<&RenderModule::getEnvironment, &Environment::direct_intensity>("Intensity").minAttribute(0)
			.var_prop<&RenderModule::getEnvironment, &Environment::indirect_intensity>("Indirect intensity").minAttribute(0)
			.LUMIX_PROP(ShadowmapCascades, "Shadow cascades")
			.LUMIX_PROP(EnvironmentCastShadows, "Cast shadows")
			.LUMIX_PROP(SkyTexturePath, "Sky texture").resourceAttribute(Texture::TYPE)
			.var_prop<&RenderModule::getEnvironment, &Environment::atmo_enabled>("Atmosphere enabled")
			.var_prop<&RenderModule::getEnvironment, &Environment::godrays_enabled>("Godrays enabled")
			.var_prop<&RenderModule::getEnvironment, &Environment::clouds_enabled>("Clouds enabled")
			.var_prop<&RenderModule::getEnvironment, &Environment::clouds_top>("Clouds top")
			.var_prop<&RenderModule::getEnvironment, &Environment::clouds_bottom>("Clouds bottom")
			.var_prop<&RenderModule::getEnvironment, &Environment::sky_intensity>("Sky intensity").minAttribute(0)
			.var_prop<&RenderModule::getEnvironment, &Environment::scatter_rayleigh>("Scatter Rayleigh").colorAttribute()
			.var_prop<&RenderModule::getEnvironment, &Environment::scatter_mie>("Scatter Mie").colorAttribute()
			.var_prop<&RenderModule::getEnvironment, &Environment::absorb_mie>("Absorb Mie").colorAttribute()
			.var_prop<&RenderModule::getEnvironment, &Environment::sunlight_color>("Sunlight color").colorAttribute()
			.var_prop<&RenderModule::getEnvironment, &Environment::sunlight_strength>("Sunlight strength").minAttribute(0)
			.var_prop<&RenderModule::getEnvironment, &Environment::height_distribution_rayleigh>("Height distribution Rayleigh")
			.var_prop<&RenderModule::getEnvironment, &Environment::height_distribution_mie>("Height distribution Mie")
			.var_prop<&RenderModule::getEnvironment, &Environment::ground_r>("Ground radius").minAttribute(0)
			.var_prop<&RenderModule::getEnvironment, &Environment::atmo_r>("Atmosphere radius").minAttribute(0)
			.var_prop<&RenderModule::getEnvironment, &Environment::fog_density>("Fog density").minAttribute(0)
			.var_prop<&RenderModule::getEnvironment, &Environment::fog_scattering>("Fog scattering").colorAttribute()
			.var_prop<&RenderModule::getEnvironment, &Environment::fog_top>("Fog top")
		.LUMIX_CMP(PointLight, "point_light", "Render / Point light")
			.icon(ICON_FA_LIGHTBULB)
			.LUMIX_PROP(PointLightCastShadows, "Cast shadows")
			.LUMIX_PROP(PointLightDynamic, "Dynamic")
			.var_prop<&RenderModule::getPointLight, &PointLight::intensity>("Intensity").minAttribute(0)
			.var_prop<&RenderModule::getPointLight, &PointLight::fov>("FOV").clampAttribute(0, 360).radiansAttribute()
			.var_prop<&RenderModule::getPointLight, &PointLight::attenuation_param>("Attenuation").clampAttribute(0, 100)
			.var_prop<&RenderModule::getPointLight, &PointLight::color>("Color").colorAttribute()
			.LUMIX_PROP(LightRange, "Range").minAttribute(0)
		.LUMIX_CMP(Decal, "decal", "Render / Decal")
			.LUMIX_PROP(DecalMaterialPath, "Material").resourceAttribute(Material::TYPE)
			.LUMIX_PROP(DecalHalfExtents, "Half extents").minAttribute(0)
			.var_prop<&RenderModule::getDecal, &Decal::uv_scale>("UV scale").minAttribute(0)
		.LUMIX_CMP(CurveDecal, "curve_decal", "Render / Curve decal")
			.LUMIX_PROP(CurveDecalMaterialPath, "Material").resourceAttribute(Material::TYPE)
			.LUMIX_PROP(CurveDecalHalfExtents, "Half extents").minAttribute(0)
			.LUMIX_PROP(CurveDecalUVScale, "UV scale").minAttribute(0)
			.LUMIX_PROP(CurveDecalBezierP0, "Bezier P0").noUIAttribute()
			.LUMIX_PROP(CurveDecalBezierP2, "Bezier P2").noUIAttribute()
		.LUMIX_CMP(Terrain, "terrain", "Render / Terrain")
			.LUMIX_FUNC_EX(RenderModule::getTerrainNormalAt, "getTerrainNormalAt")
			.LUMIX_FUNC_EX(RenderModule::getTerrainHeightAt, "getTerrainHeightAt")
			.LUMIX_PROP(TerrainMaterialPath, "Material").resourceAttribute(Material::TYPE)
			.LUMIX_PROP(TerrainXZScale, "XZ scale").minAttribute(0)
			.LUMIX_PROP(TerrainYScale, "Height scale").minAttribute(0)
			.LUMIX_PROP(TerrainTesselation, "Tesselation").minAttribute(1)
			.LUMIX_PROP(TerrainBaseGridResolution, "Grid resolution").minAttribute(8)
			.begin_array<&RenderModule::getGrassCount, &RenderModule::addGrass, &RenderModule::removeGrass>("grass")
				.LUMIX_PROP(GrassPath, "Mesh").resourceAttribute(Model::TYPE)
				.LUMIX_PROP(GrassDistance, "Distance").minAttribute(1)
				.LUMIX_PROP(GrassSpacing, "Spacing")
				.LUMIX_PROP(GrassRotationMode, "Mode").attribute<RotationModeEnum>()
			.end_array()
	;
}

RenderModuleImpl::RenderModuleImpl(Renderer& renderer,
	Engine& engine,
	World& world,
	IAllocator& allocator)
	: m_engine(engine)
	, m_world(world)
	, m_renderer(renderer)
	, m_allocator(allocator, "renderer module")
	, m_model_entity_map(m_allocator)
	, m_model_instances(m_allocator)
	, m_moved_instances(m_allocator)
	, m_instanced_models(m_allocator)
	, m_cameras(m_allocator)
	, m_terrains(m_allocator)
	, m_point_lights(m_allocator)
	, m_environments(m_allocator)
	, m_decals(m_allocator)
	, m_curve_decals(m_allocator)
	, m_debug_triangles(m_allocator)
	, m_debug_lines(m_allocator)
	, m_active_global_light_entity(INVALID_ENTITY)
	, m_active_camera(INVALID_ENTITY)
	, m_is_game_running(false)
	, m_particle_emitters(m_allocator)
	, m_bone_attachments(m_allocator)
	, m_environment_probes(m_allocator)
	, m_reflection_probes(m_allocator)
	, m_procedural_geometries(m_allocator)
	, m_material_decal_map(m_allocator)
	, m_material_curve_decal_map(m_allocator)
	, m_furs(m_allocator)
{
	m_world.componentTransformed(MODEL_INSTANCE_TYPE).bind<&RenderModuleImpl::onModelInstanceMoved>(this);
	m_world.componentTransformed(DECAL_TYPE).bind<&RenderModuleImpl::onDecalMoved>(this);
	m_world.componentTransformed(CURVE_DECAL_TYPE).bind<&RenderModuleImpl::onCurveDecalMoved>(this);
	m_world.componentTransformed(PARTICLE_EMITTER_TYPE).bind<&RenderModuleImpl::onParticleEmitterMoved>(this);
	m_world.componentTransformed(POINT_LIGHT_TYPE).bind<&RenderModuleImpl::onPointLightMoved>(this);
	m_world.componentTransformed(BONE_ATTACHMENT_TYPE).bind<&RenderModuleImpl::onBoneAttachmentMoved>(this);

	m_world.entityDestroyed().bind<&RenderModuleImpl::onEntityDestroyed>(this);
	m_culling_system = CullingSystem::create(m_allocator, engine.getPageAllocator());
	m_model_instances.reserve(1024);

	Renderer::MemRef mem;
	m_reflection_probes_texture = renderer.createTexture(128, 128, 32, gpu::TextureFormat::BC3, gpu::TextureFlags::IS_CUBE, mem, "reflection_probes");
}

UniquePtr<RenderModule> RenderModule::createInstance(Renderer& renderer,
	Engine& engine,
	World& world,
	IAllocator& allocator)
{
	return UniquePtr<RenderModuleImpl>::create(allocator, renderer, engine, world, allocator);
}



} // namespace Lumix