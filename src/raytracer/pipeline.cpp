#include "pipeline.h"

#include "core/fs/ifile.h"
#include "core/fs/file_system.h"
#include "core/geometry.h"
#include "core/lifo_allocator.h"
#include "core/iallocator.h"
#include "core/log.h"
#include "core/profiler.h"
#include "engine.h"
#include "core/vec.h"
#include "frame_buffer.h"
#include "render_scene.h"
#include "renderer.h"
#include "universe/universe.h"
#include <bgfx/bgfx.h>
#include <cmath>
#include "core/resource_manager.h"
#include "shader_manager.h"
#include "shader.h"


namespace Lumix
{


static const float SHADOW_CAM_NEAR = 50.0f;
static const float SHADOW_CAM_FAR = 5000.0f;


struct InstanceData
{
	static const int MAX_INSTANCE_COUNT = 128;

	const bgfx::InstanceDataBuffer* buffer;
	int instance_count;
	Model* model;
};


struct View
{
	uint8 bgfx_id;
	uint64 render_state;
	CommandBufferGenerator command_buffer;
};


enum class BufferCommands : uint8
{
	END,
	SET_TEXTURE,

	COUNT
};


#pragma pack(1)
struct SetTextureCommand
{
	SetTextureCommand() : type(BufferCommands::SET_TEXTURE) {}
	BufferCommands type;
	uint8 stage;
	bgfx::UniformHandle uniform;
	bgfx::TextureHandle texture;
};

#pragma pack()


CommandBufferGenerator::CommandBufferGenerator()
{
	pointer = buffer;
}


void CommandBufferGenerator::setTexture(uint8 stage,
	const bgfx::UniformHandle& uniform,
	const bgfx::TextureHandle& texture)
{
	SetTextureCommand cmd;
	cmd.stage = stage;
	cmd.uniform = uniform;
	cmd.texture = texture;
	ASSERT(pointer + sizeof(cmd) - buffer <= sizeof(buffer));
	copyMemory(pointer, &cmd, sizeof(cmd));
	pointer += sizeof(cmd);
}


void CommandBufferGenerator::getData(uint8* data)
{
	copyMemory(data, buffer, pointer - buffer);
}


void CommandBufferGenerator::clear()
{
	buffer[0] = (uint8)BufferCommands::END;
	pointer = buffer;
}


void CommandBufferGenerator::beginAppend()
{
	if (pointer != buffer) --pointer;
}


void CommandBufferGenerator::end()
{
	ASSERT(pointer + 1 - buffer <= sizeof(buffer));
	*pointer = (uint8)BufferCommands::END;
	++pointer;
}



struct PipelineImpl : public Pipeline
{
	PipelineImpl(Renderer& renderer, const Path& path, IAllocator& allocator)
		: m_allocator(allocator)
		, m_renderer(renderer)
		, m_is_ready(false)
		, m_debug_flags(BGFX_DEBUG_TEXT)
		, m_bgfx_view(0)
	{
		const Vec3 texture_vertices[] = {
			{ 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 }
		};
		static const uint16 texture_indices[] = {
			0, 1, 2, 3
		};

		bgfx::VertexDecl texture_vertices_vd;
		texture_vertices_vd.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.end();

		auto* vertices_mem = bgfx::copy(texture_vertices, sizeof(texture_vertices));
		auto* indices_mem = bgfx::copy(texture_indices, sizeof(texture_indices));

		m_texture_vb = bgfx::createVertexBuffer(vertices_mem, texture_vertices_vd);
		m_texture_ib = bgfx::createIndexBuffer(indices_mem);


		auto* binary_manager = m_renderer.getEngine()
			.getResourceManager().get(ResourceManager::SHADER_BINARY);

		Path vs_path("shaders/raytracer_vs.shb");
		Path fs_path("shaders/raytracer_fs.shb");
		auto* vs_binary = static_cast<ShaderBinary*>(binary_manager->load(vs_path));
		auto* fs_binary = static_cast<ShaderBinary*>(binary_manager->load(fs_path));

		auto vs_handle = vs_binary->getHandle();
		auto fs_handle = fs_binary->getHandle();

		auto program = bgfx::createProgram(vs_handle, fs_handle);


		bgfx::setViewClear(m_bgfx_view
			, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
			, 0x303030ff
			, 1.0f
			, 0
			);

		m_view_x = m_view_y = 0;

		m_scene = nullptr;
		m_width = m_height = -1;

		m_stats = {};

		m_is_ready = true;//TODO: ready after file load
	}


	~PipelineImpl()
	{
		bgfx::destroyVertexBuffer(m_texture_vb);
		bgfx::destroyIndexBuffer(m_texture_ib);
	}


	const Stats& getStats() override
	{
		return m_stats;
	}


	void load() override
	{

	}


	void cleanup()
	{
		bgfx::frame();
		bgfx::frame();
	}


	void onFileLoaded(FS::IFile& file, bool success)
	{
		if(!success) return;

		cleanup();
		m_width = m_height = -1;
		m_is_ready = true;
	}


	void setViewProjection(const Matrix& mtx, int width, int height) override
	{
		bgfx::setViewRect(m_bgfx_view, 0, 0, (uint16_t)width, (uint16_t)height);
		bgfx::setViewTransform(m_bgfx_view, nullptr, &mtx.m11);
	}


	int getWidth() override { return m_width; }


	int getHeight() override { return m_height; }


	float getFPS()
	{
		return m_renderer.getEngine().getFPS();
	}


	float getCPUTime() const override
	{
		auto* stats = bgfx::getStats();
		return float(double(stats->cpuTimeEnd - stats->cpuTimeBegin) / (double)stats->cpuTimerFreq);
	}


	float getGPUTime() const override
	{
		auto* stats = bgfx::getStats();
		return float(double(stats->gpuTimeEnd - stats->gpuTimeBegin) / (double)stats->gpuTimerFreq);
	}


	void toggleStats() override
	{
		m_debug_flags ^= BGFX_DEBUG_STATS;
		bgfx::setDebug(m_debug_flags);
	}


	void renderModel(Model& model, const Matrix& mtx) override
	{
		
	}


	void executeCommandBuffer(const uint8* data) const
	{
		const uint8* ip = data;
		for (;;)
		{
			switch ((BufferCommands)*ip)
			{
				case BufferCommands::END:
					return;
				case BufferCommands::SET_TEXTURE:
				{
					auto cmd = (SetTextureCommand*)ip;
					ip += sizeof(*cmd);
					bgfx::setTexture(cmd->stage, cmd->uniform, cmd->texture);
					break;
				}
				default:
					ASSERT(false);
					break;
			}
		}
	}


	void setViewport(int x, int y, int w, int h) override
	{
		m_view_x = x;
		m_view_y = y;
		if (m_width == w && m_height == h) return;

		m_width = w;
		m_height = h;
	}


	void render() override
	{
		PROFILE_FUNCTION();

		if (!isReady()) return;
		if (!m_scene) return;

		bgfx::setStencil(BGFX_STENCIL_NONE, BGFX_STENCIL_NONE);
		bgfx::setState(m_render_state);
		//bgfx::setTransform(&mtx.m11);
		bgfx::setVertexBuffer(m_texture_vb);
		bgfx::setIndexBuffer(m_texture_ib, 0, 4);
		//++m_stats.m_draw_call_count;
		//++m_stats.m_instance_count;
		//m_stats.m_triangle_count += 2;

		bgfx::setState(BGFX_STATE_DEFAULT);

		TODO("render models");
		bgfx::submit(m_bgfx_view, m_program);
	}


	bool hasScene()
	{
		return m_scene != nullptr;
	}


	void clear(uint32 flags, uint32 color)
	{
		bgfx::setViewClear(m_bgfx_view, (uint16)flags, color, 1.0f, 0);
		bgfx::touch(m_bgfx_view);
	}


	bool isReady() const override { return m_is_ready; }


	void setScene(RenderScene* scene) override 
	{
		m_scene = scene;
	}


	uint32 m_debug_flags;
	uint8 m_bgfx_view;
	uint64 m_render_state;

	IAllocator& m_allocator;
	Renderer& m_renderer;
	RenderScene* m_scene;

	bgfx::VertexBufferHandle m_texture_vb;
	bgfx::IndexBufferHandle m_texture_ib;
	//bgfx::TextureHandle 
	bgfx::ProgramHandle m_program;//TODO: shader system

	bool m_is_ready;
	ComponentIndex m_applied_camera;

	Stats m_stats;

	int m_view_x;
	int m_view_y;
	int m_width;
	int m_height;
};


Pipeline* Pipeline::create(Renderer& renderer, const Path& path, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, PipelineImpl)(renderer, path, allocator);
}


void Pipeline::destroy(Pipeline* pipeline)
{
	LUMIX_DELETE(static_cast<PipelineImpl*>(pipeline)->m_allocator, pipeline);
}


} // ~namespace Lumix
