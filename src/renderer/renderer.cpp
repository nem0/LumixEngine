#include "renderer.h"

#include "engine/array.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/debug/debug.h"
#include "engine/engine.h"
#include "engine/fs/os_file.h"
#include "engine/lifo_allocator.h"
#include "engine/log.h"
#include "engine/profiler.h"
#include "engine/properties.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/system.h"
#include "engine/universe/component.h"
#include "engine/universe/universe.h"
#include "renderer/draw2d.h"
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
#include <cfloat>
#include <cstdio>


namespace bx
{

	struct AllocatorI
	{
		virtual ~AllocatorI() {}

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


static const ComponentType GLOBAL_LIGHT_TYPE = Properties::getComponentType("global_light");
static const ComponentType POINT_LIGHT_TYPE = Properties::getComponentType("point_light");
static const ComponentType MODEL_INSTANCE_TYPE = Properties::getComponentType("renderable");
static const ComponentType CAMERA_TYPE = Properties::getComponentType("camera");
static const ResourceType MATERIAL_TYPE("material");
static const ResourceType MODEL_TYPE("model");
static const ResourceType SHADER_TYPE("shader");
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


struct BoneProperty : Properties::IEnumProperty
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
	using namespace Properties;

	static auto render_scene = scene("renderer", 
		component("bone_attachment",
			property("Parent", &RenderScene::getBoneAttachmentParent, &RenderScene::setBoneAttachmentParent),
			property("Relative position", &RenderScene::getBoneAttachmentPosition, &RenderScene::setBoneAttachmentPosition),
			property("Relative rotation", &RenderScene::getBoneAttachmentRotation, &RenderScene::setBoneAttachmentRotation, 
				RadiansAttribute()),
			BoneProperty()
		),
		component("particle_emitter_spawn_shape",
			property("Radius", &RenderScene::getParticleEmitterShapeRadius, &RenderScene::setParticleEmitterShapeRadius)
		),
		component("particle_emitter_plane",
			property("Bounce", &RenderScene::getParticleEmitterPlaneBounce, &RenderScene::setParticleEmitterPlaneBounce,
				ClampAttribute(0, 1)),
			array("Planes", &RenderScene::getParticleEmitterPlaneCount, &RenderScene::addParticleEmitterPlane, &RenderScene::removeParticleEmitterPlane, 
				property("Entity", &RenderScene::getParticleEmitterPlaneEntity, &RenderScene::setParticleEmitterPlaneEntity)
			)
		),
		component("particle_emitter_attractor",
			property("Force", &RenderScene::getParticleEmitterAttractorForce, &RenderScene::setParticleEmitterAttractorForce),
			array("Attractors", &RenderScene::getParticleEmitterAttractorCount, &RenderScene::addParticleEmitterAttractor, &RenderScene::removeParticleEmitterAttractor,
				property("Entity", &RenderScene::getParticleEmitterAttractorEntity, &RenderScene::setParticleEmitterAttractorEntity)
			)
		),
		component("particle_emitter_alpha",
			sampled_func_property("Alpha", &RenderScene::getParticleEmitterAlpha, &RenderScene::setParticleEmitterAlpha, &RenderScene::getParticleEmitterAlphaCount, 1)
		),
		component("particle_emitter_random_rotation"),
		component("environment_probe"),
		component("particle_emitter_force",
			property("Acceleration", &RenderScene::getParticleEmitterAcceleration, &RenderScene::setParticleEmitterAcceleration)
		),
		component("particle_emitter_subimage",
			property("Rows", &RenderScene::getParticleEmitterSubimageRows, &RenderScene::setParticleEmitterSubimageRows),
			property("Columns", &RenderScene::getParticleEmitterSubimageCols, &RenderScene::setParticleEmitterSubimageCols)
		),
		component("particle_emitter_size",
			sampled_func_property("Size", &RenderScene::getParticleEmitterSize, &RenderScene::setParticleEmitterSize, &RenderScene::getParticleEmitterSizeCount, 1)
		),
		component("scripted_particle_emitter",
			property("Material", &RenderScene::getScriptedParticleEmitterMaterialPath, &RenderScene::setScriptedParticleEmitterMaterialPath,
				ResourceAttribute("Material (*.mat)", MATERIAL_TYPE))
		),
		component("particle_emitter",
			property("Life", &RenderScene::getParticleEmitterInitialLife, &RenderScene::setParticleEmitterInitialLife),
			property("Initial size", &RenderScene::getParticleEmitterInitialSize, &RenderScene::setParticleEmitterInitialSize),
			property("Spawn period", &RenderScene::getParticleEmitterSpawnPeriod, &RenderScene::setParticleEmitterSpawnPeriod),
			property("Autoemit", &RenderScene::getParticleEmitterAutoemit, &RenderScene::setParticleEmitterAutoemit),
			property("Local space", &RenderScene::getParticleEmitterLocalSpace, &RenderScene::setParticleEmitterLocalSpace),
			property("Material", &RenderScene::getParticleEmitterMaterialPath, &RenderScene::setParticleEmitterMaterialPath,
				ResourceAttribute("Material (*.mat)", MATERIAL_TYPE)),
			property("Spawn count", &RenderScene::getParticleEmitterSpawnCount, &RenderScene::setParticleEmitterSpawnCount)
		),
		component("particle_emitter_linear_movement",
			property("x", &RenderScene::getParticleEmitterLinearMovementX, &RenderScene::setParticleEmitterLinearMovementX),
			property("y", &RenderScene::getParticleEmitterLinearMovementY, &RenderScene::setParticleEmitterLinearMovementY),
			property("z", &RenderScene::getParticleEmitterLinearMovementZ, &RenderScene::setParticleEmitterLinearMovementZ)
		),
		component("camera",
			property("Slot", &RenderScene::getCameraSlot, &RenderScene::setCameraSlot),
			property("Orthographic size", &RenderScene::getCameraOrthoSize, &RenderScene::setCameraOrthoSize, 
				MinAttribute(0)),
			property("Orthographic", &RenderScene::isCameraOrtho, &RenderScene::setCameraOrtho),
			property("FOV", &RenderScene::getCameraFOV, &RenderScene::setCameraFOV,
				RadiansAttribute()),
			property("Near", &RenderScene::getCameraNearPlane, &RenderScene::setCameraNearPlane, 
				MinAttribute(0)),
			property("Far", &RenderScene::getCameraFarPlane, &RenderScene::setCameraFarPlane, 
				MinAttribute(0))
		),
		component("renderable",
			property("Source", &RenderScene::getModelInstancePath, &RenderScene::setModelInstancePath,
				ResourceAttribute("Mesh (*.msh)", MODEL_TYPE)),
			property("Keep skin", &RenderScene::getModelInstanceKeepSkin, &RenderScene::setModelInstanceKeepSkin),
			const_array("Materials", &RenderScene::getModelInstanceMaterialsCount, 
				property("Source", &RenderScene::getModelInstanceMaterial, &RenderScene::setModelInstanceMaterial,
					ResourceAttribute("Material (*.mat)", MATERIAL_TYPE))
			)
		),
		component("global_light",
			property("Color", &RenderScene::getGlobalLightColor, &RenderScene::setGlobalLightColor,
				ColorAttribute()),
			property("Intensity", &RenderScene::getGlobalLightIntensity, &RenderScene::setGlobalLightIntensity, 
				MinAttribute(0)),
			property("Indirect intensity", &RenderScene::getGlobalLightIndirectIntensity, &RenderScene::setGlobalLightIndirectIntensity, MinAttribute(0)),
			property("Fog density", &RenderScene::getFogDensity, &RenderScene::setFogDensity,
				ClampAttribute(0, 1)),
			property("Fog bottom", &RenderScene::getFogBottom, &RenderScene::setFogBottom),
			property("Fog height", &RenderScene::getFogHeight, &RenderScene::setFogHeight, 
				MinAttribute(0)),
			property("Fog color", &RenderScene::getFogColor, &RenderScene::setFogColor,
				ColorAttribute()),
			property("Shadow cascades", &RenderScene::getShadowmapCascades, &RenderScene::setShadowmapCascades)
		),
		component("point_light",
			property("Diffuse color", &RenderScene::getPointLightColor, &RenderScene::setPointLightColor,
				ColorAttribute()),
			property("Specular color", &RenderScene::getPointLightSpecularColor, &RenderScene::setPointLightSpecularColor,
				ColorAttribute()),
			property("Diffuse intensity", &RenderScene::getPointLightIntensity, &RenderScene::setPointLightIntensity, 
				MinAttribute(0)),
			property("Specular intensity", &RenderScene::getPointLightSpecularIntensity, &RenderScene::setPointLightSpecularIntensity, MinAttribute(0)),
			property("FOV", &RenderScene::getLightFOV, &RenderScene::setLightFOV, 
				ClampAttribute(0, 360),
				RadiansAttribute()),
			property("Attenuation", &RenderScene::getLightAttenuation, &RenderScene::setLightAttenuation,
				ClampAttribute(0, 1000)),
			property("Range", &RenderScene::getLightRange, &RenderScene::setLightRange, 
				MinAttribute(0)),
			property("Cast shadows", &RenderScene::getLightCastShadows, &RenderScene::setLightCastShadows, 
				MinAttribute(0))
		),
		component("decal",
			property("Material", &RenderScene::getDecalMaterialPath, &RenderScene::setDecalMaterialPath,
				ResourceAttribute("Material (*.mat)", MATERIAL_TYPE)),
			property("Scale", &RenderScene::getDecalScale, &RenderScene::setDecalScale, 
				MinAttribute(0))
		),
		component("terrain",
			property("Material", &RenderScene::getTerrainMaterialPath, &RenderScene::setTerrainMaterialPath,
				ResourceAttribute("Material (*.mat)", MATERIAL_TYPE)),
			property("XZ scale", &RenderScene::getTerrainXZScale, &RenderScene::setTerrainXZScale, 
				MinAttribute(0)),
			property("Height scale", &RenderScene::getTerrainYScale, &RenderScene::setTerrainYScale, 
				MinAttribute(0)),
			array("grass", &RenderScene::getGrassCount, &RenderScene::addGrass, &RenderScene::removeGrass,
				property("Mesh", &RenderScene::getGrassPath, &RenderScene::setGrassPath,
					ResourceAttribute("Mesh (*.msh)", MODEL_TYPE)),
				property("Distance", &RenderScene::getGrassDistance, &RenderScene::setGrassDistance,
					MinAttribute(1)),
				property("Density", &RenderScene::getGrassDensity, &RenderScene::setGrassDensity),
				enum_property("Mode", &RenderScene::getGrassRotationMode, &RenderScene::setGrassRotationMode, (int)Terrain::GrassType::RotationMode::COUNT, getGrassRotationModeName)
			)
		)
	);
	render_scene.registerScene();
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

			file.write(data, size);
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
		, m_model_manager(m_allocator)
		, m_material_manager(*this, m_allocator)
		, m_shader_manager(*this, m_allocator)
		, m_shader_binary_manager(*this, m_allocator)
		, m_passes(m_allocator)
		, m_shader_defines(m_allocator)
		, m_layers(m_allocator)
		, m_bgfx_allocator(m_allocator)
		, m_callback_stub(*this)
		, m_vsync(true)
		, m_font_atlas(m_allocator)
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
		initDraw2DFont();
	}


	~RendererImpl()
	{
		Texture* draw2d_texture = m_draw2d_material->getTexture(0);
		m_draw2d_material->setTexture(0, nullptr);
		if (draw2d_texture)
		{
			draw2d_texture->destroy();
			LUMIX_DELETE(m_engine.getAllocator(), draw2d_texture);
		}

		m_draw2d_material->getResourceManager().unload(*m_draw2d_material);

		m_shader_manager.unload(*m_default_shader);
		m_texture_manager.destroy();
		m_model_manager.destroy();
		m_material_manager.destroy();
		m_shader_manager.destroy();
		m_shader_binary_manager.destroy();

		bgfx::destroy(m_mat_color_uniform);
		bgfx::destroy(m_roughness_metallic_uniform);
		bgfx::frame();
		bgfx::frame();
		bgfx::shutdown();
	}


	void initDraw2DFont()
	{
		m_font_atlas.AddFontDefault();
		u8* pixels;
		int w, h;
		m_font_atlas.GetTexDataAsRGBA32(&pixels, &w, &h);
		auto* material_manager = m_engine.getResourceManager().get(MATERIAL_TYPE);
		Resource* resource = material_manager->load(Path("pipelines/common/draw2d.mat"));
		m_draw2d_material = (Material*)resource;
		
		Texture* old_texture = m_draw2d_material->getTexture(0);
		Texture* texture = LUMIX_NEW(m_engine.getAllocator(), Texture)(
			Path("draw2d_font"), *m_engine.getResourceManager().get(TEXTURE_TYPE), m_engine.getAllocator());
		
		texture->create(w, h, pixels);
		m_draw2d_material->setTexture(0, texture);
		if (old_texture)
		{
			old_texture->destroy();
			LUMIX_DELETE(m_engine.getAllocator(), old_texture);
		}

		m_font_atlas.TexID = &texture->handle;
	}


	void setMainPipeline(Pipeline* pipeline) override
	{
		m_main_pipeline = pipeline;
	}


	Pipeline* getMainPipeline() override
	{
		return m_main_pipeline;
	}


	FontAtlas& getFontAtlas() override
	{
		return m_font_atlas;
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
	FontAtlas m_font_atlas;
	Material* m_draw2d_material;
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



