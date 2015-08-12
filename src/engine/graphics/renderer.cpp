#include "renderer.h"

#include "core/array.h"
#include "core/crc32.h"
#include "debug/debug.h"
#include "core/fs/file_system.h"
#include "core/json_serializer.h"
#include "core/math_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/vec4.h"
#include "debug/allocator.h"
#include "editor/property_descriptor.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "graphics/geometry.h"
#include "graphics/material.h"
#include "graphics/material_manager.h"
#include "graphics/model.h"
#include "graphics/model_instance.h"
#include "graphics/model_manager.h"
#include "graphics/pipeline.h"
#include "graphics/render_scene.h"
#include "graphics/shader.h"
#include "graphics/shader_manager.h"
#include "graphics/texture.h"
#include "graphics/texture_manager.h"
#include "universe/universe.h"
#include <bgfx.h>
#include <Windows.h>
#include <cstdio>


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
}


namespace Lumix
{


static const uint32_t GLOBAL_LIGHT_HASH = crc32("global_light");
static const uint32_t POINT_LIGHT_HASH = crc32("point_light");
static const uint32_t RENDERABLE_HASH = crc32("renderable");
static const uint32_t CAMERA_HASH = crc32("camera");


struct RendererImpl : public Renderer
{
	struct CallbackStub : public bgfx::CallbackI
	{
		virtual void fatal(bgfx::Fatal::Enum _code, const char* _str) override
		{
			Lumix::g_log_error.log("bgfx") << _str;
			if (bgfx::Fatal::DebugCheck == _code)
			{
				Lumix::Debug::debugBreak();
			}
			else
			{
				abort();
			}
		}


		virtual void traceVargs(const char* _filePath, uint16_t _line, const char* _format, va_list _argList) override
		{
			char tmp[2048];
			vsnprintf(tmp, sizeof(tmp), _format, _argList);
			Lumix::g_log_info.log("bgfx") << _filePath << "(" << _line << ") " << tmp;
		}


		virtual void screenShot(const char*,
								uint32_t,
								uint32_t,
								uint32_t,
								const void*,
								uint32_t,
								bool) override
		{
			ASSERT(false);
		}


		virtual void captureBegin(uint32_t,
								  uint32_t,
								  uint32_t,
								  bgfx::TextureFormat::Enum,
								  bool) override
		{
			ASSERT(false);
		}


		virtual uint32_t cacheReadSize(uint64_t) override { return 0; }
		virtual bool cacheRead(uint64_t, void*, uint32_t) override
		{
			return false;
		}
		virtual void cacheWrite(uint64_t, const void*, uint32_t) override {}
		virtual void captureEnd() override { ASSERT(false); }
		virtual void captureFrame(const void*, uint32_t) override
		{
			ASSERT(false);
		}
	};


	RendererImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_texture_manager(m_allocator)
		, m_model_manager(m_allocator, *this)
		, m_material_manager(m_allocator)
		, m_shader_manager(*this, m_allocator)
		, m_pipeline_manager(*this, m_allocator)
		, m_passes(m_allocator)
	{
		bgfx::PlatformData d;
		if (s_hwnd)
		{
			memset(&d, 0, sizeof(d));
			d.nwh = s_hwnd;
			bgfx::setPlatformData(d);
		}
		bgfx::init(bgfx::RendererType::Count, 0, 0, &m_callback_stub);
		bgfx::reset(800, 600);
		bgfx::setDebug(BGFX_DEBUG_TEXT);

		ResourceManager& manager = engine.getResourceManager();
		m_texture_manager.create(ResourceManager::TEXTURE, manager);
		m_model_manager.create(ResourceManager::MODEL, manager);
		m_material_manager.create(ResourceManager::MATERIAL, manager);
		m_shader_manager.create(ResourceManager::SHADER, manager);
		m_pipeline_manager.create(ResourceManager::PIPELINE, manager);

		m_current_pass_hash = crc32("MAIN");
		m_view_counter = 0;
	}

	~RendererImpl()
	{
		m_texture_manager.destroy();
		m_model_manager.destroy();
		m_material_manager.destroy();
		m_shader_manager.destroy();
		m_pipeline_manager.destroy();

		bgfx::frame();
		bgfx::frame();
		bgfx::shutdown();
	}


	virtual IScene* createScene(Universe& universe) override
	{
		return RenderScene::createInstance(
			*this, m_engine, universe, true, m_allocator);
	}


	virtual void destroyScene(IScene* scene) override
	{
		RenderScene::destroyInstance(static_cast<RenderScene*>(scene));
	}


	void registerPropertyDescriptors()
	{
		ASSERT(m_engine.getWorldEditor());
		WorldEditor& editor = *m_engine.getWorldEditor();
		IAllocator& allocator = m_engine.getWorldEditor()->getAllocator();

		editor.registerComponentType("camera", "Camera");
		editor.registerComponentType("global_light", "Global light");
		editor.registerComponentType("renderable", "Mesh");
		editor.registerComponentType("point_light", "Point light");
		editor.registerComponentType("terrain", "Terrain");

		editor.registerProperty(
			"camera",
			allocator.newObject<StringPropertyDescriptor<RenderScene>>(
				"slot",
				&RenderScene::getCameraSlot,
				&RenderScene::setCameraSlot,
				allocator));
		editor.registerProperty(
			"camera",
			allocator.newObject<DecimalPropertyDescriptor<RenderScene>>(
				"fov",
				&RenderScene::getCameraFOV,
				&RenderScene::setCameraFOV,
				0.0f,
				360.0f,
				1.0f,
				allocator));
		editor.registerProperty(
			"camera",
			allocator.newObject<DecimalPropertyDescriptor<RenderScene>>(
				"near",
				&RenderScene::getCameraNearPlane,
				&RenderScene::setCameraNearPlane,
				0.0f,
				FLT_MAX,
				0.0f,
				allocator));
		editor.registerProperty(
			"camera",
			allocator.newObject<DecimalPropertyDescriptor<RenderScene>>(
				"far",
				&RenderScene::getCameraFarPlane,
				&RenderScene::setCameraFarPlane,
				0.0f,
				FLT_MAX,
				0.0f,
				allocator));

		editor.registerProperty(
			"renderable",
			allocator.newObject<ResourcePropertyDescriptor<RenderScene>>(
				"source",
				&RenderScene::getRenderablePath,
				&RenderScene::setRenderablePath,
				"Mesh (*.msh)",
				allocator));
		editor.registerProperty(
			"renderable",
			allocator.newObject<BoolPropertyDescriptor<RenderScene>>(
				"is_always_visible",
				&RenderScene::isRenderableAlwaysVisible,
				&RenderScene::setRenderableIsAlwaysVisible,
				allocator));

		editor.registerProperty(
			"global_light",
			allocator.newObject<DecimalPropertyDescriptor<RenderScene>>(
				"ambient_intensity",
				&RenderScene::getLightAmbientIntensity,
				&RenderScene::setLightAmbientIntensity,
				0.0f,
				1.0f,
				0.05f,
				allocator));
		editor.registerProperty(
			"global_light",
			allocator.newObject<DecimalPropertyDescriptor<RenderScene>>(
				"intensity",
				&RenderScene::getGlobalLightIntensity,
				&RenderScene::setGlobalLightIntensity,
				0.0f,
				1.0f,
				0.05f,
				allocator));
		editor.registerProperty(
			"global_light",
			allocator.newObject<DecimalPropertyDescriptor<RenderScene>>(
				"fog_density",
				&RenderScene::getFogDensity,
				&RenderScene::setFogDensity,
				0.0f,
				1.0f,
				0.01f,
				allocator));
		editor.registerProperty(
			"global_light",
			allocator.newObject<ColorPropertyDescriptor<RenderScene>>(
				"ambient_color",
				&RenderScene::getLightAmbientColor,
				&RenderScene::setLightAmbientColor,
				allocator));
		editor.registerProperty(
			"global_light",
			allocator.newObject<ColorPropertyDescriptor<RenderScene>>(
				"color",
				&RenderScene::getGlobalLightColor,
				&RenderScene::setGlobalLightColor,
				allocator));
		editor.registerProperty(
			"global_light",
			allocator.newObject<ColorPropertyDescriptor<RenderScene>>(
				"fog_color",
				&RenderScene::getFogColor,
				&RenderScene::setFogColor,
				allocator));

		editor.registerProperty(
			"point_light",
			allocator.newObject<DecimalPropertyDescriptor<RenderScene>>(
				"intensity",
				&RenderScene::getPointLightIntensity,
				&RenderScene::setPointLightIntensity,
				0.0f,
				1.0f,
				0.05f,
				allocator));
		editor.registerProperty(
			"point_light",
			allocator.newObject<ColorPropertyDescriptor<RenderScene>>(
				"color",
				&RenderScene::getPointLightColor,
				&RenderScene::setPointLightColor,
				allocator));
		editor.registerProperty(
			"point_light",
			allocator.newObject<ColorPropertyDescriptor<RenderScene>>(
				"specular",
				&RenderScene::getPointLightSpecularColor,
				&RenderScene::setPointLightSpecularColor,
				allocator));
		editor.registerProperty(
			"point_light",
			allocator.newObject<DecimalPropertyDescriptor<RenderScene>>(
				"range",
				&RenderScene::getLightRange,
				&RenderScene::setLightRange,
				0.0f,
				FLT_MAX,
				0.0f,
				allocator));
		editor.registerProperty(
			"point_light",
			allocator.newObject<DecimalPropertyDescriptor<RenderScene>>(
				"FOV",
				&RenderScene::getLightFOV,
				&RenderScene::setLightFOV,
				0.0f,
				360.0f,
				5.0f,
				allocator));

		editor.registerProperty(
			"terrain",
			allocator.newObject<ResourcePropertyDescriptor<RenderScene>>(
				"material",
				&RenderScene::getTerrainMaterialPath,
				&RenderScene::setTerrainMaterialPath,
				"Material (*.mat)",
				allocator));
		editor.registerProperty(
			"terrain",
			allocator.newObject<DecimalPropertyDescriptor<RenderScene>>(
				"xz_scale",
				&RenderScene::getTerrainXZScale,
				&RenderScene::setTerrainXZScale,
				0.0f,
				FLT_MAX,
				0.0f,
				allocator));
		editor.registerProperty(
			"terrain",
			allocator.newObject<DecimalPropertyDescriptor<RenderScene>>(
				"y_scale",
				&RenderScene::getTerrainYScale,
				&RenderScene::setTerrainYScale,
				0.0f,
				FLT_MAX,
				0.0f,
				allocator));

		auto grass = allocator.newObject<ArrayDescriptor<RenderScene>>(
			"grass",
			&RenderScene::getGrassCount,
			&RenderScene::addGrass,
			&RenderScene::removeGrass,
			allocator);
		grass->addChild(
			allocator.newObject<ResourceArrayObjectDescriptor<RenderScene>>(
				"mesh",
				&RenderScene::getGrassPath,
				&RenderScene::setGrassPath,
				"Mesh (*.msh)",
				allocator));
		auto ground =
			allocator.newObject<IntArrayObjectDescriptor<RenderScene>>(
				"ground",
				&RenderScene::getGrassGround,
				&RenderScene::setGrassGround,
				allocator);
		ground->setLimit(0, 4);
		grass->addChild(ground);
		grass->addChild(
			allocator.newObject<IntArrayObjectDescriptor<RenderScene>>(
				"density",
				&RenderScene::getGrassDensity,
				&RenderScene::setGrassDensity,
				allocator));
		editor.registerProperty("terrain", grass);
	}


	virtual bool create() override { return true; }


	virtual void destroy() override {}


	virtual const char* getName() const override { return "renderer"; }


	virtual Engine& getEngine() override { return m_engine; }


	virtual int getPassIdx(const char* pass) override
	{
		for (int i = 0; i < m_passes.size(); ++i)
		{
			if (strcmp(m_passes[i], pass) == 0)
			{
				return i;
			}
		}

		auto& new_pass = m_passes.pushEmpty();
		copyString(new_pass, sizeof(new_pass), pass);
		return m_passes.size() - 1;
	}


	virtual void setWorldEditor(WorldEditor& editor) override
	{
		registerPropertyDescriptors();
	}


	virtual void makeScreenshot(const Path& filename) override
	{
		bgfx::saveScreenShot(filename.c_str());
	}


	virtual void frame() override
	{
		bgfx::frame();
		m_view_counter = 0;
	}


	virtual int getViewCounter() const override
	{
		return m_view_counter;
	}


	virtual void viewCounterAdd() override
	{
		++m_view_counter;
	}


	Engine& m_engine;
	Debug::Allocator m_allocator;
	Lumix::Array<ShaderCombinations::Pass> m_passes;
	CallbackStub m_callback_stub;
	TextureManager m_texture_manager;
	MaterialManager m_material_manager;
	ShaderManager m_shader_manager;
	ModelManager m_model_manager;
	PipelineManager m_pipeline_manager;
	uint32_t m_current_pass_hash;
	int m_view_counter;

	static HWND s_hwnd;
};


HWND RendererImpl::s_hwnd;


void Renderer::setInitData(void* data)
{
	RendererImpl::s_hwnd = (HWND)data;
}


Renderer* Renderer::createInstance(Engine& engine)
{
	return engine.getAllocator().newObject<RendererImpl>(engine);
}


void Renderer::destroyInstance(Renderer& renderer)
{
	renderer.getEngine().getAllocator().deleteObject(&renderer);
}


} // ~namespace Lumix
