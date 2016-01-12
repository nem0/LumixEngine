#include "renderer.h"

#include "core/array.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/fs/os_file.h"
#include "core/json_serializer.h"
#include "core/lifo_allocator.h"
#include "core/log.h"
#include "core/math_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/vec.h"
#include "debug/debug.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "renderer/material.h"
#include "renderer/material_manager.h"
#include "renderer/model.h"
#include "renderer/model_manager.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/shader.h"
#include "renderer/shader_manager.h"
#include "renderer/texture.h"
#include "renderer/texture_manager.h"
#include "universe/universe.h"
#include <bgfx/bgfx.h>
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


		void traceVargs(const char* _filePath,
			uint16 _line,
			const char* _format,
			va_list _argList) override
		{
			char tmp[2048];
			vsnprintf(tmp, sizeof(tmp), _format, _argList);
			Lumix::g_log_info.log("bgfx") << _filePath << "(" << _line << ") " << tmp;
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
				g_log_error.log("renderer") << "Failed to save screenshot to " << filePath;
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
		bgfx::PlatformData d;
		if (s_platform_data)
		{
			setMemory(&d, 0, sizeof(d));
			d.nwh = s_platform_data;
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
	}

	~RendererImpl()
	{
		m_texture_manager.destroy();
		m_model_manager.destroy();
		m_material_manager.destroy();
		m_shader_manager.destroy();
		m_shader_binary_manager.destroy();

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


	IScene* createScene(UniverseContext& ctx) override
	{
		return RenderScene::createInstance(
			*this, m_engine, *ctx.m_universe, true, m_allocator);
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


	int getShaderDefineIdx(const char* define) override
	{
		for (int i = 0; i < m_shader_defines.size(); ++i)
		{
			if (compareString(m_shader_defines[i], define) == 0)
			{
				return i;
			}
		}

		auto& new_define = m_shader_defines.pushEmpty();
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

		auto& new_pass = m_passes.pushEmpty();
		copyString(new_pass, pass);
		return m_passes.size() - 1;
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


	typedef char ShaderDefine[32];


	Engine& m_engine;
	Debug::Allocator m_allocator;
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
	BGFXAllocator m_bgfx_allocator;
	bgfx::VertexDecl m_basic_vertex_decl;
	bgfx::VertexDecl m_basic_2d_vertex_decl;

	static void* s_platform_data;
};


void* RendererImpl::s_platform_data = nullptr;


void Renderer::setInitData(void* data)
{
	RendererImpl::s_platform_data = data;
}


extern "C"
{
	LUMIX_RENDERER_API IPlugin* createPlugin(Engine& engine)
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


} // ~namespace Lumix



