#include "renderer.h"

#include "engine/array.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/debug.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/job_system.h"
#include "engine/mt/atomic.h"
#include "engine/mt/sync.h"
#include "engine/mt/task.h"
#include "engine/os.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/universe/component.h"
#include "engine/universe/universe.h"
#include "renderer/font.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/particle_system.h"
#include "renderer/render_scene.h"
#include "renderer/shader.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"


#include <Windows.h>
#undef near
#undef far
#include "gl/GL.h"
#include "ffr/ffr.h"
#include <cstdio>

#define FFR_GL_IMPORT(prototype, name) static prototype name;
#define FFR_GL_IMPORT_TYPEDEFS

#include "ffr/gl_ext.h"

#define CHECK_GL(gl) \
	do { \
		gl; \
		GLenum err = glGetError(); \
		if (err != GL_NO_ERROR) { \
			logError("Renderer") << "OpenGL error " << err; \
		} \
	} while(0)

namespace Lumix
{


static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("model_instance");

enum { TRANSIENT_BUFFER_SIZE = 32 * 1024 * 1024 };


template <typename T>
struct RenderResourceManager : public ResourceManager
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
		ffr::QueryHandle handle;
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
		return u64(gpu_timestamp * (OS::Timer::getFrequency() / double(1'000'000'000))) + m_gpu_to_cpu_offset;
	}


	void init()
	{
		ffr::QueryHandle q = ffr::createQuery();
		ffr::queryTimestamp(q);
		const u64 cpu_timestamp = OS::Timer::getRawTimestamp();
		const u64 gpu_timestamp = ffr::getQueryResult(q);
		m_gpu_to_cpu_offset = cpu_timestamp - u64(gpu_timestamp * (OS::Timer::getFrequency() / double(1'000'000'000)));
		ffr::destroy(q);
	}


	void clear()
	{
		m_queries.clear();

		for(const ffr::QueryHandle h : m_pool) {
			ffr::destroy(h);
		}
		m_pool.clear();
	}


	ffr::QueryHandle allocQuery()
	{
		if(!m_pool.empty()) {
			const ffr::QueryHandle res = m_pool.back();
			m_pool.pop();
			return res;
		}
		return ffr::createQuery();
	}


	void beginQuery(const char* name, i64 profiler_link)
	{
		MT::CriticalSectionLock lock(m_mutex);
		Query& q = m_queries.emplace();
		q.profiler_link = profiler_link;
		q.name = name;
		q.is_end = false;
		q.is_frame = false;
		q.handle = allocQuery();
		ffr::queryTimestamp(q.handle);
	}


	void endQuery()
	{
		MT::CriticalSectionLock lock(m_mutex);
		Query& q = m_queries.emplace();
		q.is_end = true;
		q.is_frame = false;
		q.handle = allocQuery();
		ffr::queryTimestamp(q.handle);
	}


	void frame()
	{
		PROFILE_FUNCTION();
		MT::CriticalSectionLock lock(m_mutex);
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
			
			if (!ffr::isQueryReady(q.handle)) break;

			if (q.is_end) {
				const u64 timestamp = toCPUTimestamp(ffr::getQueryResult(q.handle));
				Profiler::endGPUBlock(timestamp);
			}
			else {
				const u64 timestamp = toCPUTimestamp(ffr::getQueryResult(q.handle));
				Profiler::beginGPUBlock(q.name, timestamp, q.profiler_link);
			}
			m_pool.push(q.handle);
			m_queries.erase(0);
		}
	}


	Array<Query> m_queries;
	Array<ffr::QueryHandle> m_pool;
	MT::CriticalSection m_mutex;
	i64 m_gpu_to_cpu_offset;
};


struct BoneProperty : Reflection::IEnumProperty
{
	BoneProperty() 
	{ 
		name = "Bone"; 
	}


	void getValue(ComponentUID cmp, int index, OutputMemoryStream& stream) const override
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		int value = scene->getBoneAttachmentBone((EntityRef)cmp.entity);
		stream.write(value);
	}


	void setValue(ComponentUID cmp, int index, InputMemoryStream& stream) const override
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		int value = stream.read<int>();
		scene->setBoneAttachmentBone((EntityRef)cmp.entity, value);
	}


	EntityPtr getModelInstance(RenderScene* render_scene, EntityRef bone_attachment) const
	{
		EntityPtr parent_entity = render_scene->getBoneAttachmentParent(bone_attachment);
		if (!parent_entity.isValid()) return INVALID_ENTITY;
		return render_scene->getUniverse().hasComponent((EntityRef)parent_entity, MODEL_INSTANCE_TYPE) ? parent_entity : INVALID_ENTITY;
	}


	int getEnumValueIndex(ComponentUID cmp, int value) const override  { return value; }
	int getEnumValue(ComponentUID cmp, int index) const override { return index; }


	int getEnumCount(ComponentUID cmp) const override
	{
		RenderScene* render_scene = static_cast<RenderScene*>(cmp.scene);
		EntityPtr model_instance = getModelInstance(render_scene, (EntityRef)cmp.entity);
		if (!model_instance.isValid()) return 0;

		auto* model = render_scene->getModelInstanceModel((EntityRef)model_instance);
		if (!model || !model->isReady()) return 0;

		return model->getBoneCount();
	}


	const char* getEnumName(ComponentUID cmp, int index) const override
	{
		RenderScene* render_scene = static_cast<RenderScene*>(cmp.scene);
		EntityPtr model_instance = getModelInstance(render_scene, (EntityRef)cmp.entity);
		if (!model_instance.isValid()) return "";

		auto* model = render_scene->getModelInstanceModel((EntityRef)model_instance);
		if (!model) return "";

		return model->getBone(index).name.c_str();
	}
};


static void registerProperties(IAllocator& allocator)
{
	using namespace Reflection;

	static auto rotationModeDesc = enumDesciptor<Terrain::GrassType::RotationMode>(
		LUMIX_ENUM_VALUE(Terrain::GrassType::RotationMode::ALL_RANDOM),
		LUMIX_ENUM_VALUE(Terrain::GrassType::RotationMode::Y_UP),
		LUMIX_ENUM_VALUE(Terrain::GrassType::RotationMode::ALIGN_WITH_NORMAL)
	);
	registerEnum(rotationModeDesc);

	static auto render_scene = scene("renderer", 
		component("bone_attachment",
			property("Parent", LUMIX_PROP(RenderScene, BoneAttachmentParent)),
			property("Relative position", LUMIX_PROP(RenderScene, BoneAttachmentPosition)),
			property("Relative rotation", LUMIX_PROP(RenderScene, BoneAttachmentRotation), 
				RadiansAttribute()),
			BoneProperty()
		),
		component("environment_probe",
			property("Enabled", &RenderScene::isEnvironmentProbeEnabled, &RenderScene::enableEnvironmentProbe),
			property("Radius", LUMIX_PROP(RenderScene, EnvironmentProbeRadius)),
			property("Enabled reflection", &RenderScene::isEnvironmentProbeReflectionEnabled, &RenderScene::enableEnvironmentProbeReflection),
			property("Override global size", &RenderScene::isEnvironmentProbeCustomSize, &RenderScene::enableEnvironmentProbeCustomSize),
			var_property("Radiance size", &RenderScene::getEnvironmentProbe, &EnvironmentProbe::radiance_size),
			var_property("Irradiance size", &RenderScene::getEnvironmentProbe, &EnvironmentProbe::irradiance_size)
		),
		component("particle_emitter",
			property("Resource", LUMIX_PROP(RenderScene, ParticleEmitterPath),
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
			property("Enabled", &RenderScene::isModelInstanceEnabled, &RenderScene::enableModelInstance),
			property("Source", LUMIX_PROP(RenderScene, ModelInstancePath),
				ResourceAttribute("Mesh (*.msh)", Model::TYPE))
		),
		component("environment",
			var_property("Color", &RenderScene::getEnvironment, &Environment::m_diffuse_color, ColorAttribute()),
			var_property("Intensity", &RenderScene::getEnvironment, &Environment::m_diffuse_intensity, MinAttribute(0)),
			var_property("Indirect intensity", &RenderScene::getEnvironment, &Environment::m_indirect_intensity, MinAttribute(0)),
			var_property("Fog density", &RenderScene::getEnvironment, &Environment::m_fog_density, ClampAttribute(0, 1)),
			var_property("Fog bottom", &RenderScene::getEnvironment, &Environment::m_fog_bottom),
			var_property("Fog height", &RenderScene::getEnvironment, &Environment::m_fog_height, MinAttribute(0)),
			var_property("Fog color", &RenderScene::getEnvironment, &Environment::m_fog_color, ColorAttribute()),
			property("Shadow cascades", LUMIX_PROP(RenderScene, ShadowmapCascades))
		),
		component("point_light",
			var_property("Cast shadows", &RenderScene::getPointLight, &PointLight::cast_shadows),
			var_property("Intensity", &RenderScene::getPointLight, &PointLight::intensity, MinAttribute(0)),
			var_property("FOV", &RenderScene::getPointLight, &PointLight::fov, ClampAttribute(0, 360), RadiansAttribute()),
			var_property("Attenuation", &RenderScene::getPointLight, &PointLight::attenuation_param, ClampAttribute(0, 100)),
			var_property("Color", &RenderScene::getPointLight, &PointLight::color, ColorAttribute()),
			property("Range", LUMIX_PROP(RenderScene, LightRange), MinAttribute(0))
		),
		component("text_mesh",
			property("Text", LUMIX_PROP(RenderScene, TextMeshText)),
			property("Font", LUMIX_PROP(RenderScene, TextMeshFontPath),
				ResourceAttribute("Font (*.ttf)", FontResource::TYPE)),
			property("Font Size", LUMIX_PROP(RenderScene, TextMeshFontSize)),
			property("Color", LUMIX_PROP(RenderScene, TextMeshColorRGBA),
				ColorAttribute()),
			property("Camera-oriented", &RenderScene::isTextMeshCameraOriented, &RenderScene::setTextMeshCameraOriented)
		),
		component("decal",
			property("Material", LUMIX_PROP(RenderScene, DecalMaterialPath),
				ResourceAttribute("Material (*.mat)", Material::TYPE)),
			property("Half extents", LUMIX_PROP(RenderScene, DecalHalfExtents), 
				MinAttribute(0))
		),
		component("terrain",
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
				enum_property("Mode", LUMIX_PROP(RenderScene, GrassRotationMode), rotationModeDesc)
			)
		)
	);
	registerScene(render_scene);
}


struct RendererImpl final : public Renderer
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
		, m_vsync(true)
		, m_profiler(m_allocator)
		, m_layers(m_allocator)
	{
		ffr::preinit(m_allocator);
	}


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

		JobSystem::SignalHandle signal = JobSystem::INVALID_HANDLE;
		JobSystem::runEx(this, [](void* data) {
			RendererImpl* renderer = (RendererImpl*)data;
			ffr::destroy(renderer->m_transient_buffer);
			renderer->m_profiler.clear();
			ffr::shutdown();
		}, &signal, m_last_exec_job, 1);
		JobSystem::wait(signal);
	}


	void init() override
	{
		registerProperties(m_engine.getAllocator());
		char cmd_line[4096];
		OS::getCommandLine(cmd_line, lengthOf(cmd_line));
		CommandLineParser cmd_line_parser(cmd_line);
		m_vsync = true;
		while (cmd_line_parser.next())
		{
			if (cmd_line_parser.currentEquals("-no_vsync"))
			{
				m_vsync = false;
				break;
			}
		}

		JobSystem::SignalHandle signal = JobSystem::INVALID_HANDLE;
		JobSystem::runEx(this, [](void* data) {
			PROFILE_BLOCK("init_render");
			RendererImpl& renderer = *(RendererImpl*)data;
			Engine& engine = renderer.getEngine();
			void* window_handle = engine.getPlatformData().window_handle;
			ffr::init(window_handle);
			renderer.m_framebuffer = ffr::createFramebuffer();
			renderer.m_transient_buffer = ffr::allocBufferHandle();
			renderer.m_transient_buffer_offset = 0;
			renderer.m_transient_buffer_frame_offset = 0;
			const uint transient_flags = (uint)ffr::BufferFlags::PERSISTENT
				| (uint)ffr::BufferFlags::MAP_WRITE
				| (uint)ffr::BufferFlags::MAP_FLUSH_EXPLICIT;
			ffr::createBuffer(renderer.m_transient_buffer, transient_flags, 2 * TRANSIENT_BUFFER_SIZE, nullptr);
			renderer.m_transient_buffer_ptr = (u8*)ffr::map(renderer.m_transient_buffer, 0, 2 * TRANSIENT_BUFFER_SIZE, transient_flags);
			renderer.m_profiler.init();
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


	MemRef copy(const void* data, uint size) override
	{
		MemRef mem = allocate(size);
		copyMemory(mem.data, data, size);
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


	MemRef allocate(uint size) override
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


	ffr::FramebufferHandle getFramebuffer() const override
	{
		return m_framebuffer;
	}


	void getTextureImage(ffr::TextureHandle texture, int size, void* data) override
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				ffr::pushDebugGroup("get image data");
				ffr::getTextureImage(handle, size, buf);
				ffr::popDebugGroup();
			}

			ffr::TextureHandle handle;
			uint size;
			void* buf;
		};

		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		cmd->handle = texture;
		cmd->size = size;
		cmd->buf = data;
		push(cmd, 0);
	}


	void updateTexture(ffr::TextureHandle handle, uint x, uint y, uint w, uint h, ffr::TextureFormat format, const MemRef& mem) override
	{
		ASSERT(mem.size > 0);
		ASSERT(handle.isValid());

		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				ffr::update(handle, 0, x, y, w, h, format, mem.data);
				if (mem.own) {
					renderer->free(mem);
				}
			}

			ffr::TextureHandle handle;
			uint x, y, w, h;
			ffr::TextureFormat format;
			MemRef mem;
			RendererImpl* renderer;
		};

		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		cmd->handle = handle;
		cmd->x = x;
		cmd->y = y;
		cmd->w = w;
		cmd->h = h;
		cmd->format = format;
		cmd->mem = mem;
		cmd->renderer = this;

		push(cmd, 0);
	}


	ffr::TextureHandle loadTexture(const MemRef& memory, u32 flags, ffr::TextureInfo* info, const char* debug_name) override
	{
		ASSERT(memory.size > 0);

		const ffr::TextureHandle handle = ffr::allocTextureHandle();
		if (!handle.isValid()) return handle;

		ffr::TextureInfo tmp_info = ffr::getTextureInfo(memory.data);
		if(info) {
			*info = tmp_info;
		}

		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				ffr::loadTexture(handle, memory.data, memory.size, flags, debug_name);
				if(memory.own) {
					renderer->free(memory);
				}
			}

			StaticString<MAX_PATH_LENGTH> debug_name;
			ffr::TextureHandle handle;
			MemRef memory;
			u32 flags;
			RendererImpl* renderer; 
		};

		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		cmd->debug_name = debug_name;
		cmd->handle = handle;
		cmd->memory = memory;
		cmd->flags = flags;
		cmd->renderer = this;
		push(cmd, 0);

		return handle;
	}


	TransientSlice allocTransient(uint size) override
	{
		// TODO grow if not enough space
		TransientSlice slice;
		slice.buffer = m_transient_buffer;
		slice.offset = MT::atomicAdd(&m_transient_buffer_offset, size);
		if (slice.offset + size > TRANSIENT_BUFFER_SIZE) {
			logError("Renderer") << "Out of transient memory";
			ASSERT(false);
			slice.size = 0;
			slice.ptr = nullptr;
			MT::atomicSubtract(&m_transient_buffer_offset, size);
		}
		else {
			slice.size = size;
			slice.ptr = m_transient_buffer_ptr + slice.offset + m_transient_buffer_frame_offset;
		}
		slice.offset += m_transient_buffer_frame_offset;
		return slice;
	}


	ffr::BufferHandle createBuffer(const MemRef& memory) override
	{
		ffr::BufferHandle handle = ffr::allocBufferHandle();
		if(!handle.isValid()) return handle;

		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				ffr::createBuffer(handle, (uint)ffr::BufferFlags::DYNAMIC_STORAGE, memory.size, memory.data);
				if (memory.own) {
					renderer->free(memory);
				}
			}

			ffr::BufferHandle handle;
			MemRef memory;
			ffr::TextureFormat format;
			Renderer* renderer;
		};

		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		cmd->handle = handle;
		cmd->memory = memory;
		cmd->renderer = this;
		push(cmd, 0);

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
		for(int i = 0; i < m_layers.size(); ++i) {
			if(m_layers[i] == name) return i;
		}
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

		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		cmd->fnc = fnc;
		cmd->ptr = user_ptr;
		cmd->renderer = this;
		push(cmd, 0);
	}

	
	void destroy(ffr::ProgramHandle program) override
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				ffr::destroy(program); 
			}

			ffr::ProgramHandle program;
			RendererImpl* renderer;
		};

		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		cmd->program = program;
		cmd->renderer = this;
		push(cmd, 0);
	}


	void destroy(ffr::BufferHandle buffer) override
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				ffr::destroy(buffer);
			}

			ffr::BufferHandle buffer;
			RendererImpl* renderer;
		};

		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		cmd->buffer = buffer;
		cmd->renderer = this;
		push(cmd, 0);
	}


	ffr::TextureHandle createTexture(uint w, uint h, uint depth, ffr::TextureFormat format, u32 flags, const MemRef& memory, const char* debug_name) override
	{
		ffr::TextureHandle handle = ffr::allocTextureHandle();
		if(!handle.isValid()) return handle;

		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override
			{
				PROFILE_FUNCTION();
				ffr::createTexture(handle, w, h, depth, format, flags, memory.data, debug_name);
				if (memory.own) renderer->free(memory);
			}

			StaticString<MAX_PATH_LENGTH> debug_name;
			ffr::TextureHandle handle;
			MemRef memory;
			uint w;
			uint h;
			uint depth;
			ffr::TextureFormat format;
			Renderer* renderer;
			u32 flags;
		};

		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		cmd->debug_name = debug_name;
		cmd->handle = handle;
		cmd->memory = memory;
		cmd->format = format;
		cmd->flags = flags;
		cmd->w = w;
		cmd->h = h;
		cmd->depth = depth;
		cmd->renderer = this;
		push(cmd, 0);

		return handle;
	}


	void destroy(ffr::TextureHandle tex)
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				ffr::destroy(texture); 
			}

			ffr::TextureHandle texture;
			RendererImpl* renderer;
		};

		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		cmd->texture = tex;
		cmd->renderer = this;
		push(cmd, 0);
	}


	void push(RenderJob* cmd, i64 profiler_link) override
	{
		ASSERT(!cmd->renderer);
		cmd->renderer = this;
		cmd->profiler_link = profiler_link;
		
		JobSystem::SignalHandle preconditions = m_last_exec_job;
		JobSystem::incSignal(&m_setup_jobs_done);
		JobSystem::run(cmd, [](void* data){
			RenderJob* cmd = (RenderJob*)data;
			PROFILE_BLOCK("setup_render_job");
			cmd->setup();
			RendererImpl* r = (RendererImpl*)cmd->renderer;
			JobSystem::decSignal(r->m_setup_jobs_done);
		}, &preconditions);

		JobSystem::SignalHandle exec_counter = JobSystem::INVALID_HANDLE;
		JobSystem::runEx(cmd, [](void* data){
			PROFILE_BLOCK("execute_render_job");
			Profiler::blockColor(0xaa, 0xff, 0xaa);
			RenderJob* cmd = (RenderJob*)data;
			Profiler::link(cmd->profiler_link);
			cmd->execute();
			LUMIX_DELETE(cmd->renderer->getAllocator(), cmd);
		}, &exec_counter, preconditions, 1);

		m_last_exec_job = exec_counter;
	}


	ResourceManager& getTextureManager() override { return m_texture_manager; }
	FontManager& getFontManager() override { return *m_font_manager; }

	void createScenes(Universe& ctx) override
	{
		auto* scene = RenderScene::createInstance(*this, m_engine, ctx, m_allocator);
		ctx.addScene(scene);
	}


	void destroyScene(IScene* scene) override { RenderScene::destroyInstance(static_cast<RenderScene*>(scene)); }
	const char* getName() const override { return "renderer"; }
	Engine& getEngine() override { return m_engine; }
	int getShaderDefinesCount() const override { return m_shader_defines.size(); }
	const char* getShaderDefine(int define_idx) const override { return m_shader_defines[define_idx]; }

	void makeScreenshot(const Path& filename) override {  }
	void resize(int w, int h) override {  }


	u8 getShaderDefineIdx(const char* define) override
	{
		for (int i = 0; i < m_shader_defines.size(); ++i)
		{
			if (m_shader_defines[i] == define)
			{
				return i;
			}
		}

		if (m_shader_defines.size() >= MAX_SHADER_DEFINES) {
			ASSERT(false);
			logError("Renderer") << "Too many shader defines.";
		}

		m_shader_defines.emplace(define);
		return m_shader_defines.size() - 1;
	}


	void startCapture() override
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				ffr::startCapture();
			}
		};
		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		push(cmd, 0);
	}


	void stopCapture() override
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				ffr::stopCapture();
			}
		};
		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		push(cmd, 0);
	}


	void frame() override
	{
		PROFILE_FUNCTION();
		struct SwapCmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				JobSystem::enableBackupWorker(true);
				ffr::swapBuffers();
				if(renderer->m_fence.isValid()) {
					ffr::waitClient(renderer->m_fence);
					ffr::destroy(renderer->m_fence);
				}
				renderer->m_fence = ffr::createFence();
				JobSystem::enableBackupWorker(false);
				renderer->m_profiler.frame();
			}
			RendererImpl* renderer;
			bool capture;
		};
		SwapCmd* swap_cmd = LUMIX_NEW(m_allocator, SwapCmd);
		swap_cmd->renderer = this;
		push(swap_cmd, 0);
		JobSystem::wait(m_prev_frame_job);
		JobSystem::wait(m_setup_jobs_done);
		m_setup_jobs_done = JobSystem::INVALID_HANDLE;

		m_transient_buffer_frame_offset = (m_transient_buffer_frame_offset + TRANSIENT_BUFFER_SIZE) % (2 * TRANSIENT_BUFFER_SIZE);
		m_transient_buffer_offset = 0;

		m_prev_frame_job = m_last_exec_job;
		//m_last_exec_job = JobSystem::INVALID_HANDLE;
	}


	using ShaderDefine = StaticString<32>;
	using Layer = StaticString<32>;


	Engine& m_engine;
	IAllocator& m_allocator;
	Array<ShaderDefine> m_shader_defines;
	Array<StaticString<32>> m_layers;
	FontManager* m_font_manager;
	RenderResourceManager<Material> m_material_manager;
	RenderResourceManager<Model> m_model_manager;
	RenderResourceManager<ParticleEmitterResource> m_particle_emitter_manager;
	RenderResourceManager<PipelineResource> m_pipeline_manager;
	RenderResourceManager<Shader> m_shader_manager;
	RenderResourceManager<Texture> m_texture_manager;
	bool m_vsync;
	JobSystem::SignalHandle m_last_exec_job = JobSystem::INVALID_HANDLE;
	JobSystem::SignalHandle m_prev_frame_job = JobSystem::INVALID_HANDLE;
	JobSystem::SignalHandle m_setup_jobs_done = JobSystem::INVALID_HANDLE;
	ffr::FenceHandle m_fence = ffr::INVALID_FENCE;

	ffr::FramebufferHandle m_framebuffer;
	ffr::BufferHandle m_transient_buffer;
	i32 m_transient_buffer_offset;
	i32 m_transient_buffer_frame_offset;
	u8* m_transient_buffer_ptr = nullptr;
	GPUProfiler m_profiler;
};


extern "C"
{
	LUMIX_PLUGIN_ENTRY(renderer)
	{
		return LUMIX_NEW(engine.getAllocator(), RendererImpl)(engine);
	}
}


} // namespace Lumix



