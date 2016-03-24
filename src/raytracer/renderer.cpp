#include "renderer.h"

#include "iplugin.h"
#include "core/crc32.h"
#include "engine/property_descriptor.h"
#include "engine/property_register.h"
#include <bgfx/bgfx.h>
#include "core/log.h"
#include "debug/debug.h"
#include "core/fs/os_file.h"
#include "engine.h"
#include "core/profiler.h"
#include <cfloat>
#include "renderer/model_manager.h"
#include "renderer/shader_manager.h"
#include "render_scene.h"
#include "core/resource_manager.h"
//#include ""


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
	PropertyRegister::registerComponentType("renderable_model", "Model");
}


static const uint32 RENDERABLE_MODEL = crc32("renderable_model");
static const uint32 CAMERA_HASH = crc32("camera");


struct BGFXAllocator : public bx::AllocatorI
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


struct RendererImpl : public Renderer
{
	struct CallbackStub : public bgfx::CallbackI
	{
		explicit CallbackStub(RendererImpl& renderer)
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


	explicit RendererImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_bgfx_allocator(m_allocator)
		, m_callback_stub(*this)
		, m_model_manager(m_allocator, *this)
		, m_shader_manager(*this, m_allocator)
		, m_shader_binary_manager(*this, m_allocator)
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
		m_model_manager.create(ResourceManager::MODEL, manager);
		m_shader_manager.create(ResourceManager::SHADER, manager);
		m_shader_binary_manager.create(ResourceManager::SHADER_BINARY, manager);
	}

	~RendererImpl()
	{
		m_model_manager.destroy();
		m_shader_manager.destroy();
		m_shader_binary_manager.destroy();

		bgfx::frame();
		bgfx::frame();
		bgfx::shutdown();
	}


	IScene* createScene(Universe& ctx) override
	{
		return RenderScene::createInstance(*this, m_engine, ctx, m_allocator);
	}


	void destroyScene(IScene* scene) override
	{
		RenderScene::destroyInstance(static_cast<RenderScene*>(scene));
	}


	bool create() override { return true; }


	void destroy() override {}


	const char* getName() const override { return "renderer"; }


	Engine& getEngine() override { return m_engine; }


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
	}


	Engine& m_engine;
	IAllocator& m_allocator;
	CallbackStub m_callback_stub;
	BGFXAllocator m_bgfx_allocator;

	ModelManager m_model_manager;
	ShaderManager m_shader_manager;
	ShaderBinaryManager m_shader_binary_manager;
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



