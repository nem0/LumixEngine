#include "renderer.h"

#include "engine/array.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/debug.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/atomic.h"
#include "engine/job_system.h"
#include "engine/sync.h"
#include "engine/thread.h"
#include "engine/os.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/universe.h"
#include "renderer/font.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/particle_system.h"
#include "renderer/render_scene.h"
#include "renderer/shader.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"


namespace Lumix
{


static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("model_instance");


struct TransientBuffer {
	static constexpr u32 INIT_SIZE = 1024 * 1024;
	
	void init() {
		m_buffer = gpu::allocBufferHandle();
		m_offset = 0;
		gpu::createBuffer(m_buffer, gpu::BufferFlags::MAPPABLE, INIT_SIZE, nullptr);
		m_size = INIT_SIZE;
		m_ptr = (u8*)gpu::map(m_buffer, INIT_SIZE);
	}

	Renderer::TransientSlice alloc(u32 size) {
		Renderer::TransientSlice slice;
		size = (size + 15) & ~15;
		slice.offset = atomicAdd(&m_offset, size);
		slice.size = size;
		if (slice.offset + size <= m_size) {
			slice.buffer = m_buffer;
			slice.ptr = m_ptr + slice.offset;
			return slice;
		}

		MutexGuard lock(m_mutex);
		if (!m_overflow.buffer) {
			m_overflow.buffer = gpu::allocBufferHandle();
			m_overflow.data = (u8*)OS::memReserve(128 * 1024 * 1024);
			m_overflow.size = 0;
			m_overflow.commit = 0;
		}
		slice.ptr = m_overflow.data + m_overflow.size;
		m_overflow.size += size;
		if (m_overflow.size > m_overflow.commit) {
			const u32 page_size = OS::getMemPageSize();
			m_overflow.commit = (m_overflow.size + page_size - 1) & ~(page_size - 1);
			OS::memCommit(m_overflow.data, m_overflow.commit);
		}
		slice.offset = 0;
		slice.buffer = m_overflow.buffer;
		return slice;
	} 

	void prepareToRender() {
		gpu::unmap(m_buffer);
		m_ptr = nullptr;

		if (m_overflow.buffer) {
			gpu::createBuffer(m_overflow.buffer, gpu::BufferFlags::NONE, nextPow2(m_overflow.size + m_size), nullptr);
			gpu::update(m_overflow.buffer, m_overflow.data, m_overflow.size);
			OS::memRelease(m_overflow.data);
			m_overflow.data = nullptr;
			m_overflow.commit = 0;
		}
	}

	void renderDone() {
		if (m_overflow.buffer) {
			m_size = nextPow2(m_overflow.size + m_size);
			gpu::destroy(m_buffer);
			m_buffer = m_overflow.buffer;
			m_overflow.buffer = gpu::INVALID_BUFFER;
			m_overflow.size = 0;
		}

		ASSERT(!m_ptr);
		m_ptr = (u8*)gpu::map(m_buffer, m_size);
		m_offset = 0;
	}

	gpu::BufferHandle m_buffer = gpu::INVALID_BUFFER;
	i32 m_offset = 0;
	u32 m_size = 0;
	u8* m_ptr = nullptr;
	Mutex m_mutex;

	struct {
		gpu::BufferHandle buffer = gpu::INVALID_BUFFER;
		u8* data = nullptr;
		u32 size = 0;
		u32 commit = 0;
	} m_overflow;
};


struct FrameData {
	FrameData(struct RendererImpl& renderer, IAllocator& allocator) 
		: jobs(allocator)
		, renderer(renderer)
		, to_compile_shaders(allocator)
		, material_updates(allocator)
	{}

	struct ShaderToCompile {
		Shader* shader;
		gpu::VertexDecl decl;
		u32 defines;
		gpu::ProgramHandle program;
		Shader::Sources sources;
	};

	struct MaterialUpdates {
		u32 idx;
		MaterialConsts value;
	};

	TransientBuffer transient_buffer;
	u32 gpu_frame = 0xffFFffFF;

	Array<MaterialUpdates> material_updates;
	Array<Renderer::RenderJob*> jobs;
	Mutex shader_mutex;
	Array<ShaderToCompile> to_compile_shaders;
	RendererImpl& renderer;
	JobSystem::SignalHandle can_setup = JobSystem::INVALID_HANDLE;
	JobSystem::SignalHandle setup_done = JobSystem::INVALID_HANDLE;
};


template <typename T>
struct RenderResourceManager : ResourceManager
{
	RenderResourceManager(Renderer& renderer, IAllocator& allocator) 
		: ResourceManager(allocator)
		, m_renderer(renderer)
	{}


	Resource* createResource(const Path& path) override
	{
		return LUMIX_NEW(m_allocator, T)(path, *this, m_renderer, m_allocator);
	}


	void destroyResource(Resource& resource) override
	{
		LUMIX_DELETE(m_allocator, &resource);
	}

	Renderer& m_renderer;
};


struct GPUProfiler
{
	struct Query
	{
		StaticString<32> name;
		gpu::QueryHandle handle;
		u64 result;
		i64 profiler_link;
		bool is_end;
		bool is_frame;
	};


	GPUProfiler(IAllocator& allocator) 
		: m_queries(allocator)
		, m_pool(allocator)
		, m_gpu_to_cpu_offset(0)
	{
	}


	~GPUProfiler()
	{
		ASSERT(m_pool.empty());
		ASSERT(m_queries.empty());
	}


	u64 toCPUTimestamp(u64 gpu_timestamp) const
	{
		return u64(gpu_timestamp * (OS::Timer::getFrequency() / double(gpu::getQueryFrequency()))) + m_gpu_to_cpu_offset;
	}


	void init()
	{
		gpu::QueryHandle q = gpu::createQuery();
		gpu::queryTimestamp(q);
		const u64 cpu_timestamp = OS::Timer::getRawTimestamp();

		u32 try_num = 0;
		while (!gpu::isQueryReady(q) && try_num < 10) {
			gpu::swapBuffers();
			++try_num;
		}
		if (try_num == 10) {
			logError("Failed to get GPU timestamp, timings are unreliable.");
			m_gpu_to_cpu_offset = 0;
		}
		else {
			const u64 gpu_timestamp = gpu::getQueryResult(q);
			m_gpu_to_cpu_offset = cpu_timestamp - u64(gpu_timestamp * (OS::Timer::getFrequency() / double(gpu::getQueryFrequency())));
			gpu::destroy(q);
		}
	}


	void clear()
	{
		m_queries.clear();

		for(const gpu::QueryHandle h : m_pool) {
			gpu::destroy(h);
		}
		m_pool.clear();
	}


	gpu::QueryHandle allocQuery()
	{
		if(!m_pool.empty()) {
			const gpu::QueryHandle res = m_pool.back();
			m_pool.pop();
			return res;
		}
		return gpu::createQuery();
	}


	void beginQuery(const char* name, i64 profiler_link)
	{
		MutexGuard lock(m_mutex);
		Query& q = m_queries.emplace();
		q.profiler_link = profiler_link;
		q.name = name;
		q.is_end = false;
		q.is_frame = false;
		q.handle = allocQuery();
		gpu::queryTimestamp(q.handle);
	}


	void endQuery()
	{
		MutexGuard lock(m_mutex);
		Query& q = m_queries.emplace();
		q.is_end = true;
		q.is_frame = false;
		q.handle = allocQuery();
		gpu::queryTimestamp(q.handle);
	}


	void frame()
	{
		PROFILE_FUNCTION();
		MutexGuard lock(m_mutex);
		Query frame_query;
		frame_query.is_frame = true;
		m_queries.push(frame_query);
		while (!m_queries.empty()) {
			Query q = m_queries[0];
			if (q.is_frame) {
				Profiler::gpuFrame();
				m_queries.erase(0);
				continue;
			}
			
			if (!gpu::isQueryReady(q.handle)) break;

			if (q.is_end) {
				const u64 timestamp = toCPUTimestamp(gpu::getQueryResult(q.handle));
				Profiler::endGPUBlock(timestamp);
			}
			else {
				const u64 timestamp = toCPUTimestamp(gpu::getQueryResult(q.handle));
				Profiler::beginGPUBlock(q.name, timestamp, q.profiler_link);
			}
			m_pool.push(q.handle);
			m_queries.erase(0);
		}
	}


	Array<Query> m_queries;
	Array<gpu::QueryHandle> m_pool;
	Mutex m_mutex;
	i64 m_gpu_to_cpu_offset;
};


struct BoneEnum : Reflection::EnumAttribute
{
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


	EntityPtr getModelInstance(RenderScene* render_scene, EntityRef bone_attachment) const
	{
		EntityPtr parent_entity = render_scene->getBoneAttachmentParent(bone_attachment);
		if (!parent_entity.isValid()) return INVALID_ENTITY;
		return render_scene->getUniverse().hasComponent((EntityRef)parent_entity, MODEL_INSTANCE_TYPE) ? parent_entity : INVALID_ENTITY;
	}
};


static void registerProperties(IAllocator& allocator)
{
	using namespace Reflection;

	struct RotationModeEnum : Reflection::EnumAttribute {
		u32 count(ComponentUID cmp) const override { return 2; }
		const char* name(ComponentUID cmp, u32 idx) const override {
			switch((Terrain::GrassType::RotationMode)idx) {
				case Terrain::GrassType::RotationMode::ALL_RANDOM: return "All random";
				case Terrain::GrassType::RotationMode::Y_UP: return "Y up";
				default: ASSERT(false); return "N/A";
			}
		}
	};

	static auto render_scene = scene("renderer",
		functions(
			LUMIX_FUNC(RenderScene::setGlobalLODMultiplier),
			LUMIX_FUNC(RenderScene::getGlobalLODMultiplier),
			LUMIX_FUNC(RenderScene::addDebugCross),
			LUMIX_FUNC(RenderScene::addDebugLine)
		),
		component("bone_attachment",
			property("Parent", LUMIX_PROP(RenderScene, BoneAttachmentParent)),
			property("Relative position", LUMIX_PROP(RenderScene, BoneAttachmentPosition)),
			property("Relative rotation", LUMIX_PROP(RenderScene, BoneAttachmentRotation), 
				RadiansAttribute()),
			property("Bone", LUMIX_PROP(RenderScene, BoneAttachmentBone), BoneEnum()) 
		),
		component("fur",
			var_property("Layers", &RenderScene::getFur, &FurComponent::layers),
			var_property("Scale", &RenderScene::getFur, &FurComponent::scale),
			var_property("Gravity", &RenderScene::getFur, &FurComponent::gravity),
			var_property("Enabled", &RenderScene::getFur, &FurComponent::enabled)
		),
		component("environment_probe",
			property("Enabled", &RenderScene::isEnvironmentProbeEnabled, &RenderScene::enableEnvironmentProbe),
			var_property("Inner range", &RenderScene::getEnvironmentProbe, &EnvironmentProbe::inner_range),
			var_property("Outer range", &RenderScene::getEnvironmentProbe, &EnvironmentProbe::outer_range)
		),
		component("reflection_probe",
			property("Enabled", &RenderScene::isReflectionProbeEnabled, &RenderScene::enableReflectionProbe),
			var_property("size", &RenderScene::getReflectionProbe, &ReflectionProbe::size),
			var_property("half_extents", &RenderScene::getReflectionProbe, &ReflectionProbe::half_extents)
		),
		component("particle_emitter",
			property("Emit rate", LUMIX_PROP(RenderScene, ParticleEmitterRate)),
			property("Source", LUMIX_PROP(RenderScene, ParticleEmitterPath),
				ResourceAttribute("Particle emitter (*.par)", ParticleEmitterResource::TYPE))
		),
		component("camera",
			var_property("FOV", &RenderScene::getCamera, &Camera::fov, RadiansAttribute()),
			var_property("Near", &RenderScene::getCamera, &Camera::near, MinAttribute(0)),
			var_property("Far", &RenderScene::getCamera, &Camera::far, MinAttribute(0)),
			var_property("Orthographic", &RenderScene::getCamera, &Camera::is_ortho),
			var_property("Orthographic size", &RenderScene::getCamera, &Camera::ortho_size, MinAttribute(0))
		),
		component("model_instance",
			functions(
				LUMIX_FUNC_EX(RenderScene::getModelInstanceModel, "getModel")
			),
			property("Enabled", &RenderScene::isModelInstanceEnabled, &RenderScene::enableModelInstance),
			property("Material", &RenderScene::getModelInstanceMaterialOverride,&RenderScene::setModelInstanceMaterialOverride, NoUIAttribute()),
			property("Source", LUMIX_PROP(RenderScene, ModelInstancePath), ResourceAttribute("Mesh (*.msh)", Model::TYPE))
		),
		component("environment",
			var_property("Color", &RenderScene::getEnvironment, &Environment::diffuse_color, ColorAttribute()),
			var_property("Intensity", &RenderScene::getEnvironment, &Environment::diffuse_intensity, MinAttribute(0)),
			var_property("Indirect intensity", &RenderScene::getEnvironment, &Environment::indirect_intensity, MinAttribute(0)),
			property("Shadow cascades", LUMIX_PROP(RenderScene, ShadowmapCascades)),
			property("Cast shadows", LUMIX_PROP(RenderScene, EnvironmentCastShadows))
		),
		component("point_light",
			property("Cast shadows", LUMIX_PROP(RenderScene, PointLightCastShadows)),
			property("Dynamic", LUMIX_PROP(RenderScene, PointLightDynamic)),
			var_property("Intensity", &RenderScene::getPointLight, &PointLight::intensity, MinAttribute(0)),
			var_property("FOV", &RenderScene::getPointLight, &PointLight::fov, ClampAttribute(0, 360), RadiansAttribute()),
			var_property("Attenuation", &RenderScene::getPointLight, &PointLight::attenuation_param, ClampAttribute(0, 100)),
			var_property("Color", &RenderScene::getPointLight, &PointLight::color, ColorAttribute()),
			property("Range", LUMIX_PROP(RenderScene, LightRange), MinAttribute(0))
		),
		component("decal",
			property("Material", LUMIX_PROP(RenderScene, DecalMaterialPath),
				ResourceAttribute("Material (*.mat)", Material::TYPE)),
			property("Half extents", LUMIX_PROP(RenderScene, DecalHalfExtents), 
				MinAttribute(0))
		),
		component("terrain",
			functions(
				LUMIX_FUNC(RenderScene::getTerrainNormalAt),
				LUMIX_FUNC(RenderScene::getTerrainHeightAt)
			),
			property("Material", LUMIX_PROP(RenderScene, TerrainMaterialPath),
				ResourceAttribute("Material (*.mat)", Material::TYPE)),
			property("XZ scale", LUMIX_PROP(RenderScene, TerrainXZScale), 
				MinAttribute(0)),
			property("Height scale", LUMIX_PROP(RenderScene, TerrainYScale), 
				MinAttribute(0)),
			array("grass", &RenderScene::getGrassCount, &RenderScene::addGrass, &RenderScene::removeGrass,
				property("Mesh", LUMIX_PROP(RenderScene, GrassPath),
					ResourceAttribute("Mesh (*.msh)", Model::TYPE)),
				property("Distance", LUMIX_PROP(RenderScene, GrassDistance),
					MinAttribute(1)),
				property("Density", LUMIX_PROP(RenderScene, GrassDensity)),
				property("Mode", LUMIX_PROP(RenderScene, GrassRotationMode), RotationModeEnum())
			)
		)
	);
	registerScene(render_scene);
}


struct RendererImpl final : Renderer
{
	explicit RendererImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_texture_manager(*this, m_allocator)
		, m_pipeline_manager(*this, m_allocator)
		, m_model_manager(*this, m_allocator)
		, m_particle_emitter_manager(*this, m_allocator)
		, m_material_manager(*this, m_allocator)
		, m_shader_manager(*this, m_allocator)
		, m_font_manager(nullptr)
		, m_shader_defines(m_allocator)
		, m_profiler(m_allocator)
		, m_layers(m_allocator)
		, m_material_buffer(m_allocator)
		, m_plugins(m_allocator)
	{
		LUMIX_FUNC(Model::getBoneCount);
		LUMIX_FUNC(Model::getBoneName);
		LUMIX_FUNC(Model::getBoneParent);

		m_shader_defines.reserve(32);

		gpu::preinit(m_allocator, shouldLoadRenderdoc());
		m_frames[0].create(*this, m_allocator);
		m_frames[1].create(*this, m_allocator);
		m_frames[2].create(*this, m_allocator);
	}

	u32 getVersion() const override { return 0; }
	void serialize(OutputMemoryStream& stream) const override {}
	bool deserialize(u32 version, InputMemoryStream& stream) override { return version == 0; }

	~RendererImpl()
	{
		m_particle_emitter_manager.destroy();
		m_pipeline_manager.destroy();
		m_texture_manager.destroy();
		m_model_manager.destroy();
		m_material_manager.destroy();
		m_shader_manager.destroy();
		m_font_manager->destroy();
		LUMIX_DELETE(m_allocator, m_font_manager);

		frame();
		frame();
		frame();

		waitForRender();
		
		JobSystem::SignalHandle signal = JobSystem::INVALID_HANDLE;
		JobSystem::runEx(this, [](void* data) {
			RendererImpl* renderer = (RendererImpl*)data;
			for (const Local<FrameData>& frame : renderer->m_frames) {
				gpu::destroy(frame->transient_buffer.m_buffer);
			}
			gpu::destroy(renderer->m_material_buffer.buffer);
			gpu::destroy(renderer->m_material_buffer.staging_buffer);
			renderer->m_profiler.clear();
			gpu::shutdown();
		}, &signal, JobSystem::INVALID_HANDLE, 1);
		JobSystem::wait(signal);
	}

	static bool shouldLoadRenderdoc() {
		char cmd_line[4096];
		OS::getCommandLine(Span(cmd_line));
		CommandLineParser cmd_line_parser(cmd_line);
		while (cmd_line_parser.next()) {
			if (cmd_line_parser.currentEquals("-renderdoc")) {
				return true;
			}
		}
		return false;
	}

	void init() override
	{
		registerProperties(m_engine.getAllocator());
		
		struct InitData {
			gpu::InitFlags flags = gpu::InitFlags::VSYNC;
			RendererImpl* renderer;
		} init_data;
		init_data.renderer = this;
		
		char cmd_line[4096];
		OS::getCommandLine(Span(cmd_line));
		CommandLineParser cmd_line_parser(cmd_line);
		while (cmd_line_parser.next()) {
			if (cmd_line_parser.currentEquals("-no_vsync")) {
				init_data.flags = init_data.flags & ~gpu::InitFlags::VSYNC;
			}
			else if (cmd_line_parser.currentEquals("-debug_opengl")) {
				init_data.flags = init_data.flags | gpu::InitFlags::DEBUG_OUTPUT;
			}
		}

		JobSystem::SignalHandle signal = JobSystem::INVALID_HANDLE;
		JobSystem::runEx(&init_data, [](void* data) {
			PROFILE_BLOCK("init_render");
			InitData* init_data = (InitData*)data;
			RendererImpl& renderer = *(RendererImpl*)init_data->renderer;
			Engine& engine = renderer.getEngine();
			void* window_handle = engine.getWindowHandle();
			if (!gpu::init(window_handle, init_data->flags)) {
				OS::messageBox("Failed to initialize renderer. More info in lumix.log.");
				fatal(false, "gpu::init()");
			}

			gpu::MemoryStats mem_stats;
			if (gpu::getMemoryStats(Ref(mem_stats))) {
				logInfo("Initial GPU memory stats:\n",
					"total: ", (mem_stats.total_available_mem / (1024.f * 1024.f)), "MB\n"
					"currect: ", (mem_stats.current_available_mem / (1024.f * 1024.f)), "MB\n"
					"dedicated: ", (mem_stats.dedicated_vidmem/ (1024.f * 1024.f)), "MB\n");
			}

			for (const Local<FrameData>& frame : renderer.m_frames) {
				frame->transient_buffer.init();
			}
			renderer.m_cpu_frame = renderer.m_frames[0].get();
			renderer.m_gpu_frame = renderer.m_frames[0].get();

			renderer.m_profiler.init();

			MaterialBuffer& mb = renderer.m_material_buffer;
			mb.buffer = gpu::allocBufferHandle();
			mb.staging_buffer = gpu::allocBufferHandle();
			mb.map.insert(0, 0);
			mb.data.resize(400);
			mb.data[0].hash = 0;
			mb.data[0].ref_count = 1;
			mb.first_free = 1;
			for (int i = 1; i < 400; ++i) {
				mb.data[i].ref_count = 0;
				mb.data[i].next_free = i + 1;
			}
			mb.data.back().next_free = -1;
			gpu::createBuffer(mb.buffer
				, gpu::BufferFlags::UNIFORM_BUFFER
				, sizeof(MaterialConsts) * 400
				, nullptr
			);
			gpu::createBuffer(mb.staging_buffer
				, gpu::BufferFlags::UNIFORM_BUFFER
				, sizeof(MaterialConsts)
				, nullptr
			);

			;

			MaterialConsts default_mat;
			default_mat.color = Vec4(1, 0, 1, 1);
			gpu::update(mb.buffer, &default_mat, sizeof(MaterialConsts));
		}, &signal, JobSystem::INVALID_HANDLE, 1);
		JobSystem::wait(signal);

		ResourceManagerHub& manager = m_engine.getResourceManager();
		m_pipeline_manager.create(PipelineResource::TYPE, manager);
		m_texture_manager.create(Texture::TYPE, manager);
		m_model_manager.create(Model::TYPE, manager);
		m_material_manager.create(Material::TYPE, manager);
		m_particle_emitter_manager.create(ParticleEmitterResource::TYPE, manager);
		m_shader_manager.create(Shader::TYPE, manager);
		m_font_manager = LUMIX_NEW(m_allocator, FontManager)(*this, m_allocator);
		m_font_manager->create(FontResource::TYPE, manager);

		RenderScene::registerLuaAPI(m_engine.getState());

		m_layers.emplace("default");
	}


	MemRef copy(const void* data, u32 size) override
	{
		MemRef mem = allocate(size);
		memcpy(mem.data, data, size);
		return mem;
	}


	IAllocator& getAllocator() override
	{
		return m_allocator;
	}


	void free(const MemRef& memory) override
	{
		ASSERT(memory.own);
		m_allocator.deallocate(memory.data);
	}


	MemRef allocate(u32 size) override
	{
		MemRef ret;
		ret.size = size;
		ret.own = true;
		ret.data = m_allocator.allocate(size);
		return ret;
	}


	void beginProfileBlock(const char* name, i64 link) override
	{
		m_profiler.beginQuery(name, link);
	}


	void endProfileBlock() override
	{
		m_profiler.endQuery();
	}


	void getTextureImage(gpu::TextureHandle texture, u32 w, u32 h, gpu::TextureFormat out_format, Span<u8> data) override
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::pushDebugGroup("get image data");
				gpu::TextureHandle staging = gpu::allocTextureHandle();
				const gpu::TextureFlags flags = gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::READBACK;
				gpu::createTexture(staging, w, h, 1, out_format, flags, nullptr, "staging_buffer");
				gpu::copy(staging, handle, 0, 0);
				gpu::readTexture(staging, 0, buf);
				gpu::destroy(staging);
				gpu::popDebugGroup();
			}

			gpu::TextureHandle handle;
			gpu::TextureFormat out_format;
			u32 w;
			u32 h;
			Span<u8> buf;
		};

		
		Cmd& cmd = createJob<Cmd>();
		cmd.handle = texture;
		cmd.w = w;
		cmd.h = h;
		cmd.buf = data;
		cmd.out_format = out_format;
		queue(cmd, 0);
	}


	void updateTexture(gpu::TextureHandle handle, u32 slice, u32 x, u32 y, u32 w, u32 h, gpu::TextureFormat format, const MemRef& mem) override
	{
		ASSERT(mem.size > 0);
		ASSERT(handle);

		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::update(handle, 0, slice, x, y, w, h, format, mem.data);
				if (mem.own) {
					renderer->free(mem);
				}
			}

			gpu::TextureHandle handle;
			u32 x, y, w, h, slice;
			gpu::TextureFormat format;
			MemRef mem;
			RendererImpl* renderer;
		};

		Cmd& cmd = createJob<Cmd>();
		cmd.handle = handle;
		cmd.x = x;
		cmd.y = y;
		cmd.w = w;
		cmd.h = h;
		cmd.slice = slice;
		cmd.format = format;
		cmd.mem = mem;
		cmd.renderer = this;

		queue(cmd, 0);
	}


	gpu::TextureHandle loadTexture(const MemRef& memory, gpu::TextureFlags flags, gpu::TextureInfo* info, const char* debug_name) override
	{
		ASSERT(memory.size > 0);

		const gpu::TextureHandle handle = gpu::allocTextureHandle();
		if (!handle) return handle;

		if(info) {
			*info = gpu::getTextureInfo(memory.data);
		}

		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::loadTexture(handle, memory.data, memory.size, flags, debug_name);
				if(memory.own) {
					renderer->free(memory);
				}
			}

			StaticString<MAX_PATH_LENGTH> debug_name;
			gpu::TextureHandle handle;
			MemRef memory;
			gpu::TextureFlags flags;
			RendererImpl* renderer; 
		};

		Cmd& cmd = createJob<Cmd>();
		cmd.debug_name = debug_name;
		cmd.handle = handle;
		cmd.memory = memory;
		cmd.flags = flags;
		cmd.renderer = this;
		queue(cmd, 0);

		return handle;
	}


	TransientSlice allocTransient(u32 size) override
	{
		return m_cpu_frame->transient_buffer.alloc(size);
	}
	
	gpu::BufferHandle getMaterialUniformBuffer() override {
		return m_material_buffer.buffer;
	}

	u32 createMaterialConstants(const MaterialConsts& data) override {
		const u32 hash = crc32(&data, sizeof(data));
		auto iter = m_material_buffer.map.find(hash);
		u32 idx;
		if(iter.isValid()) {
			idx = iter.value();
		}
		else {
			if (m_material_buffer.first_free == -1) {
				ASSERT(false);
				++m_material_buffer.data[0].ref_count;
				return 0;
			}
			idx = m_material_buffer.first_free;
			m_material_buffer.first_free = m_material_buffer.data[m_material_buffer.first_free].next_free;
			m_material_buffer.data[idx].ref_count = 0;
			m_material_buffer.data[idx].hash = crc32(&data, sizeof(data));
			m_material_buffer.map.insert(hash, idx);
			m_cpu_frame->material_updates.push({idx, data});
		}
		++m_material_buffer.data[idx].ref_count;
		return idx;
	}

	void destroyMaterialConstants(u32 idx) override {
		--m_material_buffer.data[idx].ref_count;
		if (m_material_buffer.data[idx].ref_count > 0) return;
			
		const u32 hash = m_material_buffer.data[idx].hash;
		m_material_buffer.data[idx].next_free = m_material_buffer.first_free;
		m_material_buffer.first_free = idx;
		m_material_buffer.map.erase(hash);
	}


	gpu::BufferHandle createBuffer(const MemRef& memory, gpu::BufferFlags flags) override
	{
		gpu::BufferHandle handle = gpu::allocBufferHandle();
		if(!handle) return handle;

		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::createBuffer(handle, flags, memory.size, memory.data);
				if (memory.own) {
					renderer->free(memory);
				}
			}

			gpu::BufferHandle handle;
			MemRef memory;
			gpu::BufferFlags flags;
			gpu::TextureFormat format;
			Renderer* renderer;
		};

		Cmd& cmd = createJob<Cmd>();
		cmd.handle = handle;
		cmd.memory = memory;
		cmd.renderer = this;
		cmd.flags = flags;
		queue(cmd, 0);

		return handle;
	}

	
	u8 getLayersCount() const override
	{
		return (u8)m_layers.size();
	}


	const char* getLayerName(u8 layer) const override
	{
		return m_layers[layer];
	}


	u8 getLayerIdx(const char* name) override
	{
		for(u8 i = 0; i < m_layers.size(); ++i) {
			if(m_layers[i] == name) return i;
		}
		ASSERT(m_layers.size() < 0xff);
		m_layers.emplace(name);
		return m_layers.size() - 1;
	}


	void runInRenderThread(void* user_ptr, void (*fnc)(Renderer& renderer, void*)) override
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				fnc(*renderer, ptr); 
			}

			void* ptr;
			void (*fnc)(Renderer&, void*);
			Renderer* renderer;

		};

		Cmd& cmd = createJob<Cmd>();
		cmd.fnc = fnc;
		cmd.ptr = user_ptr;
		cmd.renderer = this;
		queue(cmd, 0);
	}

	
	void destroy(gpu::ProgramHandle program) override
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				gpu::destroy(program); 
			}

			gpu::ProgramHandle program;
			RendererImpl* renderer;
		};

		Cmd& cmd = createJob<Cmd>();
		cmd.program = program;
		cmd.renderer = this;
		queue(cmd, 0);
	}

	void destroy(gpu::BufferHandle buffer) override
	{
		if (!buffer) return;
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				gpu::destroy(buffer);
			}

			gpu::BufferHandle buffer;
			RendererImpl* renderer;
		};

		Cmd& cmd = createJob<Cmd>();
		cmd.buffer = buffer;
		cmd.renderer = this;
		queue(cmd, 0);
	}


	gpu::TextureHandle createTexture(u32 w, u32 h, u32 depth, gpu::TextureFormat format, gpu::TextureFlags flags, const MemRef& memory, const char* debug_name) override
	{
		gpu::TextureHandle handle = gpu::allocTextureHandle();
		if(!handle) return handle;

		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override
			{
				PROFILE_FUNCTION();
				gpu::createTexture(handle, w, h, depth, format, flags, memory.data, debug_name);
				if (memory.own) renderer->free(memory);
			}

			StaticString<MAX_PATH_LENGTH> debug_name;
			gpu::TextureHandle handle;
			MemRef memory;
			u32 w;
			u32 h;
			u32 depth;
			gpu::TextureFormat format;
			Renderer* renderer;
			gpu::TextureFlags flags;
		};

		Cmd& cmd = createJob<Cmd>();
		cmd.debug_name = debug_name;
		cmd.handle = handle;
		cmd.memory = memory;
		cmd.format = format;
		cmd.flags = flags;
		cmd.w = w;
		cmd.h = h;
		cmd.depth = depth;
		cmd.renderer = this;
		queue(cmd, 0);

		return handle;
	}


	void destroy(gpu::TextureHandle tex) override
	{
		if (!tex) return;
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				gpu::destroy(texture); 
			}

			gpu::TextureHandle texture;
			RendererImpl* renderer;
		};

		Cmd& cmd = createJob<Cmd>();
		cmd.texture = tex;
		cmd.renderer = this;
		queue(cmd, 0);
	}


	void queue(RenderJob& cmd, i64 profiler_link) override
	{
		cmd.profiler_link = profiler_link;
		
		m_cpu_frame->jobs.push(&cmd);

		JobSystem::run(&cmd, [](void* data){
			RenderJob* cmd = (RenderJob*)data;
			PROFILE_BLOCK("setup_render_job");
			cmd->setup();
		}, &m_cpu_frame->setup_done);
	}

	void addPlugin(RenderPlugin& plugin) override {
		m_plugins.push(&plugin);
	}

	void removePlugin(RenderPlugin& plugin) override {
		m_plugins.eraseItem(&plugin);
	}

	Span<RenderPlugin*> getPlugins() override { return m_plugins; }


	ResourceManager& getTextureManager() override { return m_texture_manager; }
	FontManager& getFontManager() override { return *m_font_manager; }

	void createScenes(Universe& ctx) override
	{
		UniquePtr<RenderScene> scene = RenderScene::createInstance(*this, m_engine, ctx, m_allocator);
		ctx.addScene(scene.move());
	}

	void* allocJob(u32 size, u32 align) override {
		return m_allocator.allocate_aligned(size, align);
	}

	void deallocJob(void* job) override {
		m_allocator.deallocate_aligned(job);
	}

	const char* getName() const override { return "renderer"; }
	Engine& getEngine() override { return m_engine; }
	int getShaderDefinesCount() const override { return m_shader_defines.size(); }
	const char* getShaderDefine(int define_idx) const override { return m_shader_defines[define_idx]; }

	gpu::ProgramHandle queueShaderCompile(Shader& shader, gpu::VertexDecl decl, u32 defines) override {
		ASSERT(shader.isReady());
		MutexGuard lock(m_cpu_frame->shader_mutex);
		
		for (const auto& i : m_cpu_frame->to_compile_shaders) {
			if (i.shader == &shader && decl.hash == i.decl.hash && defines == i.defines) {
				return i.program;
			}
		}
		gpu::ProgramHandle program = gpu::allocProgramHandle();
		m_cpu_frame->to_compile_shaders.push({&shader, decl, defines, program, shader.m_sources});
		return program;
	}

	void makeScreenshot(const Path& filename) override {  }


	u8 getShaderDefineIdx(const char* define) override
	{
		MutexGuard lock(m_shader_defines_mutex);
		for (int i = 0; i < m_shader_defines.size(); ++i)
		{
			if (m_shader_defines[i] == define)
			{
				return i;
			}
		}

		if (m_shader_defines.size() >= MAX_SHADER_DEFINES) {
			ASSERT(false);
			logError("Too many shader defines.");
		}

		m_shader_defines.emplace(define);
		ASSERT(m_shader_defines.size() <= 32); // m_shader_defines are reserved in renderer constructor, so getShaderDefine() is MT safe
		return u8(m_shader_defines.size() - 1);
	}


	void startCapture() override
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				gpu::startCapture();
			}
		};
		Cmd& cmd = createJob<Cmd>();
		queue(cmd, 0);
	}


	void stopCapture() override
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				gpu::stopCapture();
			}
		};
		Cmd& cmd = createJob<Cmd>();
		queue(cmd, 0);
	}

	void render() {
		FrameData& frame = *m_gpu_frame;
		frame.transient_buffer.prepareToRender();
		
		gpu::MemoryStats mem_stats;
		if (gpu::getMemoryStats(Ref(mem_stats))) {
			Profiler::gpuMemStats(mem_stats.total_available_mem, mem_stats.current_available_mem, mem_stats.dedicated_vidmem);
		}

		for (const auto& i : frame.to_compile_shaders) {
			Shader::compile(i.program, i.decl, i.defines, i.sources, *this);
		}
		frame.to_compile_shaders.clear();

		for (const auto& i : frame.material_updates) {
			gpu::update(m_material_buffer.staging_buffer, &i.value, sizeof(MaterialConsts));
			gpu::copy(m_material_buffer.buffer, m_material_buffer.staging_buffer, i.idx * sizeof(MaterialConsts), sizeof(MaterialConsts));
		}
		frame.material_updates.clear();

		gpu::useProgram(gpu::INVALID_PROGRAM);
		gpu::bindIndexBuffer(gpu::INVALID_BUFFER);
		for (RenderJob* job : frame.jobs) {
			PROFILE_BLOCK("execute_render_job");
			Profiler::blockColor(0xaa, 0xff, 0xaa);
			Profiler::link(job->profiler_link);
			job->execute();
			destroyJob(*job);
		}
		frame.jobs.clear();

		PROFILE_BLOCK("swap buffers");
		JobSystem::enableBackupWorker(true);
			
		frame.gpu_frame = gpu::swapBuffers();
			
		JobSystem::enableBackupWorker(false);
		m_profiler.frame();

		m_gpu_frame = m_frames[(getFrameIndex(m_gpu_frame) + 1) % lengthOf(m_frames)].get();
		FrameData& check_frame = *m_frames[(getFrameIndex(m_gpu_frame) + 1) % lengthOf(m_frames)].get();

		if (check_frame.gpu_frame != 0xffFFffFF && gpu::frameFinished(check_frame.gpu_frame)) {
			check_frame.gpu_frame = 0xffFFffFF;
			check_frame.transient_buffer.renderDone();
			JobSystem::decSignal(check_frame.can_setup);
		}

		if (m_gpu_frame->gpu_frame != 0xffFFffFF) {
			gpu::waitFrame(m_gpu_frame->gpu_frame);
			m_gpu_frame->gpu_frame = 0xFFffFFff;   
			m_gpu_frame->transient_buffer.renderDone();
			JobSystem::decSignal(m_gpu_frame->can_setup);
		}
	}

	void waitForCommandSetup() override
	{
		JobSystem::wait(m_cpu_frame->setup_done);
		m_cpu_frame->setup_done = JobSystem::INVALID_HANDLE;
	}

	void waitForRender() override {
		JobSystem::wait(m_last_render);
	}

	i32 getFrameIndex(FrameData* frame) const {
		for (i32 i = 0; i < (i32)lengthOf(m_frames); ++i) {
			if (frame == m_frames[i].get()) return i;
		}
		ASSERT(false);
		return -1;
	}

	void frame() override
	{
		PROFILE_FUNCTION();
		
		JobSystem::wait(m_cpu_frame->setup_done);
		m_cpu_frame->setup_done = JobSystem::INVALID_HANDLE;
		for (const auto& i : m_cpu_frame->to_compile_shaders) {
			const u64 key = i.defines | ((u64)i.decl.hash << 32);
			i.shader->m_programs.insert(key, i.program);
		}

		JobSystem::incSignal(&m_cpu_frame->can_setup);
		
		m_cpu_frame = m_frames[(getFrameIndex(m_cpu_frame) + 1) % lengthOf(m_frames)].get();
		JobSystem::runEx(this, [](void* ptr){
			auto* renderer = (RendererImpl*)ptr;
			renderer->render();
		}, &m_last_render, JobSystem::INVALID_HANDLE, 1);

		JobSystem::wait(m_cpu_frame->can_setup);
	}

	Engine& m_engine;
	IAllocator& m_allocator;
	Array<StaticString<32>> m_shader_defines;
	Mutex m_shader_defines_mutex;
	Array<StaticString<32>> m_layers;
	FontManager* m_font_manager;
	MaterialManager m_material_manager;
	RenderResourceManager<Model> m_model_manager;
	RenderResourceManager<ParticleEmitterResource> m_particle_emitter_manager;
	RenderResourceManager<PipelineResource> m_pipeline_manager;
	RenderResourceManager<Shader> m_shader_manager;
	RenderResourceManager<Texture> m_texture_manager;

	Array<RenderPlugin*> m_plugins;
	Local<FrameData> m_frames[3];
	FrameData* m_gpu_frame = nullptr;
	FrameData* m_cpu_frame = nullptr;
	JobSystem::SignalHandle m_last_render = JobSystem::INVALID_HANDLE;

	GPUProfiler m_profiler;

	struct MaterialBuffer {
		MaterialBuffer(IAllocator& alloc) 
			: map(alloc)
			, data(alloc)
		{}

		struct Data {
			u32 ref_count;
			union {
				u32 hash;
				u32 next_free;
			};
		};

		gpu::BufferHandle buffer = gpu::INVALID_BUFFER;
		gpu::BufferHandle staging_buffer = gpu::INVALID_BUFFER;
		Array<Data> data;
		int first_free;
		HashMap<u32, u32> map;
	} m_material_buffer;
};


extern "C"
{
	LUMIX_PLUGIN_ENTRY(renderer)
	{
		return LUMIX_NEW(engine.getAllocator(), RendererImpl)(engine);
	}
}


} // namespace Lumix



