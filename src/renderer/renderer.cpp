#include "renderer.h"

#include "engine/array.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/fs/os_file.h"
#include "engine/lifo_allocator.h"
#include "engine/log.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/debug/debug.h"
#include "engine/engine.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include "engine/system.h"
#include "renderer/material.h"
#include "renderer/material_manager.h"
#include "renderer/model.h"
#include "renderer/model_manager.h"
#include "renderer/render_scene.h"
#include "renderer/shader.h"
#include "renderer/shader_manager.h"
#include "renderer/texture.h"
#include "renderer/texture_manager.h"
#include "engine/universe/universe.h"
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


static const ComponentType GLOBAL_LIGHT_TYPE = PropertyRegister::getComponentType("global_light");
static const ComponentType POINT_LIGHT_TYPE = PropertyRegister::getComponentType("point_light");
static const ComponentType MODEL_INSTANCE_TYPE = PropertyRegister::getComponentType("renderable");
static const ComponentType CAMERA_TYPE = PropertyRegister::getComponentType("camera");
static const ResourceType MATERIAL_TYPE("material");
static const ResourceType MODEL_TYPE("model");
static const ResourceType SHADER_TYPE("shader");
static const ResourceType TEXTURE_TYPE("texture");
static const ResourceType SHADER_BINARY_TYPE("shader_binary");


struct BonePropertyDescriptor : public IEnumPropertyDescriptor
{
	BonePropertyDescriptor(const char* name)
	{
		setName(name);
		m_type = ENUM;
	}

	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		ASSERT(index == -1);
		int value;
		stream.read(&value, sizeof(value));
		auto* render_scene = static_cast<RenderScene*>(cmp.scene);
		render_scene->setBoneAttachmentBone(cmp.handle, value);
	}


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		ASSERT(index == -1);
		auto* render_scene = static_cast<RenderScene*>(cmp.scene);
		int value = render_scene->getBoneAttachmentBone(cmp.handle);
		int len = sizeof(value);
		stream.write(&value, len);
	}


	ComponentHandle getModelInstance(RenderScene* render_scene, ComponentHandle bone_attachment_cmp)
	{
		Entity parent_entity = render_scene->getBoneAttachmentParent(bone_attachment_cmp);
		if (parent_entity == INVALID_ENTITY) return INVALID_COMPONENT;
		ComponentHandle model_instance = render_scene->getModelInstanceComponent(parent_entity);
		return model_instance;
	}


	int getEnumCount(IScene* scene, ComponentHandle cmp) override
	{
		auto* render_scene = static_cast<RenderScene*>(scene);
		ComponentHandle model_instance = getModelInstance(render_scene, cmp);
		if (model_instance == INVALID_COMPONENT) return 0;
		auto* model = render_scene->getModelInstanceModel(model_instance);
		if (!model || !model->isReady()) return 0;
		return model->getBoneCount();
	}


	const char* getEnumItemName(IScene* scene, ComponentHandle cmp, int index) override
	{
		auto* render_scene = static_cast<RenderScene*>(scene);
		ComponentHandle model_instance = getModelInstance(render_scene, cmp);
		if (model_instance == INVALID_COMPONENT) return "";
		auto* model = render_scene->getModelInstanceModel(model_instance);
		if (!model) return "";
		return model->getBone(index).name.c_str();
	}
};


static void registerProperties(IAllocator& allocator)
{
	PropertyRegister::add("bone_attachment",
		LUMIX_NEW(allocator, EntityPropertyDescriptor<RenderScene>)(
			"Parent", &RenderScene::getBoneAttachmentParent, &RenderScene::setBoneAttachmentParent));
	PropertyRegister::add("bone_attachment", LUMIX_NEW(allocator, BonePropertyDescriptor)("Bone"));
	PropertyRegister::add("bone_attachment",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec3, RenderScene>)(
			"Relative position", &RenderScene::getBoneAttachmentPosition, &RenderScene::setBoneAttachmentPosition));
	auto bone_attachment_relative_rot = LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec3, RenderScene>)(
		"Relative rotation", &RenderScene::getBoneAttachmentRotation, &RenderScene::setBoneAttachmentRotation);
	bone_attachment_relative_rot->setIsInRadians(true);
	PropertyRegister::add("bone_attachment", bone_attachment_relative_rot);
	PropertyRegister::add("particle_emitter_spawn_shape",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Radius",
			&RenderScene::getParticleEmitterShapeRadius,
			&RenderScene::setParticleEmitterShapeRadius,
			0.0f,
			FLT_MAX,
			0.01f));

	PropertyRegister::add("particle_emitter_plane",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Bounce",
			&RenderScene::getParticleEmitterPlaneBounce,
			&RenderScene::setParticleEmitterPlaneBounce,
			0.0f,
			1.0f,
			0.01f));
	auto plane_module_planes = LUMIX_NEW(allocator, ArrayDescriptor<RenderScene>)("Planes",
		&RenderScene::getParticleEmitterPlaneCount,
		&RenderScene::addParticleEmitterPlane,
		&RenderScene::removeParticleEmitterPlane,
		allocator);
	plane_module_planes->addChild(LUMIX_NEW(allocator, EntityPropertyDescriptor<RenderScene>)(
		"Entity", &RenderScene::getParticleEmitterPlaneEntity, &RenderScene::setParticleEmitterPlaneEntity));
	PropertyRegister::add("particle_emitter_plane", plane_module_planes);

	PropertyRegister::add("particle_emitter_attractor",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Force",
			&RenderScene::getParticleEmitterAttractorForce,
			&RenderScene::setParticleEmitterAttractorForce,
			-FLT_MAX,
			FLT_MAX,
			0.01f));
	auto attractor_module_planes = LUMIX_NEW(allocator, ArrayDescriptor<RenderScene>)("Attractors",
		&RenderScene::getParticleEmitterAttractorCount,
		&RenderScene::addParticleEmitterAttractor,
		&RenderScene::removeParticleEmitterAttractor,
		allocator);
	attractor_module_planes->addChild(LUMIX_NEW(allocator, EntityPropertyDescriptor<RenderScene>)(
		"Entity", &RenderScene::getParticleEmitterAttractorEntity, &RenderScene::setParticleEmitterAttractorEntity));
	PropertyRegister::add("particle_emitter_attractor", attractor_module_planes);

	PropertyRegister::add("particle_emitter_alpha",
		LUMIX_NEW(allocator, SampledFunctionDescriptor<RenderScene>)("Alpha",
			&RenderScene::getParticleEmitterAlpha,
			&RenderScene::setParticleEmitterAlpha,
			&RenderScene::getParticleEmitterAlphaCount,
			1,
			1));

	PropertyRegister::add("particle_emitter_force",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec3, RenderScene>)("Acceleration",
			&RenderScene::getParticleEmitterAcceleration,
			&RenderScene::setParticleEmitterAcceleration));

	PropertyRegister::add("particle_emitter_subimage",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<int, RenderScene>)("Rows",
			&RenderScene::getParticleEmitterSubimageRows,
			&RenderScene::setParticleEmitterSubimageRows));
	PropertyRegister::add("particle_emitter_subimage",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<int, RenderScene>)("Columns",
			&RenderScene::getParticleEmitterSubimageCols,
			&RenderScene::setParticleEmitterSubimageCols));

	PropertyRegister::add("particle_emitter_size",
		LUMIX_NEW(allocator, SampledFunctionDescriptor<RenderScene>)("Size",
			&RenderScene::getParticleEmitterSize,
			&RenderScene::setParticleEmitterSize,
			&RenderScene::getParticleEmitterSizeCount,
			1,
			1));

	PropertyRegister::add("particle_emitter_linear_movement",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)(
			"x", &RenderScene::getParticleEmitterLinearMovementX, &RenderScene::setParticleEmitterLinearMovementX));
	PropertyRegister::add("particle_emitter_linear_movement",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)(
			"y", &RenderScene::getParticleEmitterLinearMovementY, &RenderScene::setParticleEmitterLinearMovementY));
	PropertyRegister::add("particle_emitter_linear_movement",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)(
			"z", &RenderScene::getParticleEmitterLinearMovementZ, &RenderScene::setParticleEmitterLinearMovementZ));

	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)(
			"Life", &RenderScene::getParticleEmitterInitialLife, &RenderScene::setParticleEmitterInitialLife));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)(
			"Initial size", &RenderScene::getParticleEmitterInitialSize, &RenderScene::setParticleEmitterInitialSize));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)(
			"Spawn period", &RenderScene::getParticleEmitterSpawnPeriod, &RenderScene::setParticleEmitterSpawnPeriod));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Int2, RenderScene>)(
			"Spawn count", &RenderScene::getParticleEmitterSpawnCount, &RenderScene::setParticleEmitterSpawnCount));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<RenderScene>)(
			"Autoemit", &RenderScene::getParticleEmitterAutoemit, &RenderScene::setParticleEmitterAutoemit));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<RenderScene>)(
			"Local space", &RenderScene::getParticleEmitterLocalSpace, &RenderScene::setParticleEmitterLocalSpace));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Material",
			&RenderScene::getParticleEmitterMaterialPath,
			&RenderScene::setParticleEmitterMaterialPath,
			"Material (*.mat)",
			MATERIAL_TYPE));

	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, StringPropertyDescriptor<RenderScene>)(
			"Slot", &RenderScene::getCameraSlot, &RenderScene::setCameraSlot));
	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Orthographic size",
			&RenderScene::getCameraOrthoSize,
			&RenderScene::setCameraOrthoSize,
			0.0f,
			FLT_MAX,
			1.0f));
	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<RenderScene>)(
			"Orthographic", &RenderScene::isCameraOrtho, &RenderScene::setCameraOrtho));
	PropertyRegister::add("camera",
		&(LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)(
			  "FOV", &RenderScene::getCameraFOV, &RenderScene::setCameraFOV, 1, 179, 1))
			 ->setIsInRadians(true));
	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)(
			"Near", &RenderScene::getCameraNearPlane, &RenderScene::setCameraNearPlane, 0.0f, FLT_MAX, 0.0f));
	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)(
			"Far", &RenderScene::getCameraFarPlane, &RenderScene::setCameraFarPlane, 0.0f, FLT_MAX, 0.0f));

	PropertyRegister::add("renderable",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)(
			"Source", &RenderScene::getModelInstancePath, &RenderScene::setModelInstancePath, "Mesh (*.msh)", MODEL_TYPE));

	auto model_instance_material = LUMIX_NEW(allocator, ArrayDescriptor<RenderScene>)(
		"Materials", &RenderScene::getModelInstanceMaterialsCount, nullptr, nullptr, allocator);
	model_instance_material->addChild(LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Material",
		&RenderScene::getModelInstanceMaterial,
		&RenderScene::setModelInstanceMaterial,
		"Material (*.mat)",
		MATERIAL_TYPE));
	PropertyRegister::add("renderable", model_instance_material);

	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)(
			"Ambient color", &RenderScene::getLightAmbientColor, &RenderScene::setLightAmbientColor));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)(
			"Diffuse color", &RenderScene::getGlobalLightColor, &RenderScene::setGlobalLightColor));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)(
			"Specular color", &RenderScene::getGlobalLightSpecular, &RenderScene::setGlobalLightSpecular));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Ambient intensity",
			&RenderScene::getLightAmbientIntensity,
			&RenderScene::setLightAmbientIntensity,
			0.0f,
			FLT_MAX,
			0.05f));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Diffuse intensity",
			&RenderScene::getGlobalLightIntensity,
			&RenderScene::setGlobalLightIntensity,
			0.0f,
			FLT_MAX,
			0.05f));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Specular intensity",
			&RenderScene::getGlobalLightSpecularIntensity,
			&RenderScene::setGlobalLightSpecularIntensity,
			0,
			FLT_MAX,
			0.01f));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec4, RenderScene>)(
			"Shadow cascades", &RenderScene::getShadowmapCascades, &RenderScene::setShadowmapCascades));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)(
			"Fog density", &RenderScene::getFogDensity, &RenderScene::setFogDensity, 0.0f, 1.0f, 0.01f));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)(
			"Fog bottom", &RenderScene::getFogBottom, &RenderScene::setFogBottom, -FLT_MAX, FLT_MAX, 1.0f));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)(
			"Fog height", &RenderScene::getFogHeight, &RenderScene::setFogHeight, 0.01f, FLT_MAX, 1.0f));
	PropertyRegister::add(
		"global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)(
			"Fog color", &RenderScene::getFogColor, &RenderScene::setFogColor));

	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)(
			"Diffuse color", &RenderScene::getPointLightColor, &RenderScene::setPointLightColor));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)(
			"Specular color", &RenderScene::getPointLightSpecularColor, &RenderScene::setPointLightSpecularColor));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Diffuse intensity",
			&RenderScene::getPointLightIntensity,
			&RenderScene::setPointLightIntensity,
			0.0f,
			FLT_MAX,
			0.05f));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Specular intensity",
			&RenderScene::getPointLightSpecularIntensity,
			&RenderScene::setPointLightSpecularIntensity,
			0.0f,
			FLT_MAX,
			0.05f));
	PropertyRegister::add("point_light",
		&(LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)(
			  "FOV", &RenderScene::getLightFOV, &RenderScene::setLightFOV, 0, 360, 5))
			 ->setIsInRadians(true));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)(
			"Attenuation", &RenderScene::getLightAttenuation, &RenderScene::setLightAttenuation, 0.0f, 1000.0f, 0.1f));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)(
			"Range", &RenderScene::getLightRange, &RenderScene::setLightRange, 0.0f, FLT_MAX, 1.0f));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<RenderScene>)(
			"Cast shadows", &RenderScene::getLightCastShadows, &RenderScene::setLightCastShadows));

	PropertyRegister::add("decal",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Material",
			&RenderScene::getDecalMaterialPath,
			&RenderScene::setDecalMaterialPath,
			"Material (*.mat)",
			MATERIAL_TYPE));
	PropertyRegister::add("decal",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec3, RenderScene>)(
			"Scale", &RenderScene::getDecalScale, &RenderScene::setDecalScale));

	PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Material",
			&RenderScene::getTerrainMaterialPath,
			&RenderScene::setTerrainMaterialPath,
			"Material (*.mat)",
			MATERIAL_TYPE));
	PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)(
			"XZ scale", &RenderScene::getTerrainXZScale, &RenderScene::setTerrainXZScale, 0.0f, FLT_MAX, 0.0f));
	PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)(
			"Height scale", &RenderScene::getTerrainYScale, &RenderScene::setTerrainYScale, 0.0f, FLT_MAX, 0.0f));

	auto grass = LUMIX_NEW(allocator, ArrayDescriptor<RenderScene>)(
		"Grass", &RenderScene::getGrassCount, &RenderScene::addGrass, &RenderScene::removeGrass, allocator);
	grass->addChild(LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)(
		"Mesh", &RenderScene::getGrassPath, &RenderScene::setGrassPath, "Mesh (*.msh)", MODEL_TYPE));
	grass->addChild(LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)(
		"Distance", &RenderScene::getGrassDistance, &RenderScene::setGrassDistance, 1.0f, FLT_MAX, 1.0f));
	grass->addChild(LUMIX_NEW(allocator, IntPropertyDescriptor<RenderScene>)(
		"Density", &RenderScene::getGrassDensity, &RenderScene::setGrassDensity));
	PropertyRegister::add("terrain", grass);
}


struct BGFXAllocator LUMIX_FINAL : public bx::AllocatorI
{

	explicit BGFXAllocator(Lumix::IAllocator& source)
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


struct RendererImpl LUMIX_FINAL : public Renderer
{
	struct CallbackStub LUMIX_FINAL : public bgfx::CallbackI
	{
		explicit CallbackStub(RendererImpl& renderer)
			: m_renderer(renderer)
		{}


		void fatal(bgfx::Fatal::Enum _code, const char* _str) override
		{
			Lumix::g_log_error.log("Renderer") << _str;
			if (bgfx::Fatal::DebugCheck == _code || bgfx::Fatal::InvalidShader == _code)
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
			if(!file.open(filePath, Lumix::FS::Mode::CREATE_AND_WRITE, m_renderer.m_allocator))
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
		Lumix::getCommandLine(cmd_line, Lumix::lengthOf(cmd_line));
		Lumix::CommandLineParser cmd_line_parser(cmd_line);
		while (cmd_line_parser.next())
		{
			if (cmd_line_parser.currentEquals("-opengl"))
			{
				renderer_type = bgfx::RendererType::OpenGL;
				break;
			}
		}

		bool res = bgfx::init(renderer_type, 0, 0, &m_callback_stub, &m_bgfx_allocator);
		ASSERT(res);
		bgfx::reset(800, 600);
		bgfx::setDebug(BGFX_DEBUG_TEXT);

		ResourceManager& manager = engine.getResourceManager();
		m_texture_manager.create(TEXTURE_TYPE, manager);
		m_model_manager.create(MODEL_TYPE, manager);
		m_material_manager.create(MATERIAL_TYPE, manager);
		m_shader_manager.create(SHADER_TYPE, manager);
		m_shader_binary_manager.create(SHADER_BINARY_TYPE, manager);

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
		m_shader_binary_manager.destroy();

		bgfx::destroyUniform(m_mat_color_shininess_uniform);
		bgfx::frame();
		bgfx::frame();
		bgfx::shutdown();
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


	int getLayersCount() const override
	{
		return m_layers.size();
	}


	const char* getLayerName(int idx) const override
	{
		return m_layers[idx];
	}


	bool isOpenGL() const override
	{
		return bgfx::getRendererType() == bgfx::RendererType::OpenGL ||
			   bgfx::getRendererType() == bgfx::RendererType::OpenGLES;
	}


	ModelManager& getModelManager() override
	{
		return m_model_manager;
	}


	MaterialManager& getMaterialManager() override
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


	const char* getName() const override { return "renderer"; }


	Engine& getEngine() override { return m_engine; }


	int getShaderDefinesCount() const override
	{
		return m_shader_defines.size();
	}


	const char* getShaderDefine(int define_idx) override
	{
		return m_shader_defines[define_idx];
	}


	uint8 getShaderDefineIdx(const char* define) override
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


	const char* getPassName(int idx) override
	{
		return m_passes[idx];
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


	Shader* getDefaultShader() override
	{
		return m_default_shader;
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
		return LUMIX_NEW(engine.getAllocator(), RendererImpl)(engine);
	}
}


} // namespace Lumix



