#include "renderer.h"

#include "engine/array.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/debug/debug.h"
#include "engine/engine.h"
#include "engine/fs/os_file.h"
#include "engine/log.h"
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
#include "renderer/render_scene.h"
#include "renderer/shader.h"
#include "renderer/shader_manager.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"
#include "renderer/texture_manager.h"
#include <bgfx/bgfx.h>
#include <cstdio>


namespace bx
{

	struct AllocatorI
	{
		virtual ~AllocatorI() = default;

		/// Allocated, resizes memory block or frees memory.
		///
		/// @param[in] _ptr If _ptr is NULL new block will be allocated.
		/// @param[in] _size If _ptr is set, and _size is 0, memory will be freed.
		/// @param[in] _align Alignment.
		/// @param[in] _file Debug file path info.
		/// @param[in] _line Debug file line info.
		virtual void* realloc(void* _ptr, size_t _size, size_t _align, const char* _file, uint32_t _line) = 0;
	};

} // namespace bx


namespace bgfx
{

struct PlatformData
{
	void* ndt;			//< Native display type
	void* nwh;			//< Native window handle
	void* context;		//< GL context, or D3D device
	void* backBuffer;   //< GL backbuffer, or D3D render target view
	void* backBufferDS; //< Backbuffer depth/stencil.
};


void setPlatformData(const PlatformData& _pd);

} // namespace bgfx


namespace Lumix
{


static const ResourceType MATERIAL_TYPE("material");
static const ResourceType MODEL_TYPE("model");
static const ResourceType SHADER_TYPE("shader");
static const ResourceType FONT_TYPE("font");
static const ResourceType TEXTURE_TYPE("texture");
static const ResourceType SHADER_BINARY_TYPE("shader_binary");


static const char* getGrassRotationModeName(int index) 
{
	switch ((Terrain::GrassType::RotationMode)index)
	{
		case Terrain::GrassType::RotationMode::ALL_RANDOM: return "XYZ Random";
		case Terrain::GrassType::RotationMode::Y_UP: return "Y Up";
		case Terrain::GrassType::RotationMode::ALIGN_WITH_NORMAL: return "Align with normal";
		default: ASSERT(false); return "Error";
	}
}


struct BoneProperty : Reflection::IEnumProperty
{
	BoneProperty() { name = "Bone"; }


	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		int value = scene->getBoneAttachmentBone(cmp.handle);
		stream.write(value);
	}


	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		int value = stream.read<int>();
		scene->setBoneAttachmentBone(cmp.handle, value);
	}


	ComponentHandle getModelInstance(RenderScene* render_scene, ComponentHandle bone_attachment_cmp) const
	{
		Entity parent_entity = render_scene->getBoneAttachmentParent(bone_attachment_cmp);
		if (parent_entity == INVALID_ENTITY) return INVALID_COMPONENT;
		ComponentHandle model_instance = render_scene->getModelInstanceComponent(parent_entity);
		return model_instance;
	}


	int getEnumCount(ComponentUID cmp) const override
	{
		RenderScene* render_scene = static_cast<RenderScene*>(cmp.scene);
		ComponentHandle model_instance = getModelInstance(render_scene, cmp.handle);
		if (model_instance == INVALID_COMPONENT) return 0;
		auto* model = render_scene->getModelInstanceModel(model_instance);
		if (!model || !model->isReady()) return 0;
		return model->getBoneCount();
	}


	const char* getEnumName(ComponentUID cmp, int index) const override
	{
		RenderScene* render_scene = static_cast<RenderScene*>(cmp.scene);
		ComponentHandle model_instance = getModelInstance(render_scene, cmp.handle);
		if (model_instance == INVALID_COMPONENT) return "";
		auto* model = render_scene->getModelInstanceModel(model_instance);
		if (!model) return "";
		return model->getBone(index).name.c_str();
	}
};


static void registerProperties(IAllocator& allocator)
{
	using namespace Reflection;

	//&RenderScene::getParticleEmitterShapeRadius

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
		component("environment_probe"),
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
				ResourceAttribute("Material (*.mat)", MATERIAL_TYPE))
		),
		component("particle_emitter",
			property("Life", LUMIX_PROP(RenderScene, ParticleEmitterInitialLife)),
			property("Initial size", LUMIX_PROP(RenderScene, ParticleEmitterInitialSize)),
			property("Spawn period", LUMIX_PROP(RenderScene, ParticleEmitterSpawnPeriod)),
			property("Autoemit", LUMIX_PROP(RenderScene, ParticleEmitterAutoemit)),
			property("Local space", LUMIX_PROP(RenderScene, ParticleEmitterLocalSpace)),
			property("Material", LUMIX_PROP(RenderScene, ParticleEmitterMaterialPath),
				ResourceAttribute("Material (*.mat)", MATERIAL_TYPE)),
			property("Spawn count", LUMIX_PROP(RenderScene, ParticleEmitterSpawnCount))
		),
		component("particle_emitter_linear_movement",
			property("x", LUMIX_PROP(RenderScene, ParticleEmitterLinearMovementX)),
			property("y", LUMIX_PROP(RenderScene, ParticleEmitterLinearMovementY)),
			property("z", LUMIX_PROP(RenderScene, ParticleEmitterLinearMovementZ))
		),
		component("camera",
			property("Slot", LUMIX_PROP(RenderScene, CameraSlot)),
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
				ResourceAttribute("Mesh (*.msh)", MODEL_TYPE)),
			property("Keep skin", LUMIX_PROP(RenderScene, ModelInstanceKeepSkin)),
			const_array("Materials", &RenderScene::getModelInstanceMaterialsCount, 
				property("Source", LUMIX_PROP(RenderScene, ModelInstanceMaterial),
					ResourceAttribute("Material (*.mat)", MATERIAL_TYPE))
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
		component("decal",
			property("Material", LUMIX_PROP(RenderScene, DecalMaterialPath),
				ResourceAttribute("Material (*.mat)", MATERIAL_TYPE)),
			property("Scale", LUMIX_PROP(RenderScene, DecalScale), 
				MinAttribute(0))
		),
		component("terrain",
			property("Material", LUMIX_PROP(RenderScene, TerrainMaterialPath),
				ResourceAttribute("Material (*.mat)", MATERIAL_TYPE)),
			property("XZ scale", LUMIX_PROP(RenderScene, TerrainXZScale), 
				MinAttribute(0)),
			property("Height scale", LUMIX_PROP(RenderScene, TerrainYScale), 
				MinAttribute(0)),
			array("grass", &RenderScene::getGrassCount, &RenderScene::addGrass, &RenderScene::removeGrass,
				property("Mesh", LUMIX_PROP(RenderScene, GrassPath),
					ResourceAttribute("Mesh (*.msh)", MODEL_TYPE)),
				property("Distance", LUMIX_PROP(RenderScene, GrassDistance),
					MinAttribute(1)),
				property("Density", LUMIX_PROP(RenderScene, GrassDensity)),
				enum_property("Mode", LUMIX_PROP(RenderScene, GrassRotationMode), (int)Terrain::GrassType::RotationMode::COUNT, getGrassRotationModeName)
			)
		)
	);
	registerScene(render_scene);
}


struct BGFXAllocator LUMIX_FINAL : public bx::AllocatorI
{

	explicit BGFXAllocator(IAllocator& source)
		: m_source(source)
	{
	}


	static const size_t NATURAL_ALIGNEMENT = 8;


	void* realloc(void* _ptr, size_t _size, size_t _alignment, const char*, u32) override
	{
		if (0 == _size)
		{
			if (_ptr)
			{
				if (NATURAL_ALIGNEMENT >= _alignment)
				{
					m_source.deallocate(_ptr);
					return nullptr;
				}

				m_source.deallocate_aligned(_ptr);
			}

			return nullptr;
		}
		else if (!_ptr)
		{
			if (NATURAL_ALIGNEMENT >= _alignment) return m_source.allocate(_size);

			return m_source.allocate_aligned(_size, _alignment);
		}

		if (NATURAL_ALIGNEMENT >= _alignment) return m_source.reallocate(_ptr, _size);

		return m_source.reallocate_aligned(_ptr, _size, _alignment);
	}


	IAllocator& m_source;
};


struct RendererImpl LUMIX_FINAL : public Renderer
{
	struct CallbackStub LUMIX_FINAL : public bgfx::CallbackI
	{
		explicit CallbackStub(RendererImpl& renderer)
			: m_renderer(renderer)
		{}


		void fatal(bgfx::Fatal::Enum _code, const char* _str) override
		{
			g_log_error.log("Renderer") << _str;
			if (bgfx::Fatal::DebugCheck == _code || bgfx::Fatal::InvalidShader == _code)
			{
				Debug::debugBreak();
			}
			else
			{
				abort();
			}
		}


		void traceVargs(const char* _filePath,
			u16 _line,
			const char* _format,
			va_list _argList) override
		{
		}


		void screenShot(const char* filePath,
			uint32_t width,
			uint32_t height,
			uint32_t pitch,
			const void* data,
			uint32_t size,
			bool yflip) override
		{
			TGAHeader header;
			setMemory(&header, 0, sizeof(header));
			int bytes_per_pixel = 4;
			header.bitsPerPixel = (char)(bytes_per_pixel * 8);
			header.height = (short)height;
			header.width = (short)width;
			header.dataType = 2;

			FS::OsFile file;
			if(!file.open(filePath, FS::Mode::CREATE_AND_WRITE))
			{
				g_log_error.log("Renderer") << "Failed to save screenshot to " << filePath;
				return;
			}
			file.write(&header, sizeof(header));

			for(u32 i = 0; i < height; ++i)
				file.write((const u8*)data + pitch * i, width * 4);
			file.close();
		}


		void captureBegin(u32,
			u32,
			u32,
			bgfx::TextureFormat::Enum,
			bool) override
		{
			ASSERT(false);
		}


		u32 cacheReadSize(u64) override { return 0; }
		bool cacheRead(u64, void*, u32) override { return false; }
		void cacheWrite(u64, const void*, u32) override {}
		void captureEnd() override { ASSERT(false); }
		void captureFrame(const void*, u32) override { ASSERT(false); }

		void setThreadName()
		{
			if (m_is_thread_name_set) return;
			m_is_thread_name_set = true;
			Profiler::setThreadName("bgfx thread");
		}

		void profilerBegin(
			const char* _name
			, uint32_t _abgr
			, const char* _filePath
			, uint16_t _line
		) override
		{
			setThreadName();
			Profiler::beginBlock("bgfx_dynamic");
		}

		void profilerBeginLiteral(
			const char* _name
			, uint32_t _abgr
			, const char* _filePath
			, uint16_t _line
		) override
		{
			setThreadName();
			Profiler::beginBlock(_name);
		}


		void profilerEnd() override
		{
			Profiler::endBlock();
		}

		bool m_is_thread_name_set = false;
		RendererImpl& m_renderer;
	};


	explicit RendererImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_texture_manager(m_allocator)
		, m_model_manager(*this, m_allocator)
		, m_material_manager(*this, m_allocator)
		, m_shader_manager(*this, m_allocator)
		, m_font_manager(nullptr)
		, m_shader_binary_manager(*this, m_allocator)
		, m_passes(m_allocator)
		, m_shader_defines(m_allocator)
		, m_layers(m_allocator)
		, m_bgfx_allocator(m_allocator)
		, m_callback_stub(*this)
		, m_vsync(true)
		, m_main_pipeline(nullptr)
	{
		registerProperties(engine.getAllocator());
		bgfx::PlatformData d;
		void* window_handle = engine.getPlatformData().window_handle;
		void* display = engine.getPlatformData().display;
		if (window_handle)
		{
			setMemory(&d, 0, sizeof(d));
			d.nwh = window_handle;
			d.ndt = display;
			bgfx::setPlatformData(d);
		}
		char cmd_line[4096];
		bgfx::RendererType::Enum renderer_type = bgfx::RendererType::Count;
		getCommandLine(cmd_line, lengthOf(cmd_line));
		CommandLineParser cmd_line_parser(cmd_line);
		m_vsync = true;
		while (cmd_line_parser.next())
		{
			if (cmd_line_parser.currentEquals("-opengl"))
			{
				renderer_type = bgfx::RendererType::OpenGL;
				break;
			}
			else if (cmd_line_parser.currentEquals("-no_vsync"))
			{
				m_vsync = false;
				break;
			}
		}

		bool res = bgfx::init(renderer_type, 0, 0, &m_callback_stub, &m_bgfx_allocator);
		ASSERT(res);
		bgfx::reset(800, 600, m_vsync ? BGFX_RESET_VSYNC : 0);
		bgfx::setDebug(BGFX_DEBUG_TEXT | BGFX_DEBUG_PROFILER);

		ResourceManager& manager = engine.getResourceManager();
		m_texture_manager.create(TEXTURE_TYPE, manager);
		m_model_manager.create(MODEL_TYPE, manager);
		m_material_manager.create(MATERIAL_TYPE, manager);
		m_shader_manager.create(SHADER_TYPE, manager);
		m_font_manager = LUMIX_NEW(m_allocator, FontManager)(*this, m_allocator);
		m_font_manager->create(FONT_TYPE, manager);
		m_shader_binary_manager.create(SHADER_BINARY_TYPE, manager);

		m_current_pass_hash = crc32("MAIN");
		m_view_counter = 0;
		m_mat_color_uniform =
			bgfx::createUniform("u_materialColor", bgfx::UniformType::Vec4);
		m_roughness_metallic_uniform =
			bgfx::createUniform("u_roughnessMetallic", bgfx::UniformType::Vec4);

		m_basic_vertex_decl.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.end();
		m_basic_2d_vertex_decl.begin()
			.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
			.end();

		m_default_shader = static_cast<Shader*>(m_shader_manager.load(Path("pipelines/common/default.shd")));
		RenderScene::registerLuaAPI(m_engine.getState());
		m_layers.emplace("default");
		m_layers.emplace("transparent");
		m_layers.emplace("water");
		m_layers.emplace("fur");
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
		m_shader_binary_manager.destroy();

		bgfx::destroy(m_mat_color_uniform);
		bgfx::destroy(m_roughness_metallic_uniform);
		bgfx::frame();
		bgfx::frame();
		bgfx::shutdown();
	}


	void setMainPipeline(Pipeline* pipeline) override
	{
		m_main_pipeline = pipeline;
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
	TextureManager& getTextureManager() override { return m_texture_manager; }
	FontManager& getFontManager() override { return *m_font_manager; }
	const bgfx::VertexDecl& getBasicVertexDecl() const override { return m_basic_vertex_decl; }
	const bgfx::VertexDecl& getBasic2DVertexDecl() const override { return m_basic_2d_vertex_decl; }


	void createScenes(Universe& ctx) override
	{
		auto* scene = RenderScene::createInstance(*this, m_engine, ctx, m_allocator);
		ctx.addScene(scene);
	}


	void destroyScene(IScene* scene) override { RenderScene::destroyInstance(static_cast<RenderScene*>(scene)); }
	const char* getName() const override { return "renderer"; }
	Engine& getEngine() override { return m_engine; }
	int getShaderDefinesCount() const override { return m_shader_defines.size(); }
	const char* getShaderDefine(int define_idx) override { return m_shader_defines[define_idx]; }
	const char* getPassName(int idx) override { return m_passes[idx]; }
	const bgfx::UniformHandle& getMaterialColorUniform() const override { return m_mat_color_uniform; }
	const bgfx::UniformHandle& getRoughnessMetallicUniform() const override { return m_roughness_metallic_uniform; }
	void makeScreenshot(const Path& filename) override { bgfx::requestScreenShot(BGFX_INVALID_HANDLE, filename.c_str()); }
	void resize(int w, int h) override { bgfx::reset(w, h, m_vsync ? BGFX_RESET_VSYNC : 0); }
	int getViewCounter() const override { return m_view_counter; }
	void viewCounterAdd() override { ++m_view_counter; }
	Shader* getDefaultShader() override { return m_default_shader; }


	u8 getShaderDefineIdx(const char* define) override
	{
		for (int i = 0; i < m_shader_defines.size(); ++i)
		{
			if (m_shader_defines[i] == define)
			{
				ASSERT(i < 256);
				return i;
			}
		}

		m_shader_defines.emplace(define);
		return m_shader_defines.size() - 1;
	}


	int getPassIdx(const char* pass) override
	{
		if(stringLength(pass) > sizeof(m_passes[0].data))
		{
			g_log_error.log("Renderer") << "Pass name \"" << pass << "\" is too long.";
			return 0;
		}
		for (int i = 0; i < m_passes.size(); ++i)
		{
			if (m_passes[i] == pass)
			{
				return i;
			}
		}

		m_passes.emplace(pass);
		return m_passes.size() - 1;
	}


	void frame(bool capture) override
	{
		PROFILE_FUNCTION();
		bgfx::frame(capture);
		m_view_counter = 0;
	}


	using ShaderDefine = StaticString<32>;
	using Layer = StaticString<32>;


	Engine& m_engine;
	IAllocator& m_allocator;
	Array<ShaderCombinations::Pass> m_passes;
	Array<ShaderDefine> m_shader_defines;
	Array<Layer> m_layers;
	CallbackStub m_callback_stub;
	TextureManager m_texture_manager;
	MaterialManager m_material_manager;
	FontManager* m_font_manager;
	ShaderManager m_shader_manager;
	ShaderBinaryManager m_shader_binary_manager;
	ModelManager m_model_manager;
	u32 m_current_pass_hash;
	int m_view_counter;
	bool m_vsync;
	Shader* m_default_shader;
	BGFXAllocator m_bgfx_allocator;
	bgfx::VertexDecl m_basic_vertex_decl;
	bgfx::VertexDecl m_basic_2d_vertex_decl;
	bgfx::UniformHandle m_mat_color_uniform;
	bgfx::UniformHandle m_roughness_metallic_uniform;
	Pipeline* m_main_pipeline;
};


extern "C"
{
	LUMIX_PLUGIN_ENTRY(renderer)
	{
		return LUMIX_NEW(engine.getAllocator(), RendererImpl)(engine);
	}
}


} // namespace Lumix



