#include "renderer.h"

#include "core/array.h"
#include "core/crc32.h"
#include "core/fs/os_file.h"
#include "core/lifo_allocator.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "debug/debug.h"
#include "engine.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include "renderer/material.h"
#include "renderer/material_manager.h"
#include "renderer/model.h"
#include "renderer/model_manager.h"
#include "renderer/render_scene.h"
#include "renderer/shader.h"
#include "renderer/shader_manager.h"
#include "renderer/texture.h"
#include "renderer/texture_manager.h"
#include "universe/universe.h"
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


static void registerProperties(IAllocator& allocator)
{
	PropertyRegister::registerComponentType("camera", "Camera");
	PropertyRegister::registerComponentType("global_light", "Global light");
	PropertyRegister::registerComponentType("renderable", "Mesh");
	PropertyRegister::registerComponentType("particle_emitter", "Particle emitter");
	PropertyRegister::registerComponentType(
		"particle_emitter_spawn_shape", "Particle emitter - spawn shape");
	PropertyRegister::registerComponentType("particle_emitter_fade", "Particle emitter - fade");
	PropertyRegister::registerComponentType("particle_emitter_plane", "Particle emitter - plane");
	PropertyRegister::registerComponentType("particle_emitter_force", "Particle emitter - force");
	PropertyRegister::registerComponentType(
		"particle_emitter_attractor", "Particle emitter - attractor");
	PropertyRegister::registerComponentType(
		"particle_emitter_linear_movement", "Particle emitter - linear movement");
	PropertyRegister::registerComponentType(
		"particle_emitter_random_rotation", "Particle emitter - random rotation");
	PropertyRegister::registerComponentType("particle_emitter_size", "Particle emitter - size");
	PropertyRegister::registerComponentType("point_light", "Point light");
	PropertyRegister::registerComponentType("terrain", "Terrain");

	PropertyRegister::registerComponentDependency("particle_emitter_fade", "particle_emitter");
	PropertyRegister::registerComponentDependency("particle_emitter_force", "particle_emitter");
	PropertyRegister::registerComponentDependency(
		"particle_emitter_linear_movement", "particle_emitter");
	PropertyRegister::registerComponentDependency(
		"particle_emitter_random_rotation", "particle_emitter");

	PropertyRegister::add("particle_emitter_spawn_shape",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Radius",
							  &RenderScene::getParticleEmitterShapeRadius,
							  &RenderScene::setParticleEmitterShapeRadius,
							  0.0f,
							  FLT_MAX,
							  0.01f,
							  allocator));

	PropertyRegister::add("particle_emitter_plane",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Bounce",
							  &RenderScene::getParticleEmitterPlaneBounce,
							  &RenderScene::setParticleEmitterPlaneBounce,
							  0.0f,
							  1.0f,
							  0.01f,
							  allocator));
	auto plane_module_planes = LUMIX_NEW(allocator, ArrayDescriptor<RenderScene>)("Planes",
		&RenderScene::getParticleEmitterPlaneCount,
		&RenderScene::addParticleEmitterPlane,
		&RenderScene::removeParticleEmitterPlane,
		allocator);
	plane_module_planes->addChild(
		LUMIX_NEW(allocator, EntityPropertyDescriptor<RenderScene>)("Entity",
			&RenderScene::getParticleEmitterPlaneEntity,
			&RenderScene::setParticleEmitterPlaneEntity,
			allocator));
	PropertyRegister::add("particle_emitter_plane", plane_module_planes);

	PropertyRegister::add("particle_emitter_attractor",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Force",
							  &RenderScene::getParticleEmitterAttractorForce,
							  &RenderScene::setParticleEmitterAttractorForce,
							  -FLT_MAX,
							  FLT_MAX,
							  0.01f,
							  allocator));
	auto attractor_module_planes = LUMIX_NEW(allocator, ArrayDescriptor<RenderScene>)("Attractors",
		&RenderScene::getParticleEmitterAttractorCount,
		&RenderScene::addParticleEmitterAttractor,
		&RenderScene::removeParticleEmitterAttractor,
		allocator);
	attractor_module_planes->addChild(
		LUMIX_NEW(allocator, EntityPropertyDescriptor<RenderScene>)("Entity",
			&RenderScene::getParticleEmitterAttractorEntity,
			&RenderScene::setParticleEmitterAttractorEntity,
			allocator));
	PropertyRegister::add("particle_emitter_attractor", attractor_module_planes);

	PropertyRegister::add("particle_emitter_fade",
		LUMIX_NEW(allocator, SampledFunctionDescriptor<RenderScene>)("Alpha",
							  &RenderScene::getParticleEmitterAlpha,
							  &RenderScene::setParticleEmitterAlpha,
							  &RenderScene::getParticleEmitterAlphaCount,
							  1,
							  1,
							  allocator));

	PropertyRegister::add("particle_emitter_force",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec3, RenderScene>)("Acceleration",
							  &RenderScene::getParticleEmitterAcceleration,
							  &RenderScene::setParticleEmitterAcceleration,
							  allocator));

	PropertyRegister::add("particle_emitter_size",
		LUMIX_NEW(allocator, SampledFunctionDescriptor<RenderScene>)("Size",
							  &RenderScene::getParticleEmitterSize,
							  &RenderScene::setParticleEmitterSize,
							  &RenderScene::getParticleEmitterSizeCount,
							  1,
							  1,
							  allocator));

	PropertyRegister::add("particle_emitter_linear_movement",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("x",
							  &RenderScene::getParticleEmitterLinearMovementX,
							  &RenderScene::setParticleEmitterLinearMovementX,
							  allocator));
	PropertyRegister::add("particle_emitter_linear_movement",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("y",
							  &RenderScene::getParticleEmitterLinearMovementY,
							  &RenderScene::setParticleEmitterLinearMovementY,
							  allocator));
	PropertyRegister::add("particle_emitter_linear_movement",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("z",
							  &RenderScene::getParticleEmitterLinearMovementZ,
							  &RenderScene::setParticleEmitterLinearMovementZ,
							  allocator));

	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("Life",
							  &RenderScene::getParticleEmitterInitialLife,
							  &RenderScene::setParticleEmitterInitialLife,
							  allocator));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("Initial size",
							  &RenderScene::getParticleEmitterInitialSize,
							  &RenderScene::setParticleEmitterInitialSize,
							  allocator));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("Spawn period",
							  &RenderScene::getParticleEmitterSpawnPeriod,
							  &RenderScene::setParticleEmitterSpawnPeriod,
							  allocator));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Int2, RenderScene>)("Spawn count",
							  &RenderScene::getParticleEmitterSpawnCount,
							  &RenderScene::setParticleEmitterSpawnCount,
							  allocator));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Material",
							  &RenderScene::getParticleEmitterMaterialPath,
							  &RenderScene::setParticleEmitterMaterialPath,
							  "Material (*.mat)",
							  ResourceManager::MATERIAL,
							  allocator));

	PropertyRegister::add(
		"camera",
		LUMIX_NEW(allocator, StringPropertyDescriptor<RenderScene>)(
			"Slot", &RenderScene::getCameraSlot, &RenderScene::setCameraSlot, allocator));
	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("FOV",
							  &RenderScene::getCameraFOV,
							  &RenderScene::setCameraFOV,
							  1.0f,
							  179.0f,
							  1.0f,
							  allocator));
	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Near",
							  &RenderScene::getCameraNearPlane,
							  &RenderScene::setCameraNearPlane,
							  0.0f,
							  FLT_MAX,
							  0.0f,
							  allocator));
	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Far",
							  &RenderScene::getCameraFarPlane,
							  &RenderScene::setCameraFarPlane,
							  0.0f,
							  FLT_MAX,
							  0.0f,
							  allocator));

	PropertyRegister::add("renderable",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Source",
							  &RenderScene::getRenderablePath,
							  &RenderScene::setRenderablePath,
							  "Mesh (*.msh)",
							  ResourceManager::MODEL,
							  allocator));

	auto renderable_material = LUMIX_NEW(allocator, ArrayDescriptor<RenderScene>)("Materials",
		&RenderScene::getRenderableMaterialsCount,
		nullptr,
		nullptr,
		allocator);
	renderable_material->addChild(LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Material",
		&RenderScene::getRenderableMaterial,
		&RenderScene::setRenderableMaterial,
		"Material (*.mat)",
		ResourceManager::MATERIAL,
		allocator));
	PropertyRegister::add("renderable", renderable_material);

	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Ambient color",
							  &RenderScene::getLightAmbientColor,
							  &RenderScene::setLightAmbientColor,
							  allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Diffuse color",
							  &RenderScene::getGlobalLightColor,
							  &RenderScene::setGlobalLightColor,
							  allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Specular color",
							  &RenderScene::getGlobalLightSpecular,
							  &RenderScene::setGlobalLightSpecular,
							  allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Ambient intensity",
							  &RenderScene::getLightAmbientIntensity,
							  &RenderScene::setLightAmbientIntensity,
							  0.0f,
							  FLT_MAX,
							  0.05f,
							  allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Diffuse intensity",
							  &RenderScene::getGlobalLightIntensity,
							  &RenderScene::setGlobalLightIntensity,
							  0.0f,
							  FLT_MAX,
							  0.05f,
							  allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Specular intensity",
							  &RenderScene::getGlobalLightSpecularIntensity,
							  &RenderScene::setGlobalLightSpecularIntensity,
							  0,
							  FLT_MAX,
							  0.01f,
							  allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec4, RenderScene>)("Shadow cascades",
							  &RenderScene::getShadowmapCascades,
							  &RenderScene::setShadowmapCascades,
							  allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Fog density",
							  &RenderScene::getFogDensity,
							  &RenderScene::setFogDensity,
							  0.0f,
							  1.0f,
							  0.01f,
							  allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Fog bottom",
							  &RenderScene::getFogBottom,
							  &RenderScene::setFogBottom,
							  -FLT_MAX,
							  FLT_MAX,
							  1.0f,
							  allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Fog height",
							  &RenderScene::getFogHeight,
							  &RenderScene::setFogHeight,
							  0.01f,
							  FLT_MAX,
							  1.0f,
							  allocator));
	PropertyRegister::add(
		"global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)(
			"Fog color", &RenderScene::getFogColor, &RenderScene::setFogColor, allocator));

	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Diffuse color",
							  &RenderScene::getPointLightColor,
							  &RenderScene::setPointLightColor,
							  allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Specular color",
							  &RenderScene::getPointLightSpecularColor,
							  &RenderScene::setPointLightSpecularColor,
							  allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Diffuse intensity",
							  &RenderScene::getPointLightIntensity,
							  &RenderScene::setPointLightIntensity,
							  0.0f,
							  FLT_MAX,
							  0.05f,
							  allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Specular intensity",
							  &RenderScene::getPointLightSpecularIntensity,
							  &RenderScene::setPointLightSpecularIntensity,
							  0.0f,
							  FLT_MAX,
							  0.05f,
							  allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("FOV",
							  &RenderScene::getLightFOV,
							  &RenderScene::setLightFOV,
							  0.0f,
							  360.0f,
							  5.0f,
							  allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Attenuation",
							  &RenderScene::getLightAttenuation,
							  &RenderScene::setLightAttenuation,
							  0.0f,
							  1000.0f,
							  0.1f,
							  allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Range",
							  &RenderScene::getLightRange,
							  &RenderScene::setLightRange,
							  0.0f,
							  FLT_MAX,
							  1.0f,
							  allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<RenderScene>)("Cast shadows",
							  &RenderScene::getLightCastShadows,
							  &RenderScene::setLightCastShadows,
							  allocator));
	PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Material",
							  &RenderScene::getTerrainMaterialPath,
							  &RenderScene::setTerrainMaterialPath,
							  "Material (*.mat)",
							  ResourceManager::MATERIAL,
							  allocator));
	PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("XZ scale",
							  &RenderScene::getTerrainXZScale,
							  &RenderScene::setTerrainXZScale,
							  0.0f,
							  FLT_MAX,
							  0.0f,
							  allocator));
	PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Height scale",
							  &RenderScene::getTerrainYScale,
							  &RenderScene::setTerrainYScale,
							  0.0f,
							  FLT_MAX,
							  0.0f,
							  allocator));

	PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, IntPropertyDescriptor<RenderScene>)("Grass distance",
							  &RenderScene::getGrassDistance,
							  &RenderScene::setGrassDistance,
							  allocator));

	auto grass = LUMIX_NEW(allocator, ArrayDescriptor<RenderScene>)("Grass",
		&RenderScene::getGrassCount,
		&RenderScene::addGrass,
		&RenderScene::removeGrass,
		allocator);
	grass->addChild(LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Mesh",
		&RenderScene::getGrassPath,
		&RenderScene::setGrassPath,
		"Mesh (*.msh)",
		ResourceManager::MODEL,
		allocator));
	auto ground = LUMIX_NEW(allocator, IntPropertyDescriptor<RenderScene>)(
		"Ground", &RenderScene::getGrassGround, &RenderScene::setGrassGround, allocator);
	ground->setLimit(0, 4);
	grass->addChild(ground);
	grass->addChild(LUMIX_NEW(allocator, IntPropertyDescriptor<RenderScene>)(
		"Density", &RenderScene::getGrassDensity, &RenderScene::setGrassDensity, allocator));
	PropertyRegister::add("terrain", grass);
}


static const uint32 GLOBAL_LIGHT_HASH = crc32("global_light");
static const uint32 POINT_LIGHT_HASH = crc32("point_light");
static const uint32 RENDERABLE_HASH = crc32("renderable");
static const uint32 CAMERA_HASH = crc32("camera");


struct BGFXAllocator : public bx::AllocatorI
{

	BGFXAllocator(Lumix::IAllocator& source)
		: m_source(source)
	{
	}


	static const size_t NATURAL_ALIGNEMENT = 8;


	void* realloc(void* _ptr, size_t _size, size_t _alignment, const char*, uint32) override
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


	Lumix::IAllocator& m_source;
};


struct RendererImpl : public Renderer
{
	struct CallbackStub : public bgfx::CallbackI
	{
		CallbackStub(RendererImpl& renderer)
			: m_renderer(renderer)
		{}


		void fatal(bgfx::Fatal::Enum _code, const char* _str) override
		{
			Lumix::g_log_error.log("Renderer") << _str;
			if (bgfx::Fatal::DebugCheck == _code)
			{
				Lumix::Debug::debugBreak();
			}
			else
			{
				abort();
			}
		}


		void traceVargs(const char* _filePath,
			uint16 _line,
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
			#pragma pack(1)
				struct TGAHeader
				{
					char idLength;
					char colourMapType;
					char dataType;
					short int colourMapOrigin;
					short int colourMapLength;
					char colourMapDepth;
					short int xOrigin;
					short int yOrigin;
					short int width;
					short int height;
					char bitsPerPixel;
					char imageDescriptor;
				};
			#pragma pack()

			TGAHeader header;
			setMemory(&header, 0, sizeof(header));
			int bytes_per_pixel = 4;
			header.bitsPerPixel = (char)(bytes_per_pixel * 8);
			header.height = (short)height;
			header.width = (short)width;
			header.dataType = 2;

			Lumix::FS::OsFile file;
			if(!file.open(filePath, Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE, m_renderer.m_allocator))
			{
				g_log_error.log("Renderer") << "Failed to save screenshot to " << filePath;
				return;
			}
			file.write(&header, sizeof(header));

			file.write(data, size);
			file.close();
		}


		void captureBegin(uint32,
			uint32,
			uint32,
			bgfx::TextureFormat::Enum,
			bool) override
		{
			ASSERT(false);
		}


		uint32 cacheReadSize(uint64) override { return 0; }
		bool cacheRead(uint64, void*, uint32) override { return false; }
		void cacheWrite(uint64, const void*, uint32) override {}
		void captureEnd() override { ASSERT(false); }
		void captureFrame(const void*, uint32) override { ASSERT(false); }

		RendererImpl& m_renderer;
	};


	RendererImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_texture_manager(m_allocator)
		, m_model_manager(m_allocator, *this)
		, m_material_manager(*this, m_allocator)
		, m_shader_manager(*this, m_allocator)
		, m_shader_binary_manager(*this, m_allocator)
		, m_passes(m_allocator)
		, m_shader_defines(m_allocator)
		, m_bgfx_allocator(m_allocator)
		, m_frame_allocator(m_allocator, 10 * 1024 * 1024)
		, m_callback_stub(*this)
	{
		registerProperties(engine.getAllocator());
		bgfx::PlatformData d;
		void* platform_data = engine.getPlatformData().window_handle;
		if (platform_data)
		{
			setMemory(&d, 0, sizeof(d));
			d.nwh = platform_data;
			bgfx::setPlatformData(d);
		}
		bgfx::init(bgfx::RendererType::Count, 0, 0, &m_callback_stub, &m_bgfx_allocator);
		bgfx::reset(800, 600);
		bgfx::setDebug(BGFX_DEBUG_TEXT);

		ResourceManager& manager = engine.getResourceManager();
		m_texture_manager.create(ResourceManager::TEXTURE, manager);
		m_model_manager.create(ResourceManager::MODEL, manager);
		m_material_manager.create(ResourceManager::MATERIAL, manager);
		m_shader_manager.create(ResourceManager::SHADER, manager);
		m_shader_binary_manager.create(ResourceManager::SHADER_BINARY, manager);

		m_current_pass_hash = crc32("MAIN");
		m_view_counter = 0;
		m_mat_color_shininess_uniform =
			bgfx::createUniform("u_materialColorShininess", bgfx::UniformType::Vec4);

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

		m_default_shader = static_cast<Shader*>(m_shader_manager.load(Path("shaders/default.shd")));
	}

	~RendererImpl()
	{
		m_shader_manager.unload(*m_default_shader);
		m_texture_manager.destroy();
		m_model_manager.destroy();
		m_material_manager.destroy();
		m_shader_manager.destroy();
		m_shader_binary_manager.destroy();

		bgfx::destroyUniform(m_mat_color_shininess_uniform);
		bgfx::frame();
		bgfx::frame();
		bgfx::shutdown();
	}


	MaterialManager& getMaterialManager()
	{
		return m_material_manager;
	}


	const bgfx::VertexDecl& getBasicVertexDecl() const override
	{
		return m_basic_vertex_decl;
	}


	const bgfx::VertexDecl& getBasic2DVertexDecl() const override
	{
		return m_basic_2d_vertex_decl;
	}


	IScene* createScene(Universe& ctx) override
	{
		return RenderScene::createInstance(*this, m_engine, ctx, true, m_allocator);
	}


	void destroyScene(IScene* scene) override
	{
		RenderScene::destroyInstance(static_cast<RenderScene*>(scene));
	}


	bool create() override { return true; }


	void destroy() override {}


	const char* getName() const override { return "renderer"; }


	Engine& getEngine() override { return m_engine; }


	const char* getShaderDefine(int define_idx) override
	{
		return m_shader_defines[define_idx];
	}


	uint8 getShaderDefineIdx(const char* define) override
	{
		for (int i = 0; i < m_shader_defines.size(); ++i)
		{
			if (compareString(m_shader_defines[i], define) == 0)
			{
				ASSERT(i < 256);
				return i;
			}
		}

		auto& new_define = m_shader_defines.emplace();
		copyString(new_define, define);
		return m_shader_defines.size() - 1;
	}


	int getPassIdx(const char* pass) override
	{
		for (int i = 0; i < m_passes.size(); ++i)
		{
			if (compareString(m_passes[i], pass) == 0)
			{
				return i;
			}
		}

		auto& new_pass = m_passes.emplace();
		copyString(new_pass, pass);
		return m_passes.size() - 1;
	}


	const bgfx::UniformHandle& getMaterialColorShininessUniform() const override
	{
		return m_mat_color_shininess_uniform;
	}


	void makeScreenshot(const Path& filename) override
	{
		bgfx::saveScreenShot(filename.c_str());
	}


	void resize(int w, int h) override
	{
		bgfx::reset(w, h);
	}


	void frame() override
	{
		PROFILE_FUNCTION();
		bgfx::frame();
		m_view_counter = 0;
	}


	int getViewCounter() const override
	{
		return m_view_counter;
	}


	void viewCounterAdd() override
	{
		++m_view_counter;
	}


	LIFOAllocator& getFrameAllocator() override
	{
		return m_frame_allocator;
	}


	Shader* getDefaultShader() override
	{
		return m_default_shader;
	}


	typedef char ShaderDefine[32];


	Engine& m_engine;
	IAllocator& m_allocator;
	Array<ShaderCombinations::Pass> m_passes;
	Array<ShaderDefine> m_shader_defines;
	CallbackStub m_callback_stub;
	LIFOAllocator m_frame_allocator;
	TextureManager m_texture_manager;
	MaterialManager m_material_manager;
	ShaderManager m_shader_manager;
	ShaderBinaryManager m_shader_binary_manager;
	ModelManager m_model_manager;
	uint32 m_current_pass_hash;
	int m_view_counter;
	Shader* m_default_shader;
	BGFXAllocator m_bgfx_allocator;
	bgfx::VertexDecl m_basic_vertex_decl;
	bgfx::VertexDecl m_basic_2d_vertex_decl;
	bgfx::UniformHandle m_mat_color_shininess_uniform;
};


extern "C"
{
	LUMIX_PLUGIN_ENTRY(renderer)
	{
		RendererImpl* r = LUMIX_NEW(engine.getAllocator(), RendererImpl)(engine);
		if (r->create())
		{
			return r;
		}
		LUMIX_DELETE(engine.getAllocator(), r);
		return nullptr;
	}
}


} // namespace Lumix



