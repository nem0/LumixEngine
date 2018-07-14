#include "renderer.h"

#include "engine/array.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/debug/debug.h"
#include "engine/engine.h"
#include "engine/fs/os_file.h"
#include "engine/log.h"
#include "engine/job_system.h"
#include "engine/mt/lock_free_fixed_queue.h"
#include "engine/mt/atomic.h"
#include "engine/mt/sync.h"
#include "engine/mt/task.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/system.h"
#include "engine/universe/component.h"
#include "engine/universe/universe.h"
#include "renderer/draw2d.h"
#include "renderer/font_manager.h"
#include "renderer/material.h"
#include "renderer/material_manager.h"
#include "renderer/model.h"
#include "renderer/model_manager.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/shader.h"
#include "renderer/shader_manager.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"
#include "renderer/texture_manager.h"

#include <Windows.h>
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
			g_log_error.log("Renderer") << "OpenGL error " << err; \
		} \
	} while(0)

namespace Lumix
{


static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("renderable");


struct GPUProfiler
{
	using Query = Renderer::GPUProfilerQuery;

	GPUProfiler(IAllocator& allocator) 
		: m_queries(allocator) 
		, m_pool(allocator)
		, m_history(allocator)
	{
		m_history.emplace(allocator);
		m_history.emplace(allocator);
		m_history.emplace(allocator);
	}


	~GPUProfiler()
	{
		// TODO cleanup
		ASSERT(false);
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


	void beginQuery(const char* name)
	{
		Query& q = m_queries.emplace();
		q.name = name;
		q.is_end = false;
		q.handle = allocQuery();
		ffr::queryTimestamp(q.handle);
	}


	void endQuery()
	{
		Query& q = m_queries.emplace();
		q.is_end = true;
		q.handle = allocQuery();
		ffr::queryTimestamp(q.handle);
	}


	bool getResults(Array<Query>* results)
	{
		if (m_history_rd == m_history_wr) {
			return false;
		}
		results->swap(m_history[m_history_rd % m_history.size()]);
		MT::atomicIncrement((volatile int*)&m_history_rd);
		return true;
	}


	void frame()
	{
		for (Query& q : m_queries) {
			q.result = ffr::getQueryResult(q.handle);
			m_pool.push(q.handle);
		}
		if (m_history_wr < m_history_rd + m_history.size()) {
			m_history[m_history_wr % m_history.size()].swap(m_queries);
			MT::atomicIncrement((volatile int*)&m_history_wr);
		}
		m_queries.clear();
		MT::atomicIncrement(&m_frame);
		++m_frame;
	}


	volatile int m_frame = 0;
	int m_current = -1;
	volatile uint m_history_rd = 0;
	volatile uint m_history_wr = 0;
	Array<Array<Query>> m_history;
	Array<Query> m_queries;
	Array<ffr::QueryHandle> m_pool;
};


struct RendererImpl;


struct RenderTask : MT::Task
{
	RenderTask(RendererImpl& renderer, IAllocator& allocator) 
		: MT::Task(allocator)
		, m_allocator(allocator)
		, m_renderer(renderer)
		, m_profiler(allocator)
	{}


	int task() override;

	IAllocator& m_allocator;
	RendererImpl& m_renderer;
	ffr::FramebufferHandle m_framebuffer;
	ffr::BufferHandle m_global_state_uniforms;
	
	struct PreparedCommand
	{
		Renderer::RenderCommandBase* cmd;
		Renderer::MemRef data;
	};
	MT::LockFreeFixedQueue<Renderer::RenderCommandBase*, 256> m_commands;
	GPUProfiler m_profiler;
};

struct BoneProperty : Reflection::IEnumProperty
{
	BoneProperty() 
	{ 
		name = "Bone"; 
		getter_code = "RenderScene::getBoneAttachmentBone";
		setter_code = "RenderScene::setBoneAttachmentBone";
	}


	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		int value = scene->getBoneAttachmentBone(cmp.entity);
		stream.write(value);
	}


	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		int value = stream.read<int>();
		scene->setBoneAttachmentBone(cmp.entity, value);
	}


	Entity getModelInstance(RenderScene* render_scene, Entity bone_attachment) const
	{
		Entity parent_entity = render_scene->getBoneAttachmentParent(bone_attachment);
		if (parent_entity == INVALID_ENTITY) return INVALID_ENTITY;
		return render_scene->getUniverse().hasComponent(parent_entity, MODEL_INSTANCE_TYPE) ? parent_entity : INVALID_ENTITY;
	}


	int getEnumValueIndex(ComponentUID cmp, int value) const override  { return value; }
	int getEnumValue(ComponentUID cmp, int index) const override { return index; }


	int getEnumCount(ComponentUID cmp) const override
	{
		RenderScene* render_scene = static_cast<RenderScene*>(cmp.scene);
		Entity model_instance = getModelInstance(render_scene, cmp.entity);
		if (!model_instance.isValid()) return 0;

		auto* model = render_scene->getModelInstanceModel(model_instance);
		if (!model || !model->isReady()) return 0;

		return model->getBoneCount();
	}


	const char* getEnumName(ComponentUID cmp, int index) const override
	{
		RenderScene* render_scene = static_cast<RenderScene*>(cmp.scene);
		Entity model_instance = getModelInstance(render_scene, cmp.entity);
		if (!model_instance.isValid()) return "";

		auto* model = render_scene->getModelInstanceModel(model_instance);
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
		component("particle_emitter_spawn_shape",
			property("Radius", LUMIX_PROP(RenderScene, ParticleEmitterShapeRadius))
		),
		component("particle_emitter_plane",
			property("Bounce", LUMIX_PROP(RenderScene, ParticleEmitterPlaneBounce),
				ClampAttribute(0, 1)),
			array("Planes", &RenderScene::getParticleEmitterPlaneCount, &RenderScene::addParticleEmitterPlane, &RenderScene::removeParticleEmitterPlane, 
				property("Entity", LUMIX_PROP(RenderScene, ParticleEmitterPlaneEntity))
			)
		),
		component("particle_emitter_attractor",
			property("Force", LUMIX_PROP(RenderScene, ParticleEmitterAttractorForce)),
			array("Attractors", &RenderScene::getParticleEmitterAttractorCount, &RenderScene::addParticleEmitterAttractor, &RenderScene::removeParticleEmitterAttractor,
				property("Entity", LUMIX_PROP(RenderScene, ParticleEmitterAttractorEntity))
			)
		),
		component("particle_emitter_alpha",
			sampled_func_property("Alpha", LUMIX_PROP(RenderScene, ParticleEmitterAlpha), &RenderScene::getParticleEmitterAlphaCount, 1)
		),
		component("particle_emitter_random_rotation"),
		component("environment_probe",
			property("Enabled reflection", LUMIX_PROP_FULL(RenderScene, isEnvironmentProbeReflectionEnabled, enableEnvironmentProbeReflection)),
			property("Override global size", LUMIX_PROP_FULL(RenderScene, isEnvironmentProbeCustomSize, enableEnvironmentProbeCustomSize)),
			property("Radiance size", LUMIX_PROP(RenderScene, EnvironmentProbeRadianceSize)),
			property("Irradiance size", LUMIX_PROP(RenderScene, EnvironmentProbeIrradianceSize))
		),
		component("particle_emitter_force",
			property("Acceleration", LUMIX_PROP(RenderScene, ParticleEmitterAcceleration))
		),
		component("particle_emitter_subimage",
			property("Rows", LUMIX_PROP(RenderScene, ParticleEmitterSubimageRows)),
			property("Columns", LUMIX_PROP(RenderScene, ParticleEmitterSubimageCols))
		),
		component("particle_emitter_size",
			sampled_func_property("Size", LUMIX_PROP(RenderScene, ParticleEmitterSize), &RenderScene::getParticleEmitterSizeCount, 1)
		),
		component("scripted_particle_emitter",
			property("Material", LUMIX_PROP(RenderScene, ScriptedParticleEmitterMaterialPath),
				ResourceAttribute("Material (*.mat)", Material::TYPE))
		),
		component("particle_emitter",
			property("Life", LUMIX_PROP(RenderScene, ParticleEmitterInitialLife)),
			property("Initial size", LUMIX_PROP(RenderScene, ParticleEmitterInitialSize)),
			property("Spawn period", LUMIX_PROP(RenderScene, ParticleEmitterSpawnPeriod)),
			property("Autoemit", LUMIX_PROP(RenderScene, ParticleEmitterAutoemit)),
			property("Local space", LUMIX_PROP(RenderScene, ParticleEmitterLocalSpace)),
			property("Material", LUMIX_PROP(RenderScene, ParticleEmitterMaterialPath),
				ResourceAttribute("Material (*.mat)", Material::TYPE)),
			property("Spawn count", LUMIX_PROP(RenderScene, ParticleEmitterSpawnCount))
		),
		component("particle_emitter_linear_movement",
			property("x", LUMIX_PROP(RenderScene, ParticleEmitterLinearMovementX)),
			property("y", LUMIX_PROP(RenderScene, ParticleEmitterLinearMovementY)),
			property("z", LUMIX_PROP(RenderScene, ParticleEmitterLinearMovementZ))
		),
		component("camera",
			property("Orthographic size", LUMIX_PROP(RenderScene, CameraOrthoSize), 
				MinAttribute(0)),
			property("Orthographic", LUMIX_PROP_FULL(RenderScene, isCameraOrtho, setCameraOrtho)),
			property("FOV", LUMIX_PROP(RenderScene, CameraFOV),
				RadiansAttribute()),
			property("Near", LUMIX_PROP(RenderScene, CameraNearPlane), 
				MinAttribute(0)),
			property("Far", LUMIX_PROP(RenderScene, CameraFarPlane), 
				MinAttribute(0))
		),
		component("renderable",
			property("Enabled", LUMIX_PROP_FULL(RenderScene, isModelInstanceEnabled, enableModelInstance)),
			property("Source", LUMIX_PROP(RenderScene, ModelInstancePath),
				ResourceAttribute("Mesh (*.msh)", Model::TYPE)),
			const_array("Materials", &RenderScene::getModelInstanceMaterialsCount, 
				property("Source", LUMIX_PROP(RenderScene, ModelInstanceMaterial),
					ResourceAttribute("Material (*.mat)", Material::TYPE))
			)
		),
		component("global_light",
			property("Color", LUMIX_PROP(RenderScene, GlobalLightColor),
				ColorAttribute()),
			property("Intensity", LUMIX_PROP(RenderScene, GlobalLightIntensity), 
				MinAttribute(0)),
			property("Indirect intensity", LUMIX_PROP(RenderScene, GlobalLightIndirectIntensity), MinAttribute(0)),
			property("Fog density", LUMIX_PROP(RenderScene, FogDensity),
				ClampAttribute(0, 1)),
			property("Fog bottom", LUMIX_PROP(RenderScene, FogBottom)),
			property("Fog height", LUMIX_PROP(RenderScene, FogHeight), 
				MinAttribute(0)),
			property("Fog color", LUMIX_PROP(RenderScene, FogColor),
				ColorAttribute()),
			property("Shadow cascades", LUMIX_PROP(RenderScene, ShadowmapCascades))
		),
		component("point_light",
			property("Diffuse color", LUMIX_PROP(RenderScene, PointLightColor),
				ColorAttribute()),
			property("Specular color", LUMIX_PROP(RenderScene, PointLightSpecularColor),
				ColorAttribute()),
			property("Diffuse intensity", LUMIX_PROP(RenderScene, PointLightIntensity), 
				MinAttribute(0)),
			property("Specular intensity", LUMIX_PROP(RenderScene, PointLightSpecularIntensity)),
			property("FOV", LUMIX_PROP(RenderScene, LightFOV), 
				ClampAttribute(0, 360),
				RadiansAttribute()),
			property("Attenuation", LUMIX_PROP(RenderScene, LightAttenuation),
				ClampAttribute(0, 1000)),
			property("Range", LUMIX_PROP(RenderScene, LightRange), 
				MinAttribute(0)),
			property("Cast shadows", LUMIX_PROP(RenderScene, LightCastShadows), 
				MinAttribute(0))
		),
		component("text_mesh",
			property("Text", LUMIX_PROP(RenderScene, TextMeshText)),
			property("Font", LUMIX_PROP(RenderScene, TextMeshFontPath),
				ResourceAttribute("Font (*.ttf)", FontResource::TYPE)),
			property("Font Size", LUMIX_PROP(RenderScene, TextMeshFontSize)),
			property("Color", LUMIX_PROP(RenderScene, TextMeshColorRGBA),
				ColorAttribute()),
			property("Camera-oriented", LUMIX_PROP_FULL(RenderScene, isTextMeshCameraOriented, setTextMeshCameraOriented))
		),
		component("decal",
			property("Material", LUMIX_PROP(RenderScene, DecalMaterialPath),
				ResourceAttribute("Material (*.mat)", Material::TYPE)),
			property("Scale", LUMIX_PROP(RenderScene, DecalScale), 
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


struct RendererImpl LUMIX_FINAL : public Renderer
{
	explicit RendererImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_texture_manager(*this, m_allocator)
		, m_model_manager(*this, m_allocator)
		, m_material_manager(*this, m_allocator)
		, m_shader_manager(*this, m_allocator)
		, m_font_manager(nullptr)
		, m_shader_defines(m_allocator)
		, m_layers(m_allocator)
		, m_vsync(true)
		, m_main_pipeline(nullptr)
		, m_render_task(*this, m_allocator)
		, m_frame_semaphore(2, 2)
	{
		registerProperties(engine.getAllocator());
		char cmd_line[4096];
		getCommandLine(cmd_line, lengthOf(cmd_line));
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

		ResourceManager& manager = engine.getResourceManager();
		m_texture_manager.create(Texture::TYPE, manager);
		m_model_manager.create(Model::TYPE, manager);
		m_material_manager.create(Material::TYPE, manager);
		m_shader_manager.create(Shader::TYPE, manager);
		m_font_manager = LUMIX_NEW(m_allocator, FontManager)(*this, m_allocator);
		m_font_manager->create(FontResource::TYPE, manager);

		m_default_shader = static_cast<Shader*>(m_shader_manager.load(Path("pipelines/standard.shd")));
		RenderScene::registerLuaAPI(m_engine.getState());
		m_layers.emplace("default");
		m_layers.emplace("transparent");
		m_layers.emplace("water");
		m_layers.emplace("fur");

		m_render_task.create("render task");
	}


	~RendererImpl()
	{
		m_shader_manager.unload(*m_default_shader);
		m_texture_manager.destroy();
		m_model_manager.destroy();
		m_material_manager.destroy();
		m_shader_manager.destroy();
		m_font_manager->destroy();
		LUMIX_DELETE(m_allocator, m_font_manager);

		frame(false);
		frame(false);
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


	void beginProfileBlock(const char* name) override
	{
		m_render_task.m_profiler.beginQuery(name);
	}


	void endProfileBlock() override
	{
		m_render_task.m_profiler.endQuery();
	}


	bool getGPUTimings(Array<GPUProfilerQuery>* results) override
	{
		return m_render_task.m_profiler.getResults(results);
	}


	ffr::FramebufferHandle getFramebuffer() const override
	{
		return m_render_task.m_framebuffer;
	}


	ffr::TextureHandle loadTexture(const MemRef& memory, u32 flags, ffr::TextureInfo* info) override
	{
		ASSERT(memory.size > 0);

		const ffr::TextureHandle handle = ffr::allocTextureHandle();
		if (!handle.isValid()) return handle;

		ffr::TextureInfo tmp_info = ffr::getTextureInfo(memory.data);
		if(info) {
			*info = tmp_info;
		}

		struct Cmd : RenderCommandBase {
			void setup() override {}
			void execute() override {
				ffr::loadTexture(handle, memory.data, memory.size, flags, nullptr);
			}

			ffr::TextureHandle handle;
			MemRef memory;
			u32 flags;
		};

		Cmd* cmd = LUMIX_NEW(m_render_task.m_allocator, Cmd);
		cmd->handle = handle;
		cmd->memory = memory;
		cmd->flags = flags;
		push(cmd);

		return handle;
	}


	ffr::BufferHandle createBuffer(const MemRef& memory) override
	{
		ffr::BufferHandle handle = ffr::allocBufferHandle();
		if(!handle.isValid()) return handle;

		struct Cmd : RenderCommandBase {
			void setup() override {}
			void execute() override {
				ffr::createBuffer(handle, memory.size, memory.data);
			}

			ffr::BufferHandle handle;
			MemRef memory;
			ffr::TextureFormat format;
		};

		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		cmd->handle = handle;
		cmd->memory = memory;
		push(cmd);

		return handle;
	}


	void destroy(ffr::BufferHandle buffer) override
	{
		struct Cmd : RenderCommandBase {
			void setup() override {}
			void execute() override { ffr::destroy(buffer); }

			ffr::BufferHandle buffer;
			RendererImpl* renderer;
		};

		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		cmd->buffer = buffer;
		cmd->renderer = this;
		push(cmd);
	}


	ffr::TextureHandle createTexture(uint w, uint h, ffr::TextureFormat format, u32 flags, const MemRef& memory) override
	{
		ffr::TextureHandle handle = ffr::allocTextureHandle();
		if(!handle.isValid()) return handle;

		struct Cmd : RenderCommandBase {
			void setup() override {}
			void execute() override { ffr::createTexture(handle, w, h, format, 0, memory.data); }

			ffr::TextureHandle handle;
			MemRef memory;
			uint w;
			uint h;
			ffr::TextureFormat format;
		};

		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		cmd->handle = handle;
		cmd->memory = memory;
		cmd->format = format;
		cmd->w = w;
		cmd->h = h;
		push(cmd);

		return handle;
	}


	void destroy(ffr::TextureHandle tex)
	{
		struct Cmd : RenderCommandBase {
			void setup() override {}
			void execute() override { ffr::destroy(texture); }

			ffr::TextureHandle texture;
			RendererImpl* renderer;
		};

		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		cmd->texture = tex;
		cmd->renderer = this;
		push(cmd);
	}

	struct RenderCommandSetupJobData {
		RenderCommandBase* cmd;
		RenderCommandSetupJobData* prev;
		volatile int counter;
		RendererImpl* renderer;
	};


	RenderCommandSetupJobData* m_last_job = nullptr;


	void push(RenderCommandBase* cmd) override
	{
		PROFILE_FUNCTION();
		RenderCommandSetupJobData* data = LUMIX_NEW(m_allocator, RenderCommandSetupJobData);
		data->cmd = cmd;
		data->prev = m_last_job;
		data->counter = 0;
		data->renderer = this;

		JobSystem::JobDecl job;
		job.data = data;
		job.task = [](void* data){
			RenderCommandSetupJobData* job_data = (RenderCommandSetupJobData*)data;
			RenderCommandBase* cmd = job_data->cmd;
			RendererImpl* renderer = job_data->renderer;

			cmd->setup();

			if (job_data->prev) {
				JobSystem::wait(&job_data->prev->counter);
				LUMIX_DELETE(renderer->m_allocator, job_data->prev);
			}

			Renderer::RenderCommandBase** rt_cmd = renderer->m_render_task.m_commands.alloc(true);
			*rt_cmd = cmd;
			renderer->m_render_task.m_commands.push(rt_cmd, true);
		};

		JobSystem::runJobs(&job, 1, &data->counter);
		m_last_job = data;
	}


	void setMainPipeline(Pipeline* pipeline) override
	{
		m_main_pipeline = pipeline;
	}


	void setGlobalState(const GlobalState& state) override
	{
		m_global_state = state;
		pushSetGlobalStateCommand();
	}


	GlobalState getGlobalState() const override
	{
		return m_global_state;
	}


	Pipeline* getMainPipeline() override
	{
		return m_main_pipeline;
	}


	int getLayer(const char* name) override
	{
		for (int i = 0; i < m_layers.size(); ++i)
		{
			if (m_layers[i] == name) return i;
		}
		ASSERT(m_layers.size() < 64);
		m_layers.emplace() = name;
		return m_layers.size() - 1;
	}


	int getLayersCount() const override { return m_layers.size(); }
	const char* getLayerName(int idx) const override { return m_layers[idx]; }


	ModelManager& getModelManager() override { return m_model_manager; }
	MaterialManager& getMaterialManager() override { return m_material_manager; }
	ShaderManager& getShaderManager() override { return m_shader_manager; }
	TextureManager& getTextureManager() override { return m_texture_manager; }
	FontManager& getFontManager() override { return *m_font_manager; }
// TODO
	/*
	const bgfx::VertexDecl& getBasicVertexDecl() const override { static bgfx::VertexDecl v; return v; }
	const bgfx::VertexDecl& getBasic2DVertexDecl() const override { static bgfx::VertexDecl v; return v; }
	*/

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
// TODO
	/*
	const bgfx::UniformHandle& getMaterialColorUniform() const override { static bgfx::UniformHandle v; return v; }
	const bgfx::UniformHandle& getRoughnessMetallicEmissionUniform() const override { static bgfx::UniformHandle v; return v; }
	*/
	void makeScreenshot(const Path& filename) override {  }
	void resize(int w, int h) override {  }
	Shader* getDefaultShader() override { return m_default_shader; }


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
			g_log_error.log("Renderer") << "Too many shader defines.";
		}

		m_shader_defines.emplace(define);
		return m_shader_defines.size() - 1;
	}


	void pushSetGlobalStateCommand()
	{
		struct Cmd : RenderCommandBase {
			void setup() override {}
			void execute() override { 
				ffr::update(renderer->m_render_task.m_global_state_uniforms, &state, 0, sizeof(state));
			}
			GlobalState state;
			RendererImpl* renderer;
		};
		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		cmd->state = m_global_state;
		cmd->renderer = this;
		push(cmd);
	}


	void pushSwapCommand()
	{
		struct SwapCmd : RenderCommandBase {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				renderer->m_frame_semaphore.signal();
				ffr::swapBuffers(); 
				renderer->m_render_task.m_profiler.frame();
			}
			RendererImpl* renderer;
		};
		SwapCmd* swap_cmd = LUMIX_NEW(m_allocator, SwapCmd);
		swap_cmd->renderer = this;
		push(swap_cmd);
	}


	void frame(bool capture) override
	{
		PROFILE_FUNCTION();
		pushSwapCommand();
		{
			PROFILE_BLOCK("wait for render thread");
			m_frame_semaphore.wait();
		}
		if (m_last_job) {
			JobSystem::wait(&m_last_job->counter);
			LUMIX_DELETE(m_allocator, m_last_job);
			m_last_job = nullptr;
		}
	}


	using ShaderDefine = StaticString<32>;
	using Layer = StaticString<32>;


	Engine& m_engine;
	IAllocator& m_allocator;
	MT::Semaphore m_frame_semaphore;
	Array<ShaderDefine> m_shader_defines;
	Array<Layer> m_layers;
	TextureManager m_texture_manager;
	MaterialManager m_material_manager;
	FontManager* m_font_manager;
	ShaderManager m_shader_manager;
	ModelManager m_model_manager;
	bool m_vsync;
	Shader* m_default_shader;
	Pipeline* m_main_pipeline;
	RenderTask m_render_task;
	GlobalState m_global_state;
};


int RenderTask::task()
{
	PROFILE_FUNCTION();
	void* window_handle = m_renderer.getEngine().getPlatformData().window_handle;
	ffr::init(window_handle);
	m_framebuffer = ffr::createFramebuffer(0, nullptr);
	m_global_state_uniforms = ffr::allocBufferHandle();
	ffr::createBuffer(m_global_state_uniforms, sizeof(Renderer::GlobalState), nullptr); 
	ffr::bindUniformBuffer(0, m_global_state_uniforms, 0, sizeof(Renderer::GlobalState));

	for(;;) {
		Profiler::beginBlock("get command");
		Renderer::RenderCommandBase** rt_cmd = m_commands.pop(true);
		Renderer::RenderCommandBase* cmd = *rt_cmd;
		m_commands.dealoc(rt_cmd);
		Profiler::endBlock();

		PROFILE_BLOCK("executeCommand");
		cmd->execute();
		LUMIX_DELETE(m_renderer.getAllocator(), cmd);
	}
	return 0;
}


extern "C"
{
	LUMIX_PLUGIN_ENTRY(renderer)
	{
		return LUMIX_NEW(engine.getAllocator(), RendererImpl)(engine);
	}
}


} // namespace Lumix



