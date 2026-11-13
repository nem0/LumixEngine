#include "renderer.h"

#include "core/arena_allocator.h"
#include "core/array.h"
#include "core/atomic.h"
#include "core/command_line_parser.h"
#include "engine/engine.h"
#include "core/hash.h"
#include "core/log.h"
#include "core/job_system.h"
#include "core/page_allocator.h"
#include "core/sync.h"
#include "core/thread.h"
#include "core/os.h"
#include "core/profiler.h"
#include "core/sort.h"
#include "core/stack_array.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "core/string.h"
#include "engine/world.h"
#include "renderer/draw_stream.h"
#include "renderer/font.h"
#include "renderer/material.h"
#include "renderer/mesh_clusters.h"
#include "renderer/model.h"
#include "renderer/particle_system.h"
#include "renderer/pipeline.h"
#include "renderer/postprocess.h"
#include "renderer/evox_render.h"
#include "renderer/render_module.h"
#include "renderer/shader.h"
#include "renderer/sprite.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"

namespace Lumix {

RenderBufferHandle RenderPlugin::renderBeforeTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) { return input; }
RenderBufferHandle RenderPlugin::renderBeforeTransparent(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) { return input; }
RenderBufferHandle RenderPlugin::renderAfterTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) { return input; }
bool RenderPlugin::tonemap(RenderBufferHandle input, RenderBufferHandle& output, Pipeline& pipeline) { return false; }
bool RenderPlugin::debugOutput(RenderBufferHandle input, Pipeline& pipeline) { return false; }
RenderBufferHandle RenderPlugin::renderAA(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) { return INVALID_RENDERBUFFER; }

// -cluster_stream_mb <n>: the budget of the pages of clusters that are streamed (without the permanent ones)
static u64 getClusterStreamBudget() {
	u64 mb = 384;	// 3/4 of the buffer of cluster data (CLUSTER_DATA_POOL_SIZE): the rest is for the tables, the permanent pages and the slack that fragmentation needs
	char tmp[4096];
	if (!os::getCommandLine(Span(tmp))) return mb << 20;
	CommandLineParser parser(tmp);
	while (parser.next()) {
		if (!parser.currentEquals("-cluster_stream_mb")) continue;
		if (!parser.next()) break;
		char value[32];
		parser.getCurrent(value, sizeof(value));
		u64 parsed = 0;
		for (const char* c = value; *c >= '0' && *c <= '9'; ++c) parsed = parsed * 10 + u64(*c - '0');
		if (parsed > 0) mb = parsed;
		break;
	}
	return mb << 20;
}

#ifdef _WIN32
void initFSR3(Renderer& renderer, IAllocator& allocator);
bool initDLSS(Renderer& renderer, IAllocator& allocator);
#endif

struct Renderbuffer {
	// The buffer is created in the ACTIVE state.
	// Once the user no longer needs the buffer, it can be marked as REUSABLE.
	// At the end of a frame, every REUSABLE buffer is marked as TO_REMOVE,
	// and every TO_REMOVE buffer is released.
	// This ensures that unused buffers are not kept around longer than necessary,
	// and buffers can be reused instead of being destroyed and recreated within the same frame.
	enum State {
		ACTIVE,
		REUSABLE,
		TO_REMOVE
	};
	
	#ifdef LUMIX_DEBUG
		StaticString<32> debug_name;
	#endif
	gpu::TextureHandle handle;
	IVec2 size;
	gpu::TextureFormat format;
	gpu::TextureFlags flags;
	State state;
};

template <u32 ALIGN>
struct TransientBuffer {
	static constexpr u32 INIT_SIZE = 1024 * 1024;
	static constexpr u32 OVERFLOW_BUFFER_SIZE = 512 * 1024 * 1024;
	
	void init() {
		m_buffer = gpu::allocBufferHandle();
		m_offset = 0;
		gpu::createBuffer(m_buffer, gpu::BufferFlags::MAPPABLE, INIT_SIZE, nullptr, "transient");
		m_size = INIT_SIZE;
		m_ptr = (u8*)gpu::map(m_buffer, INIT_SIZE);
		memset(m_ptr, 0, INIT_SIZE);
	}

	LUMIX_FORCE_INLINE TransientSlice alloc(u32 size) {
		TransientSlice slice;
		size = (size + (ALIGN - 1)) & ~(ALIGN - 1);
		slice.offset = m_offset.add(size);
		slice.size = size;
		if (slice.offset + size <= m_size) {
			ASSERT(m_ptr);
			slice.buffer = m_buffer;
			slice.ptr = m_ptr + slice.offset;
			return slice;
		}

		jobs::MutexGuard lock(m_mutex);
		if (!m_overflow.buffer) {
			m_overflow.buffer = gpu::allocBufferHandle();
			m_overflow.data = (u8*)os::memReserve(OVERFLOW_BUFFER_SIZE);
			m_overflow.size = 0;
			m_overflow.commit = 0;
		}
		slice.ptr = m_overflow.data + m_overflow.size;
		slice.offset = m_overflow.size;
		m_overflow.size += size;
		if (m_overflow.size > m_overflow.commit) {
			const u32 page_size = os::getMemPageSize();
			m_overflow.commit = (m_overflow.size + page_size - 1) & ~(page_size - 1);
			os::memCommit(m_overflow.data, m_overflow.commit);
		}
		slice.buffer = m_overflow.buffer;
		return slice;
	} 

	void prepareToRender() {
		if (!m_overflow.buffer) return;

		gpu::createBuffer(m_overflow.buffer, gpu::BufferFlags::MAPPABLE, nextPow2(m_overflow.size + m_size), nullptr, "transient");
		void* mem = gpu::map(m_overflow.buffer, m_overflow.size + m_size);
		if (mem) {
			memcpy(mem, m_overflow.data, m_overflow.size);
			gpu::unmap(m_overflow.buffer);
		}
		os::memRelease(m_overflow.data, OVERFLOW_BUFFER_SIZE);
		m_overflow.data = nullptr;
		m_overflow.commit = 0;
	}

	void renderDone() {
		m_offset = 0;
		if (!m_overflow.buffer) return;

		ASSERT(m_size < 1024*1024*1024);
		m_size = nextPow2(m_overflow.size + m_size);
		ASSERT(m_size < 1024*1024*1024);
		gpu::destroy(m_buffer);
		m_buffer = m_overflow.buffer;
		m_overflow.buffer = gpu::INVALID_BUFFER;
		m_overflow.size = 0;
		m_ptr = (u8*)gpu::map(m_buffer, m_size);
	}

	gpu::BufferHandle m_buffer = gpu::INVALID_BUFFER;
	AtomicI32 m_offset = 0;
	u32 m_size = 0;
	u8* m_ptr = nullptr;
	jobs::Mutex m_mutex;

	struct {
		gpu::BufferHandle buffer = gpu::INVALID_BUFFER;
		u8* data = nullptr;
		u32 size = 0;
		u32 commit = 0;
	} m_overflow;
};

struct UniformPool : TransientBuffer<256> {};
struct TransientPool : TransientBuffer<16> {};

LUMIX_RENDERER_API TransientSlice alloc(UniformPool& pool, u32 size) {
	return pool.alloc(size);
}

LUMIX_RENDERER_API TransientSlice alloc(TransientPool& pool, u32 size) {
	return pool.alloc(size);
}

LUMIX_RENDERER_API TransientSlice alloc(UniformPool& pool, const void* data, u32 size) {
	TransientSlice slice = pool.alloc(size);
	memcpy(slice.ptr, data, size);
	return slice;
}

struct FrameData {
	FrameData(struct RendererImpl& renderer, IAllocator& allocator, PageAllocator& page_allocator);

	struct ShaderToCompile {
		Shader* shader;
		StableHash content_hash;
		gpu::ProgramHandle program;
		ShaderKey key;
	};

	TransientPool transient_pool;
	UniformPool uniform_pool;
	u32 gpu_frame = 0xffFFffFF;

	ArenaAllocator arena_allocator;
	jobs::Mutex shader_mutex;
	Array<ShaderToCompile> to_compile_shaders;
	RendererImpl& renderer;
	jobs::Signal can_setup;
	jobs::Counter setup_done;
	u32 frame_number = 0;
	DrawStream begin_frame_draw_stream;
	DrawStream draw_stream;
	DrawStream end_frame_draw_stream;
};


template <typename T>
struct RenderResourceManager : ResourceManager
{
	RenderResourceManager(const char* type_name ,Renderer& renderer, IAllocator& allocator) 
		: ResourceManager(allocator)
		, m_renderer(renderer)
		, m_allocator(allocator, type_name)
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
	TagAllocator m_allocator;
};

template <>
Resource* RenderResourceManager<ParticleSystemResource>::createResource(const Path& path) {
	return LUMIX_NEW(m_allocator, ParticleSystemResource)(path, *this, m_allocator);
}

struct SpriteResourceManager final : ResourceManager {
	SpriteResourceManager(IAllocator& allocator)
		: ResourceManager(allocator)
		, m_allocator(allocator, "sprites")
	{}

	Resource* createResource(const Path& path) override {
		return LUMIX_NEW(m_allocator, Sprite)(path, *this, m_allocator);
	}

	void destroyResource(Resource& resource) override {
		LUMIX_DELETE(m_allocator, &resource);
	}

	TagAllocator m_allocator;
};


struct GPUProfiler
{
	struct Query
	{
		StaticString<32> name;
		gpu::QueryHandle handle;
		gpu::QueryHandle stats = gpu::INVALID_QUERY;
		u64 result;
		i64 profiler_link;
		bool is_end;
		bool is_frame;
	};


	GPUProfiler(IAllocator& allocator) 
		: m_queries(allocator)
		, m_pool(allocator)
		, m_stats_pool(allocator)
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
		return u64(gpu_timestamp * (os::Timer::getFrequency() / double(gpu::getQueryFrequency()))) + m_gpu_to_cpu_offset;
	}


	void init()
	{
		PROFILE_FUNCTION();
		gpu::QueryHandle q = gpu::createQuery(gpu::QueryType::TIMESTAMP);
		gpu::queryTimestamp(q);
		const u64 cpu_timestamp = os::Timer::getRawTimestamp();

		u32 try_num = 0;
		while (!gpu::isQueryReady(q) && try_num < 10) {
			gpu::present();
			++try_num;
		}
		if (try_num == 10) {
			logError("Failed to get GPU timestamp, timings are unreliable.");
			m_gpu_to_cpu_offset = 0;
		}
		else {
			const u64 gpu_timestamp = gpu::getQueryResult(q);
			m_gpu_to_cpu_offset = cpu_timestamp - u64(gpu_timestamp * (os::Timer::getFrequency() / double(gpu::getQueryFrequency())));
			gpu::destroy(q);
		}
	}


	void clear()
	{
		for(const Query& q : m_queries) {
			if (!q.is_frame) gpu::destroy(q.handle);
		}
		m_queries.clear();

		for(const gpu::QueryHandle h : m_pool) {
			gpu::destroy(h);
		}
		m_pool.clear();

		if (m_stats_query) gpu::destroy(m_stats_query);
		m_stats_query = gpu::INVALID_QUERY;

		for(const gpu::QueryHandle h : m_stats_pool) {
			gpu::destroy(h);
		}
		m_stats_pool.clear();
	}


	gpu::QueryHandle allocQuery()
	{
		if(!m_pool.empty()) {
			const gpu::QueryHandle res = m_pool.back();
			m_pool.pop();
			return res;
		}
		return gpu::createQuery(gpu::QueryType::TIMESTAMP);
	}

	gpu::QueryHandle allocStatsQuery()
	{
		if(!m_stats_pool.empty()) {
			const gpu::QueryHandle res = m_stats_pool.back();
			m_stats_pool.pop();
			return res;
		}
		return gpu::createQuery(gpu::QueryType::STATS);
	}


	void beginQuery(const char* name, i64 profiler_link, bool stats)
	{
		jobs::MutexGuard lock(m_mutex);
		Query& q = m_queries.emplace();
		q.profiler_link = profiler_link;
		q.name = name;
		q.is_end = false;
		q.is_frame = false;
		q.handle = allocQuery();
		gpu::queryTimestamp(q.handle);
		if (stats) {
			ASSERT(m_stats_counter == 0); // nested counters are not supported
			m_stats_query = allocStatsQuery();
			gpu::beginQuery(m_stats_query);
			m_stats_counter = 1;
			m_stats_name = name;
		}
		else if (m_stats_counter > 0) {
			++m_stats_counter;
		}
	}


	void endQuery()
	{
		jobs::MutexGuard lock(m_mutex);
		Query& q = m_queries.emplace();
		q.is_end = true;
		q.is_frame = false;
		q.handle = allocQuery();
		gpu::queryTimestamp(q.handle);
		if (m_stats_counter > 0) {
			--m_stats_counter;
			if (m_stats_counter == 0) {
				gpu::endQuery(m_stats_query);
				q.stats = m_stats_query;
				q.name = m_stats_name;
				m_stats_query = gpu::INVALID_QUERY;
			}
		}
	}


	// a counter for each block with stats, e.g. the geometry pass and every shadow pass have their own, "GPU triangles" is the sum
	void pushBlockTriangles(const StaticString<32>& block, u64 primitives) {
		for (u32 i = 0; i < m_block_counters_count; ++i) {
			if (m_block_counters[i].name == block.data) {
				profiler::pushCounter(m_block_counters[i].counter, (float)primitives);
				return;
			}
		}
		if (m_block_counters_count == lengthOf(m_block_counters)) return;
		BlockCounter& c = m_block_counters[m_block_counters_count++];
		c.name = block;
		c.counter = profiler::createCounter(StaticString<64>("GPU triangles: ", block.data), 0);
		profiler::pushCounter(c.counter, (float)primitives);
	}

	void frame()
	{
		PROFILE_FUNCTION();
		jobs::MutexGuard lock(m_mutex);
		i32 processed = 0;
		while (processed < m_queries.size()) {
			Query q = m_queries[processed];
			
			if (!gpu::isQueryReady(q.handle)) break;

			if (q.is_end) {
				if (q.stats && !gpu::isQueryReady(q.stats)) break;

				const u64 timestamp = toCPUTimestamp(gpu::getQueryResult(q.handle));
				if (q.stats) {
					const u64 primitives = gpu::getQueryResult(q.stats);
					profiler::gpuStats(primitives);
					m_frame_triangles += primitives;
					pushBlockTriangles(q.name, primitives);
					m_stats_pool.push(q.stats);
				}
				profiler::endGPUBlock(timestamp);
				// the block "frame" is the end of the frame. Blocks with stats that are not in it, e.g. passes of the game that run before it,
				// are counted in the frame that follows them
				const bool is_frame = m_open_blocks_count > 0 && m_open_blocks_count <= lengthOf(m_open_blocks) && m_open_blocks[m_open_blocks_count - 1] == "frame";
				if (m_open_blocks_count > 0) --m_open_blocks_count;
				if (is_frame) {
					static u32 triangles_counter = profiler::createCounter("GPU triangles", 0);
					profiler::pushCounter(triangles_counter, (float)m_frame_triangles);
					m_frame_triangles = 0;
				}
			}
			else {
				const u64 timestamp = toCPUTimestamp(gpu::getQueryResult(q.handle));
				profiler::beginGPUBlock(q.name, timestamp, q.profiler_link);
				if (m_open_blocks_count < lengthOf(m_open_blocks)) m_open_blocks[m_open_blocks_count] = q.name;
				++m_open_blocks_count;
			}
			m_pool.push(q.handle);
			++processed;
		}
		if (processed > 0) {
			m_queries.eraseRange(0, processed);
		}
	}


	Array<Query> m_queries;
	Array<gpu::QueryHandle> m_pool;
	Array<gpu::QueryHandle> m_stats_pool;
	jobs::Mutex m_mutex;
	i64 m_gpu_to_cpu_offset;
	u32 m_stats_counter = 0;
	gpu::QueryHandle m_stats_query = gpu::INVALID_QUERY;
	StaticString<32> m_stats_name;	// of the block with stats that is being recorded
	struct BlockCounter {
		StaticString<32> name;
		u32 counter;
	};
	BlockCounter m_block_counters[16];
	u32 m_block_counters_count = 0;
	StaticString<32> m_open_blocks[32];	// names of the GPU blocks the profiler has begun and not ended, see frame()
	u32 m_open_blocks_count = 0;
	u64 m_frame_triangles = 0;		// primitives of the blocks with stats of the frame processed so far
};


struct RendererImpl final : Renderer {
	explicit RendererImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator(), "renderer")
		, m_texture_manager("textures", *this, m_allocator)
		, m_model_manager("models", *this, m_allocator)
		, m_particle_emitter_manager("particle emitters", *this, m_allocator)
		, m_material_manager("materials", *this, m_allocator)
		, m_shader_manager("shaders", *this, m_allocator)
		, m_sprite_manager(m_allocator)
		, m_font_manager(nullptr)
		, m_shader_defines(m_allocator)
		, m_profiler(m_allocator)
		, m_layers(m_allocator)
		, m_cluster_data_free(m_allocator)
		, m_cluster_data_pending(m_allocator)
		, m_streams(m_allocator)
		, m_stream_id_free(m_allocator)
		, m_stream_wanted(m_allocator)
		, m_stream_by_id(m_allocator)
		, m_feedback_incoming(m_allocator)
		, m_feedback_local(m_allocator)
		, m_material_buffer(m_allocator)
		, m_plugins(m_allocator)
		, m_render_script(engine, m_allocator)
		, m_semantic_defines(m_allocator)
		, m_free_frames(m_allocator)
		, m_renderbuffers(m_allocator)
		, m_frame_thread(*this)
		, m_cubemap_sky(*this)
		, m_tdao(*this)
	{
		RenderModule::reflect();

		m_shader_defines.reserve(32);
		m_num_shader_defines = 0;

		bool try_load_renderdoc = CommandLineParser::isOn("-renderdoc");
		gpu::preinit(m_allocator, try_load_renderdoc);
		for (Local<FrameData>& f : m_frames) f.create(*this, m_allocator, m_engine.getPageAllocator());

		m_frame_thread.create("frame_thread", true);
		addPlugin(m_cubemap_sky);
		addPlugin(m_tdao);
		addPlugin(m_render_script);
	}

	Pipeline& createPipeline(PipelineType type) override {
		return *Pipeline::create(*this, type).detach();
	}

	void destroyPipeline(Pipeline& pipeline) override {
		ASSERT(&pipeline.getRenderer() == this);
		LUMIX_DELETE(m_allocator, &pipeline);
	}

	float getLODMultiplier() const override { return m_lod_multiplier; }
	void setLODMultiplier(float value) override { m_lod_multiplier = maximum(0.f, value); }

	bool requestScreenshot(StringView path, u32 max_width, u32 max_height) override {
		const ScreenshotState state = getScreenshotState();
		if (state == ScreenshotState::PENDING || state == ScreenshotState::SAVING) {
			logError("Screenshot request for ", path, " refused: ", StringView(m_screenshot_path), " is still in progress");
			return false;
		}
		const bool absolute = path.size() > 0 && (path.data[0] == '/' || path.data[0] == '\\' || (path.size() > 1 && path.data[1] == ':'));
		if (!absolute || path.size() >= MAX_PATH) {
			logError("Screenshot path must be absolute and shorter than ", (i32)MAX_PATH, ": ", path);
			return false;
		}
		if (os::fileExists(StaticString<MAX_PATH>(path))) (void)os::deleteFile(StaticString<MAX_PATH>(path));
		m_screenshot_path = StaticString<MAX_PATH>(path);
		m_screenshot_max_width = max_width;
		m_screenshot_max_height = max_height;
		m_screenshot_state = (i32)ScreenshotState::PENDING;
		return true;
	}

	ScreenshotState getScreenshotState() const override { return (ScreenshotState)(i32)m_screenshot_state; }

	void saveScreenshotIfRequested(gpu::TextureHandle texture, IVec2 size) override {
		if (getScreenshotState() != ScreenshotState::PENDING) return;

		if (!texture || size.x <= 0 || size.y <= 0) {
			logError("Could not save screenshot ", m_screenshot_path, ": the game view has no output to read back");
			m_screenshot_state = (i32)ScreenshotState::FAILED;
			return;
		}
		m_screenshot_state = (i32)ScreenshotState::SAVING;
		saveTexturePNG(StringView(m_screenshot_path), texture, size, m_screenshot_max_width, m_screenshot_max_height, &m_screenshot_state);
	}

	void saveTexturePNG(StringView request, gpu::TextureHandle texture, IVec2 size, u32 max_width, u32 max_height, AtomicI32* result) override {
		struct Callback {
			StaticString<MAX_PATH> path;
			IVec2 size;
			u32 max_width;
			u32 max_height;
			IAllocator* allocator;
			AtomicI32* result;

			void callback(Span<const u8> data) {
				const bool saved = save(data);
				if (result) *result = (i32)(saved ? ScreenshotState::SAVED : ScreenshotState::FAILED);
				LUMIX_DELETE(*allocator, this);
			}

			// written to a temporary file first, so the file exists only when it is complete
			bool save(Span<const u8> data) {
				if (data.length() == 0) {
					logError("Failed to read back the screenshot of ", path);
					return false;
				}
				char dir[MAX_PATH];
				copyString(dir, path);
				char* slash = nullptr;
				for (char* c = dir; *c; ++c) {
					if (*c == '/' || *c == '\\') slash = c;
				}
				if (slash) {
					*slash = 0;
					if (!os::makePath(dir)) logError("Failed to create ", dir);
				}

				// averaged down by a whole factor until it fits max_width x max_height (0 = no limit on that side)
				const u8* pixels = data.begin();
				IVec2 out_size = size;
				OutputMemoryStream scaled(*allocator);
				i32 factor = 1;
				if (max_width > 0) factor = maximum(factor, i32((u32(size.x) + max_width - 1) / max_width));
				if (max_height > 0) factor = maximum(factor, i32((u32(size.y) + max_height - 1) / max_height));
				if (factor > 1) {
					out_size = IVec2(size.x / factor, size.y / factor);
					scaled.resize((u64)out_size.x * out_size.y * 4);
					u8* dst = scaled.getMutableData();
					for (i32 y = 0; y < out_size.y; ++y) {
						for (i32 x = 0; x < out_size.x; ++x) {
							for (i32 c = 0; c < 4; ++c) {
								u32 sum = 0;
								for (i32 fy = 0; fy < factor; ++fy) {
									for (i32 fx = 0; fx < factor; ++fx) sum += pixels[((y * factor + fy) * size.x + x * factor + fx) * 4 + c];
								}
								*dst++ = u8(sum / u32(factor * factor));
							}
						}
					}
					pixels = scaled.data();
				}

				const StaticString<MAX_PATH + 4> tmp_path(path, ".tmp");
				os::OutputFile file;
				if (!file.open(tmp_path)) {
					logError("Could not save ", path);
					return false;
				}
				const bool saved = Texture::savePNG(&file, out_size.x, out_size.y, pixels, true, Path(path), *allocator);
				file.close();
				if (saved && !file.isError() && os::fileExists(path)) (void)os::deleteFile(path); // the editor overwrites screenshots
				if (!saved || file.isError() || !os::moveFile(tmp_path, path)) {
					logError("Could not save ", path);
					(void)os::deleteFile(tmp_path);
					return false;
				}
				return true;
			}
		};

		Callback* cb = LUMIX_NEW(m_allocator, Callback);
		cb->allocator = &m_allocator;
		cb->result = result;
		cb->path = request;
		cb->size = size;
		cb->max_width = max_width;
		cb->max_height = max_height;
		getDrawStream().readTexture(texture, makeDelegate<&Callback::callback>(cb));
	}

	void serialize(OutputMemoryStream& stream) const override {}
	bool deserialize(i32 version, InputMemoryStream& stream) override { return version == 0; }

	~RendererImpl() {
		DrawStream& stream = getEndFrameDrawStream();
		for (Renderbuffer& rb : m_renderbuffers) {
			stream.destroy(rb.handle);
			rb.handle = gpu::INVALID_TEXTURE;
		}
		
		for (RenderPlugin* plugin : m_plugins) {
			plugin->shutdown(*this);
		}
		m_plugins.clear();
		
		m_particle_emitter_manager.destroy();
		m_texture_manager.destroy();
		m_model_manager.destroy();
		m_material_manager.destroy();
		m_shader_manager.destroy();
		if (m_cluster_data_buffer) stream.destroy(m_cluster_data_buffer);
		m_cluster_data_buffer = gpu::INVALID_BUFFER;
		m_font_manager->destroy();
		LUMIX_DELETE(m_allocator, m_font_manager);
		
		frame();
		frame();
		frame();
		waitForRender();
		
		m_frame_thread.finished = true;
		m_frame_thread.semaphore.signal();
		m_frame_thread.destroy();
		
		jobs::Counter counter;
		jobs::runLambda([this]() {
			for (const Local<FrameData>& frame : m_frames) {
				gpu::destroy(frame->transient_pool.m_buffer);
				gpu::destroy(frame->uniform_pool.m_buffer);
			}
			gpu::destroy(m_material_buffer.buffer);
			m_profiler.clear();
			gpu::present();
			gpu::present();
			gpu::present();
			gpu::shutdown();
		}, &counter, 1);
		jobs::wait(&counter);
	}
	
	static void add(String& res, const char* a, u32 b) {
		char tmp[32];
		toCString(b, Span(tmp));
		res.append(a, tmp, "\n");
	}

	const char* getSemanticDefines(Span<const AttributeSemantic> attributes, const VertexDecl& decl, u32 vertex_stride) override {
		// the layout of vertices is part of the defines
		u8 key[256];
		u32 key_size = 0;
		// the callers validate what they read from files, a violation here is a bug
		ASSERT(attributes.length() <= 16);
		ASSERT(decl.isValid());
		memcpy(key, attributes.begin(), attributes.length());
		key_size += attributes.length();
		memcpy(key + key_size, &vertex_stride, sizeof(vertex_stride));
		key_size += sizeof(vertex_stride);
		for (u32 i = 0; i < decl.attributes_count; ++i) {
			const VertexAttribute& attr = decl.attributes[i];
			key[key_size++] = attr.byte_offset;
			key[key_size++] = attr.components_count;
			key[key_size++] = (u8)attr.type;
			key[key_size++] = attr.flags;
		}
		const RuntimeHash hash(key, key_size);
		auto iter = m_semantic_defines.find(hash);
		if (!iter.isValid()) {
			String s(m_allocator);
			// the mesh has the attribute
			for (u32 i = 0; i < attributes.length(); ++i) {
				switch (attributes[i]) {
					case AttributeSemantic::COUNT: ASSERT(false); break;
					case AttributeSemantic::NONE: break;
					case AttributeSemantic::POSITION: break;

					case AttributeSemantic::NORMAL: add(s, "#define NORMAL_ATTR ", 1); break;
					case AttributeSemantic::TANGENT: add(s, "#define TANGENT_ATTR ", 1); break;
					case AttributeSemantic::BITANGENT: add(s, "#define BITANGENT_ATTR ", 1); break;
					case AttributeSemantic::COLOR0: add(s, "#define COLOR0_ATTR ", 1); break;
					case AttributeSemantic::COLOR1: add(s, "#define COLOR1_ATTR ", 1); break;
					case AttributeSemantic::JOINTS: add(s, "#define INDICES_ATTR ", 1); break;
					case AttributeSemantic::WEIGHTS: add(s, "#define WEIGHTS_ATTR ", 1); break;
					case AttributeSemantic::TEXCOORD0: add(s, "#define UV0_ATTR ", 1); break;
					case AttributeSemantic::TEXCOORD1: add(s, "#define UV1_ATTR ", 1); break;
					case AttributeSemantic::AO: add(s, "#define AO_ATTR ", 1); break;
				}
			}

			// layout of vertices, see vertex_pulling.hlsli. Attributes that the mesh does not have have 0 components.
			static const char* names[] = { "POSITION", "NORMAL", "TANGENT", "BITANGENT", "COLOR0", "COLOR1", "JOINTS", "WEIGHTS", "TEXCOORD0", "TEXCOORD1", "AO" };
			static_assert(lengthOf(names) == (u32)AttributeSemantic::COUNT);
			add(s, "#define VERTEX_STRIDE ", vertex_stride);
			for (u32 semantic = 0; semantic < (u32)AttributeSemantic::COUNT; ++semantic) {
				u32 offset = 0, type = 0, components = 0, normalized = 0;
				for (u32 i = 0; i < decl.attributes_count && i < attributes.length(); ++i) {
					if ((u32)attributes[i] != semantic) continue;
					const VertexAttribute& attr = decl.attributes[i];
					offset = attr.byte_offset;
					type = (u32)attr.type;
					components = attr.components_count;
					normalized = (attr.flags & VertexAttribute::NORMALIZED) ? 1 : 0;
				}
				StaticString<96> name("#define PULL_", names[semantic]);
				add(s, StaticString<96>(name, "_OFFSET ").data, offset);
				add(s, StaticString<96>(name, "_TYPE ").data, type);
				add(s, StaticString<96>(name, "_COMPONENTS ").data, components);
				add(s, StaticString<96>(name, "_NORMALIZED ").data, normalized);
			}

			iter = m_semantic_defines.insert(hash, static_cast<String&&>(s));
		}
		return iter.value().c_str();
	}

	void initEnd() override {
		m_cubemap_sky.init();
		m_tdao.init();
	}

	void shutdownStarted() override {
	}

	void initBegin() override {
		PROFILE_FUNCTION();
		gpu::InitFlags flags = gpu::InitFlags::NONE;
		
		char cmd_line[4096];
		os::getCommandLine(Span(cmd_line));
		CommandLineParser cmd_line_parser(cmd_line);
		while (cmd_line_parser.next()) {
			if (cmd_line_parser.currentEquals("-debug_gpu")) {
				flags = flags | gpu::InitFlags::DEBUG;
			}
			else if (cmd_line_parser.currentEquals("-gpu_stable_power_state")) {
				flags = flags | gpu::InitFlags::STABLE_POWER_STATE;
			}
			else if (cmd_line_parser.currentEquals("-dred")) {
				flags = flags | gpu::InitFlags::DRED;
			}
			else if (cmd_line_parser.currentEquals("-gpu_validation")) {
				flags = flags | gpu::InitFlags::GPU_VALIDATION;
			}
		}

		jobs::Signal signal;
		jobs::runLambda([this, flags]() {
			PROFILE_BLOCK("init_render");
			os::WindowHandle window_handle = m_engine.getMainWindow();
			if (window_handle == os::INVALID_WINDOW) {
				logError("Trying to initialize renderer without any window");
				os::messageBox("Failed to initialize renderer. More info in log.");
			}
			if (!gpu::init(window_handle, flags)) {
				os::messageBox("Failed to initialize renderer. More info in log.");
			}

			gpu::MemoryStats mem_stats;
			if (gpu::getMemoryStats(mem_stats)) {
				logInfo("Initial GPU memory stats:\n",
					"total: ", (mem_stats.total_available_mem / (1024.f * 1024.f)), "MB\n"
					"currect: ", (mem_stats.current_available_mem / (1024.f * 1024.f)), "MB\n"
					"dedicated: ", (mem_stats.dedicated_vidmem/ (1024.f * 1024.f)), "MB\n");
			}

			for (const Local<FrameData>& frame : m_frames) {
				frame->transient_pool.init();
				frame->uniform_pool.init();
				jobs::turnGreen(&frame->can_setup);
			}
			m_profiler.init();
		}, &m_init_signal, 1);

		m_cpu_frame = m_frames[0].get();
		for (u32 i = 1; i < lengthOf(m_frames); ++i) {
			pushFreeFrame(*m_frames[i].get());
		}

		MaterialBuffer& mb = m_material_buffer;
		const u32 MAX_MATERIAL_CONSTS_COUNT = 10000;
		mb.buffer = gpu::allocBufferHandle();
		mb.map.insert(RuntimeHash(), 0);
		mb.data.resize(MAX_MATERIAL_CONSTS_COUNT);
		mb.data[0].hash = RuntimeHash();
		mb.data[0].ref_count = 1;
		mb.first_free = 1;
		for (int i = 1; i < MAX_MATERIAL_CONSTS_COUNT; ++i) {
			mb.data[i].ref_count = 0;
			mb.data[i].next_free = i + 1;
		}
		mb.data.back().next_free = -1;
			
		DrawStream& stream = m_cpu_frame->draw_stream;
		stream.createBuffer(mb.buffer
			, gpu::BufferFlags::NONE
			, Material::MAX_UNIFORMS_BYTES * MAX_MATERIAL_CONSTS_COUNT
			, nullptr
			, "materials"
		);

		float default_mat[Material::MAX_UNIFORMS_FLOATS] = {};
		stream.update(mb.buffer, &default_mat, sizeof(default_mat));

		ResourceManagerHub& manager = m_engine.getResourceManager();
		m_texture_manager.create(Texture::TYPE, manager);
		m_model_manager.create(Model::TYPE, manager);
		m_material_manager.create(Material::TYPE, manager);
		m_particle_emitter_manager.create(ParticleSystemResource::TYPE, manager);
		m_shader_manager.create(Shader::TYPE, manager);
		m_sprite_manager.create(Sprite::TYPE, manager);
		m_font_manager = LUMIX_NEW(m_allocator, FontManager)(*this, m_allocator);
		m_font_manager->create(FontResource::TYPE, manager);
		m_layers.emplace("default");

#ifdef _WIN32
		if (!initDLSS(*this, m_allocator)) {
			initFSR3(*this, m_allocator);
		}
#endif
	}


	MemRef copy(const void* data, u32 size) override
	{
		MemRef mem = allocate(size);
		memcpy(mem.data, data, size);
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


	MemRef allocate(u32 size) override
	{
		MemRef ret;
		ret.size = size;
		ret.own = true;
		ret.data = m_allocator.allocate(size, 8);
		return ret;
	}

	void beginProfileBlock(const char* name, i64 link, bool stats) override
	{
		gpu::pushDebugGroup(name);
		m_profiler.beginQuery(name, link, stats);
	}


	void endProfileBlock() override
	{
		m_profiler.endQuery();
		gpu::popDebugGroup();
	}

	TransientPool& getTransientPool() override {
		jobs::wait(&m_cpu_frame->can_setup);	
		return m_cpu_frame->transient_pool;
	}

	UniformPool& getUniformPool() override {
		jobs::wait(&m_cpu_frame->can_setup);
		return m_cpu_frame->uniform_pool;
	}	

	gpu::BufferHandle getMaterialUniformBuffer() override {
		return m_material_buffer.buffer;
	}

	RenderBufferHandle createRenderbuffer(const RenderbufferDesc& desc) override {
		for (Renderbuffer& rb : m_renderbuffers) {
			if (!rb.handle) continue;

			if (rb.state == Renderbuffer::ACTIVE) continue;
			if (rb.size != desc.size) continue;
			if (rb.format != desc.format) continue;
			if (rb.flags != desc.flags) continue;

			rb.state = Renderbuffer::ACTIVE;
			#ifdef LUMIX_DEBUG
				rb.debug_name = desc.debug_name;
			#endif
			StaticString<128> name(desc.debug_name, " ", u32(&rb - m_renderbuffers.begin()));
			getDrawStream().setDebugName(rb.handle, name);
			return RenderBufferHandle(u32(&rb - m_renderbuffers.begin()));
		}

		for (Renderbuffer& rb : m_renderbuffers) {
			if (rb.handle) continue;

			rb.handle = createTexture(desc.size.x, desc.size.y, 1, desc.format, desc.flags, Renderer::MemRef(), desc.debug_name);
			rb.state = Renderbuffer::ACTIVE;
			rb.flags = desc.flags;
			rb.format = desc.format;
			rb.size = desc.size;
			#ifdef LUMIX_DEBUG
				rb.debug_name = desc.debug_name;
			#endif
			return RenderBufferHandle(u32(&rb - m_renderbuffers.begin()));
		}

		Renderbuffer& rb = m_renderbuffers.emplace();
		rb.handle = createTexture(desc.size.x, desc.size.y, 1, desc.format, desc.flags, Renderer::MemRef(), desc.debug_name);
		rb.state = Renderbuffer::ACTIVE;
		rb.flags = desc.flags;
		rb.format = desc.format;
		rb.size = desc.size;
		#ifdef LUMIX_DEBUG		
			rb.debug_name = desc.debug_name;
		#endif
		return RenderBufferHandle(m_renderbuffers.size() - 1);
	}

	void releaseRenderbuffer(RenderBufferHandle handle) override {
		if (handle == INVALID_RENDERBUFFER) return;
		m_renderbuffers[u32(handle)].state = Renderbuffer::REUSABLE;
	}

	gpu::TextureHandle toTexture(RenderBufferHandle handle) override {
		if (u32(handle) >= (u32)m_renderbuffers.size()) return gpu::INVALID_TEXTURE;
		return m_renderbuffers[u32(handle)].handle;
	}

	void setRenderTargets(Span<const RenderBufferHandle> renderbuffers, RenderBufferHandle ds = INVALID_RENDERBUFFER, gpu::FramebufferFlags flags = gpu::FramebufferFlags::NONE) override {
		DrawStream& stream = getDrawStream();
		if (ds == INVALID_RENDERBUFFER && renderbuffers.length() == 0) {
			stream.setFramebuffer(nullptr, 0, gpu::INVALID_TEXTURE, gpu::FramebufferFlags::NONE);
			return;
		}

		const IVec2 viewport_size = m_renderbuffers[u32(ds == INVALID_RENDERBUFFER ? renderbuffers[0] : ds)].size;
		gpu::TextureHandle attachments[16];
		ASSERT(renderbuffers.length() <= lengthOf(attachments));
		for (u32 i = 0; i < renderbuffers.length(); ++i) {
			attachments[i] = m_renderbuffers[u32(renderbuffers[i])].handle;
		}
		stream.setFramebuffer(attachments, renderbuffers.length(), ds != INVALID_RENDERBUFFER ? m_renderbuffers[u32(ds)].handle : gpu::INVALID_TEXTURE, flags);
		stream.viewport(0, 0, viewport_size.x, viewport_size.y);
	}

	void clearBuffers() {
		PROFILE_FUNCTION();
		for (Renderbuffer& rb : m_renderbuffers) {
			switch (rb.state) {
				case Renderbuffer::ACTIVE: break;
				case Renderbuffer::REUSABLE: 
					rb.state = Renderbuffer::TO_REMOVE;
					break;
				case Renderbuffer::TO_REMOVE:
					if (rb.handle) {
						getEndFrameDrawStream().destroy(rb.handle);
						rb.handle = gpu::INVALID_TEXTURE;
					}
					break;
			}
		}

		while (!m_renderbuffers.empty()) {
			if (m_renderbuffers.last().handle) break;
			m_renderbuffers.pop();
		}
	}

	MaterialIndex createMaterialInstance(Span<const float> data) override {
		if (m_material_buffer.first_free == -1) {
			ASSERT(false);
			++m_material_buffer.data[0].ref_count;
			return MaterialIndex{0};
		}
		u32 idx = m_material_buffer.first_free;
		m_material_buffer.first_free = m_material_buffer.data[m_material_buffer.first_free].next_free;
		m_material_buffer.data[idx].ref_count = 1;
		m_material_buffer.data[idx].is_instance = true;
		m_material_buffer.data[idx].hash = RuntimeHash(data.begin(), data.length() * sizeof(float));
		
		jobs::wait(&m_cpu_frame->can_setup);
		const u32 size = u32(data.length() * sizeof(float));
		const TransientSlice slice = m_cpu_frame->uniform_pool.alloc(size);
		memcpy(slice.ptr, data.begin(), size);
		m_cpu_frame->draw_stream.copy(m_material_buffer.buffer, slice.buffer, idx * Material::MAX_UNIFORMS_BYTES, slice.offset, size);
		return MaterialIndex{idx};
	}

	// writes data at a byte offset inside the material's constants (the offset of a uniform in its shader)
	void updateMaterialConstants(MaterialIndex handle, Span<const float> data, u32 offset) override {
		jobs::wait(&m_cpu_frame->can_setup);
		const u32 size = u32(data.length() * sizeof(float));
		ASSERT(offset + size <= Material::MAX_UNIFORMS_BYTES);
		const TransientSlice slice = m_cpu_frame->uniform_pool.alloc(size);
		memcpy(slice.ptr, data.begin(), size);
		m_cpu_frame->draw_stream.copy(m_material_buffer.buffer, slice.buffer, u32(handle) * Material::MAX_UNIFORMS_BYTES + offset, slice.offset, size);
	}

	MaterialIndex createMaterialConstants(Span<const float> data) override {
		const RuntimeHash hash(data.begin(), data.length() * sizeof(float));
		auto iter = m_material_buffer.map.find(hash);
		u32 idx;
		if(iter.isValid()) {
			idx = iter.value();
		}
		else {
			if (m_material_buffer.first_free == -1) {
				ASSERT(false);
				++m_material_buffer.data[0].ref_count;
				return MaterialIndex{0};
			}
			idx = m_material_buffer.first_free;
			m_material_buffer.first_free = m_material_buffer.data[m_material_buffer.first_free].next_free;
			m_material_buffer.data[idx].ref_count = 0;
			m_material_buffer.data[idx].is_instance = false;
			m_material_buffer.data[idx].hash = RuntimeHash(data.begin(), data.length() * sizeof(float));
			m_material_buffer.map.insert(hash, idx);
			
			jobs::wait(&m_cpu_frame->can_setup);
			const u32 size = u32(data.length() * sizeof(float));
			const TransientSlice slice = m_cpu_frame->uniform_pool.alloc(size);
			memcpy(slice.ptr, data.begin(), size);
			m_cpu_frame->draw_stream.copy(m_material_buffer.buffer, slice.buffer, idx * Material::MAX_UNIFORMS_BYTES, slice.offset, size);
		}
		++m_material_buffer.data[idx].ref_count;
		return MaterialIndex{idx};
	}

	void destroyMaterialConstants(MaterialIndex handle) override {
		const u32 idx = u32(handle);
		if (idx == 0) return;

		--m_material_buffer.data[idx].ref_count;
		if (m_material_buffer.data[idx].ref_count > 0) return;
			
		const RuntimeHash hash = m_material_buffer.data[idx].hash;
		m_material_buffer.data[idx].next_free = m_material_buffer.first_free;
		m_material_buffer.first_free = idx;
		m_material_buffer.map.erase(hash);
	}

	gpu::BufferHandle createBuffer(const MemRef& memory, gpu::BufferFlags flags, const char* debug_name) override
	{
		gpu::BufferHandle handle = gpu::allocBufferHandle();
		if(!handle) return handle;

		DrawStream& stream = getDrawStream();
		stream.createBuffer(handle, flags, memory.size, memory.data, debug_name);
		if (memory.own) stream.freeMemory(memory.data, m_allocator);
		return handle;
	}

	struct ClusterDataRange {
		u32 offset;
		u32 size;
		u32 frame; // of the free, only for the pending ones
	};

	// returns the range to the free ones, which are sorted by offset and never adjacent
	void releaseClusterData(u32 offset, u32 size) {
		u32 i = 0;
		while (i < (u32)m_cluster_data_free.size() && m_cluster_data_free[i].offset < offset) ++i;
		const bool merge_prev = i > 0 && m_cluster_data_free[i - 1].offset + m_cluster_data_free[i - 1].size == offset;
		const bool merge_next = i < (u32)m_cluster_data_free.size() && offset + size == m_cluster_data_free[i].offset;
		if (merge_prev && merge_next) {
			m_cluster_data_free[i - 1].size += size + m_cluster_data_free[i].size;
			m_cluster_data_free.erase(i);
		}
		else if (merge_prev) {
			m_cluster_data_free[i - 1].size += size;
		}
		else if (merge_next) {
			m_cluster_data_free[i].offset = offset;
			m_cluster_data_free[i].size += size;
		}
		else {
			m_cluster_data_free.insert(i, ClusterDataRange{offset, size, 0});
		}
	}

	bool allocClusterRange(u32 size, u32 alignment, u32& offset, bool reclaim) override {
		if (size == 0 || alignment == 0) return false;
		if (tryAllocClusterRange(size, alignment, offset)) return true;
		if (reclaim && reclaimClusterData(size, alignment) && tryAllocClusterRange(size, alignment, offset)) return true;
		// the streamer (no reclaim) handles it, it unloads pages and tries again later
		if (!reclaim) return false;
		logError("Buffer of data of clusters is full (", (u32)CLUSTER_DATA_POOL_SIZE >> 20, " MB), a range with ", size, " bytes does not fit");
		return false;
	}

	bool tryAllocClusterRange(u32 size, u32 alignment, u32& offset) {
		if (size == 0 || alignment == 0) return false;
		// the ranges are aligned, and their sizes are multiples of 4 as shaders read 32 bit words
		const u32 aligned_size = (size + CLUSTER_DATA_ALIGN - 1) & ~(CLUSTER_DATA_ALIGN - 1);
		jobs::MutexGuard lock(m_cluster_data_mutex);

		if (!m_cluster_data_buffer) {
			m_cluster_data_buffer = createBuffer(MemRef{ CLUSTER_DATA_POOL_SIZE, nullptr, false }, gpu::BufferFlags::SHADER_BUFFER, "cluster data");
			if (!m_cluster_data_buffer) return false;
			m_cluster_data_free.push(ClusterDataRange{0, CLUSTER_DATA_POOL_SIZE, 0});
		}

		// frames in flight may still read what was freed lately
		for (u32 i = 0; i < (u32)m_cluster_data_pending.size();) {
			const ClusterDataRange r = m_cluster_data_pending[i];
			if (m_frame_number - r.frame >= CLUSTER_DATA_FREE_DELAY) {
				m_cluster_data_pending.swapAndPop(i);
				releaseClusterData(r.offset, r.size);
			}
			else {
				++i;
			}
		}

		// the smallest free range that fits at an aligned offset
		u32 best = 0xffFFffFF;
		u64 best_start = 0;
		for (u32 i = 0; i < (u32)m_cluster_data_free.size(); ++i) {
			const ClusterDataRange& r = m_cluster_data_free[i];
			const u64 start = (u64(r.offset) + alignment - 1) / alignment * alignment;
			if (start + aligned_size > u64(r.offset) + r.size) continue;
			if (best == 0xffFFffFF || r.size < m_cluster_data_free[best].size) {
				best = i;
				best_start = start;
			}
		}
		if (best == 0xffFFffFF) {
			// the caller logs it
			return false;
		}
		offset = (u32)best_start;
		const ClusterDataRange taken = m_cluster_data_free[best];
		m_cluster_data_free.erase(best);
		// what is left of the free range, before and after the allocated one
		const u32 after = u32(u64(taken.offset) + taken.size - (best_start + aligned_size));
		const u32 before = offset - taken.offset;
		if (after) m_cluster_data_free.insert(best, ClusterDataRange{u32(best_start + aligned_size), after, 0});
		if (before) m_cluster_data_free.insert(best, ClusterDataRange{taken.offset, before, 0});
		return true;
	}

	bool allocClusterData(const void* data, u32 size, u32& offset) override {
		if (!allocClusterRange(size, CLUSTER_DATA_ALIGN, offset, true)) return false;
		const u32 aligned_size = (size + CLUSTER_DATA_ALIGN - 1) & ~(CLUSTER_DATA_ALIGN - 1);

		MemRef staging = allocate(aligned_size);
		memcpy(staging.data, data, size);
		memset((u8*)staging.data + size, 0, aligned_size - size);
		const gpu::BufferHandle tmp = createBuffer(staging, gpu::BufferFlags::IMMUTABLE, "cluster data staging");
		DrawStream& stream = getDrawStream();
		stream.copy(m_cluster_data_buffer, tmp, offset, 0, aligned_size);
		stream.destroy(tmp);
		return true;
	}

	void freeClusterData(u32 offset, u32 size) override {
		if (size == 0) return;
		const u32 aligned_size = (size + CLUSTER_DATA_ALIGN - 1) & ~(CLUSTER_DATA_ALIGN - 1);
		jobs::MutexGuard lock(m_cluster_data_mutex);
		m_cluster_data_pending.push(ClusterDataRange{offset, aligned_size, m_frame_number});
	}

	gpu::BufferHandle getClusterDataBuffer() override { return m_cluster_data_buffer; }

	void updateClusterData(u32 offset, const void* data, u32 size) override {
		ASSERT(size % 4 == 0 && m_cluster_data_buffer);
		// a slice of the transient buffer of the frame, a buffer for each update (there are many of them when pages are streamed) costs more than the copy
		const TransientSlice slice = alloc(getTransientPool(), size);
		memcpy(slice.ptr, data, size);
		DrawStream& stream = getDrawStream();
		stream.barrier(slice.buffer, gpu::BarrierType::READ);
		stream.copy(m_cluster_data_buffer, slice.buffer, offset, slice.offset, size);
	}

	// ---- streaming of clusters, see ClusterStream
	u32 allocClusterStreamIds(u32 count) override {
		jobs::MutexGuard lock(m_stream_mutex);
		for (u32 i = 0; i < (u32)m_stream_id_free.size(); ++i) {
			StreamIds& r = m_stream_id_free[i];
			if (r.count < count || m_frame_number - r.frame < STREAM_ID_REUSE_DELAY) continue;
			const u32 first = r.first;
			r.first += count;
			r.count -= count;
			if (r.count == 0) m_stream_id_free.erase(i);
			return first;
		}
		if (m_stream_id_end + count > STREAM_MAX_GROUPS) {
			static bool reported = false;
			if (!reported) logError("Out of ids of groups of clusters, meshes that are loaded later are streamed without feedback");
			reported = true;
			return ClusterStream::NONE;
		}
		const u32 first = m_stream_id_end;
		m_stream_id_end += count;
		return first;
	}

	void registerClusterStream(ClusterStream* stream) override {
		jobs::MutexGuard lock(m_stream_mutex);
		m_streams.push(stream);
	}

	void unregisterClusterStream(ClusterStream* stream) override {
		jobs::MutexGuard lock(m_stream_mutex);
		const i32 idx = m_streams.indexOf(stream);
		if (idx >= 0) m_streams.swapAndPop(idx);
		for (u32 i = (u32)m_stream_wanted.size(); i > 0;) {
			--i;
			if (m_stream_wanted[i].stream == stream) m_stream_wanted.swapAndPop(i);
		}
		if (stream->global_base != ClusterStream::NONE) {
			// the ids are reused later, requests of the GPU and the readbacks in flight still have them
			m_stream_id_free.push(StreamIds{stream->global_base, (u32)stream->pages.size(), m_frame_number});
			stream->global_base = ClusterStream::NONE;
		}
	}

	bool isClusterStreamingEnabled() const override { return m_stream_enabled; }
	gpu::BufferHandle getClusterFeedbackBuffer() override { return m_feedback_buffer; }
	u32 getClusterFeedbackCapacity() override { return m_feedback_buffer ? (u32)STREAM_MAX_GROUPS : 0; }

	// the render thread calls it when the GPU is done with the copy of the feedback buffer
	void onClusterFeedback(Span<const u8> data) {
		MutexGuard guard(m_feedback_mutex);
		m_feedback_incoming.resize((u32)data.length());
		if (data.length() > 0) memcpy(m_feedback_incoming.begin(), data.begin(), data.length());
	}

	// Loads the page and the pages of its parents. False if something did not fit or there were too many loads in this frame.
	bool ensureStreamPage(ClusterStream& stream, u32 page_index, u32 stamp, u32 depth) {
		ClusterStream::Page& page = stream.pages[page_index];
		if (page.loaded) return true;
		page.wanted_frame = stamp;
		if (depth > 64 || m_stream_loads >= STREAM_LOADS_PER_FRAME) return false;
		for (;;) {
			const u32 missing = stream.firstMissingParentPage(page_index);
			if (missing == ClusterStream::NONE) break;
			if (!ensureStreamPage(stream, missing, stamp, depth + 1)) return false;
		}
		// the parents that are loaded already are not visited above, they must not be unloaded to make room for the page
		stream.protectParentPages(page_index, stamp);
		if (!makeStreamRoom(stream.pageBytes(page_index), stamp)) return false;
		// no free range of the size (the buffer is fragmented): unload some pages, the ranges that they free are usable in a few frames
		for (u32 attempt = 0; !stream.loadPage(*this, page_index, stamp, false); ++attempt) {
			if (attempt >= 2 || !evictLeastRecentlyUsedPage(stamp)) return false;
		}
		++m_stream_loads;
		return true;
	}

	u32 getLargestClusterFreeRange() {
		jobs::MutexGuard lock(m_cluster_data_mutex);
		u32 largest = 0;
		for (const ClusterDataRange& r : m_cluster_data_free) largest = maximum(largest, r.size);
		return largest;
	}

	// An allocation does not fit: unloads streamed pages, least recently used first, until the free and pending bytes could fit it. The ranges they free wait for the frames
	// in flight like any other (they are never reused early, the frames that are in flight or recorded now may still read them), so the allocation usually fails now and succeeds
	// when it is tried again a few frames later. False if there is no free range of the size now. Called with no lock held, by the thread that allocates.
	bool reclaimClusterData(u32 size, u32 alignment) {
		jobs::MutexGuard lock(m_stream_mutex);
		const u32 needed = ((size + CLUSTER_DATA_ALIGN - 1) & ~(CLUSTER_DATA_ALIGN - 1)) + alignment;
		if (getLargestClusterFreeRange() >= needed) return true;
		if (!m_stream_enabled) return false;
		const u32 stamp = m_frame_number + 1;
		for (u32 i = 0; i < 4096 && getClusterDataFreeBytes() < needed; ++i) {
			if (!evictLeastRecentlyUsedPage(stamp)) break;
		}
		return getLargestClusterFreeRange() >= size;
	}

	// Unloads the page that was used the longest time ago and can be unloaded. Pages that are needed in this frame and the ones that were loaded lately stay. False if there is none.
	bool evictLeastRecentlyUsedPage(u32 stamp) {
		ClusterStream* best_stream = nullptr;
		u32 best_page = 0, best_stamp = 0;
		for (ClusterStream* s : m_streams) {
			for (u32 p = 0; p < (u32)s->pages.size(); ++p) {
				const ClusterStream::Page& page = s->pages[p];
				if (!page.loaded || page.permanent) continue;
				if (page.wanted_frame == stamp || page.loaded_frame + STREAM_MIN_RESIDENCY > stamp) continue;
				const u32 used_at = maximum(page.last_used, page.loaded_frame);
				if (best_stream && used_at >= best_stamp) continue;
				if (!s->canEvict(p)) continue;
				best_stream = s;
				best_page = p;
				best_stamp = used_at;
			}
		}
		if (!best_stream) return false;
		best_stream->evictPage(*this, best_page);
		++m_stream_evictions;
		return true;
	}

	// Unloads the least recently used pages until `bytes` fit in the budget.
	bool makeStreamRoom(u32 bytes, u32 stamp) {
		for (;;) {
			u64 used = 0;
			for (const ClusterStream* s : m_streams) used += s->streamed_bytes;
			if (used + bytes <= m_stream_budget) return true;
			if (!evictLeastRecentlyUsedPage(stamp)) return false;
		}
	}

	// bytes of the buffer of cluster data that are free or will be soon (the ranges freed in the last frames)
	u64 getClusterDataFreeBytes() {
		jobs::MutexGuard lock(m_cluster_data_mutex);
		u64 free = 0;
		for (const ClusterDataRange& r : m_cluster_data_free) free += r.size;
		for (const ClusterDataRange& r : m_cluster_data_pending) free += r.size;
		return free;
	}

	// bytes of the ranges that are freed but wait for the frames in flight
	u64 getClusterPendingBytes() {
		jobs::MutexGuard lock(m_cluster_data_mutex);
		u64 pending = 0;
		for (const ClusterDataRange& r : m_cluster_data_pending) pending += r.size;
		return pending;
	}

	// the lists that the GPU appended to some frames ago (one frame of it, see FEEDBACK_*): which pages were used, which are needed
	void processClusterFeedback(u32 stamp) {
		if (m_feedback_local.size() < FEEDBACK_BITS) return;
		static bool first_feedback = true;
		const u32* feedback = (const u32*)m_feedback_local.begin();
		// the counter can be larger than the list, the GPU drops what does not fit
		const u32 request_count = minimum(feedback[0], (u32)FEEDBACK_REQUEST_CAPACITY);
		if (first_feedback) logInfo("Cluster streaming: first feedback of the GPU, ", request_count, " requests");
		first_feedback = false;
		m_stream_feedback_dropped = feedback[0] > FEEDBACK_REQUEST_CAPACITY ? feedback[0] - FEEDBACK_REQUEST_CAPACITY : 0;
		m_clusters_selected = feedback[2];
		m_clusters_selected_dropped = feedback[3];
		m_clusters_triangles = feedback[4];
		m_stream_feedback_requests = request_count;

		// ids to streams: the streams with ids sorted by the first one
		m_stream_by_id.clear();
		for (ClusterStream* s : m_streams) {
			if (s->global_base != ClusterStream::NONE) m_stream_by_id.push(s);
		}
		sort(m_stream_by_id.begin(), m_stream_by_id.end(), [](const ClusterStream* a, const ClusterStream* b) { return a->global_base < b->global_base; });
		auto find_page = [&](u32 id, ClusterStream*& stream) -> u32 {
			u32 lo = 0, hi = (u32)m_stream_by_id.size();	// the first stream that starts after the id
			while (lo < hi) {
				const u32 mid = (lo + hi) / 2;
				if (m_stream_by_id[mid]->global_base <= id) lo = mid + 1;
				else hi = mid;
			}
			if (lo == 0) return ClusterStream::NONE;
			stream = m_stream_by_id[lo - 1];
			const u32 page = id - stream->global_base;
			return page < (u32)stream->pages.size() ? page : ClusterStream::NONE;
		};

		// the bits of the used groups, the ids that were read back are the ones that existed when the readback was recorded
		const u32* bits = feedback + FEEDBACK_BITS / 4;
		const u32 bit_ids = ((u32)m_feedback_local.size() - FEEDBACK_BITS) / 4 * 32;
		u32 used_count = 0;
		for (ClusterStream* s : m_stream_by_id) {
			for (u32 page = 0; page < (u32)s->pages.size(); ++page) {
				const u32 id = s->global_base + page;
				if (id >= bit_ids) break;
				if ((bits[id >> 5] & (1u << (id & 31))) == 0) continue;
				ClusterStream::Page& p = s->pages[page];
				p.last_used = maximum(p.last_used, stamp);
				++used_count;
			}
		}
		m_stream_feedback_used = used_count;

		m_stream_wanted.clear();
		const u32* requests = feedback + FEEDBACK_REQUESTS / 4;
		for (u32 i = 0; i < request_count; ++i) {
			ClusterStream* s = nullptr;
			const u32 page_index = find_page(requests[i * 2], s);
			if (page_index == ClusterStream::NONE) continue;
			ClusterStream::Page& page = s->pages[page_index];
			if (page.loaded || page.wanted_frame == stamp) continue;
			float priority;
			memcpy(&priority, &requests[i * 2 + 1], sizeof(priority));
			page.wanted_frame = stamp;
			m_stream_wanted.push(StreamRef{s, page_index, priority});
		}
		// the most needed first, the loads of a frame are limited
		sort(m_stream_wanted.begin(), m_stream_wanted.end(), [](const StreamRef& a, const StreamRef& b) { return a.priority > b.priority; });
		m_stream_loads = 0;
		for (const StreamRef& w : m_stream_wanted) {
			if (m_stream_loads >= STREAM_LOADS_PER_FRAME) break;
			ensureStreamPage(*w.stream, w.page, stamp, 0);
		}
	}

	void updateClusterStreaming() {
		PROFILE_FUNCTION();
		if (!m_stream_enabled) return;
		jobs::MutexGuard lock(m_stream_mutex);
		if (m_streams.empty()) return;
		const u32 stamp = m_frame_number + 1;
		m_stream_loads = 0;
		m_stream_evictions = 0;

		if (!m_feedback_buffer) {
			// zeroed: no entries in the lists, a group that was never drawn has stamp 0
			const u32 size = FEEDBACK_BUFFER_BYTES;
			MemRef mem = allocate(size);
			memset(mem.data, 0, size);
			m_feedback_buffer = createBuffer(mem, gpu::BufferFlags::SHADER_BUFFER, "cluster feedback");
		}

		{
			MutexGuard guard(m_feedback_mutex);
			m_feedback_local.resize(m_feedback_incoming.size());
			if (!m_feedback_incoming.empty()) memcpy(m_feedback_local.begin(), m_feedback_incoming.begin(), m_feedback_incoming.size());
			m_feedback_incoming.clear();
		}
		if (!m_feedback_local.empty()) {
			processClusterFeedback(stamp);
			m_feedback_stamp = stamp;
		}
		m_feedback_local.clear();

		// the models that are loaded need room for their tables and permanent pages, the streamed pages must not take all of it
		if (m_cluster_data_buffer) {
			for (u32 i = 0; i < STREAM_LOADS_PER_FRAME && getClusterDataFreeBytes() < STREAM_HEADROOM; ++i) {
				if (!evictLeastRecentlyUsedPage(stamp)) break;
			}
		}

		if (m_stream_load_all) {
			// the parents of a group have higher numbers, so the pages are loaded from the last one
			u32 left = 64;
			for (ClusterStream* s : m_streams) {
				for (u32 p = (u32)s->pages.size(); p > 0 && left > 0;) {
					--p;
					if (s->pages[p].loaded || s->firstMissingParentPage(p) != ClusterStream::NONE) continue;
					if (!s->loadPage(*this, p, stamp, false)) break;
					++m_stream_loads;
					--left;
				}
			}
		}

		// what the profiler shows
		{
			static const u32 streamed_counter = profiler::createCounter("Cluster streamed (MB)", 0);
			static const u32 pages_counter = profiler::createCounter("Cluster pages streamed in", 0);
			static const u32 loads_counter = profiler::createCounter("Cluster page loads", 0);
			static const u32 evictions_counter = profiler::createCounter("Cluster page evictions", 0);
			static const u32 wanted_counter = profiler::createCounter("Cluster pages wanted", 0);
			static const u32 backlog_counter = profiler::createCounter("Cluster pages wanted, not loaded", 0);
			static const u32 limited_counter = profiler::createCounter("Cluster loads limit reached", 0);
			static const u32 resident_counter = profiler::createCounter("Cluster resident (MB)", 0);
			static const u32 free_counter = profiler::createCounter("Cluster pool free (MB)", 0);
			static const u32 largest_counter = profiler::createCounter("Cluster pool largest range (MB)", 0);
			static const u32 pending_counter = profiler::createCounter("Cluster pool pending free (MB)", 0);
			static const u32 ids_counter = profiler::createCounter("Cluster feedback page ids", 0);
			static const u32 requests_counter = profiler::createCounter("Cluster feedback requests", 0);
			static const u32 used_counter = profiler::createCounter("Cluster feedback used pages", 0);
			static const u32 dropped_counter = profiler::createCounter("Cluster feedback dropped entries", 0);
			static const u32 root_bytes_counter = profiler::createCounter("Cluster roots (MB)", 0);
			static const u32 selected_counter = profiler::createCounter("Clusters selected (all views)", 0);
			static const u32 triangles_counter = profiler::createCounter("Cluster triangles (all views)", 0);
			static const u32 selected_dropped_counter =profiler::createCounter("Clusters selected, over capacity", 0);
			static const u32 feedback_age_counter = profiler::createCounter("Cluster feedback age (frames)", 0);
			u64 bytes = 0;
			u64 resident = 0;	// what is in the buffer as long as the meshes are loaded: the tables and the permanent pages
			u32 loaded_pages = 0;
			u64 root_bytes = 0;
			for (const ClusterStream* s : m_streams) {
				bytes += s->streamed_bytes;
				root_bytes += s->root_bytes;
				resident += s->table_bytes;
				for (const ClusterStream::Page& page : s->pages) {
					if (page.loaded && !page.permanent) ++loaded_pages;
					if (page.loaded && page.permanent) resident += page.pool_size;
				}
			}
			u32 backlog = 0;	// requested by the GPU and still not loaded after this frame's loads
			for (const StreamRef& w : m_stream_wanted) {
				if (!w.stream->pages[w.page].loaded) ++backlog;
			}
			constexpr float MB = 1024.f * 1024.f;
			u64 free_bytes = 0;
			{
				jobs::MutexGuard cluster_lock(m_cluster_data_mutex);
				for (const ClusterDataRange& r : m_cluster_data_free) free_bytes += r.size;
			}
			profiler::pushCounter(root_bytes_counter, float(double(root_bytes) / MB));
			profiler::pushCounter(backlog_counter, (float)backlog);
			profiler::pushCounter(limited_counter, m_stream_loads >= STREAM_LOADS_PER_FRAME ? 1.f : 0.f);
			profiler::pushCounter(resident_counter, float(double(resident) / MB));
			profiler::pushCounter(free_counter, float(double(free_bytes) / MB));
			profiler::pushCounter(largest_counter, float(getLargestClusterFreeRange() / MB));
			profiler::pushCounter(pending_counter, float(double(getClusterPendingBytes()) / MB));
			profiler::pushCounter(ids_counter, (float)m_stream_id_end);
			profiler::pushCounter(requests_counter, (float)m_stream_feedback_requests);
			profiler::pushCounter(used_counter, (float)m_stream_feedback_used);
			profiler::pushCounter(dropped_counter, (float)m_stream_feedback_dropped);
			profiler::pushCounter(selected_counter, (float)m_clusters_selected);			profiler::pushCounter(triangles_counter, (float)m_clusters_triangles);
			profiler::pushCounter(selected_dropped_counter,(float)m_clusters_selected_dropped);
			profiler::pushCounter(feedback_age_counter, (float)(stamp - m_feedback_stamp));
			profiler::pushCounter(streamed_counter, float(double(bytes) / (1024.0 * 1024.0)));
			profiler::pushCounter(pages_counter, (float)loaded_pages);
			profiler::pushCounter(loads_counter, (float)m_stream_loads);
			profiler::pushCounter(evictions_counter, (float)m_stream_evictions);
			profiler::pushCounter(wanted_counter, (float)m_stream_wanted.size());
		}

		if (m_stream_id_end > 0) {
			// what the GPU appended until now (the stream is executed in order), then the counters start from 0 for the culling of the next frame
			DrawStream& stream = getDrawStream();
			const u32 bits_bytes = ((m_stream_id_end + 31) / 32) * 4;	// the bits of the ids that exist
			stream.readBuffer(m_feedback_buffer, 0, FEEDBACK_BITS + bits_bytes, makeDelegate<&RendererImpl::onClusterFeedback>(this));
			static const u32 zero_counters[FEEDBACK_REQUESTS / 4] = {};
			stream.update(m_feedback_buffer, zero_counters, sizeof(zero_counters));
			const TransientSlice zeros = alloc(getTransientPool(), bits_bytes);
			memset(zeros.ptr, 0, bits_bytes);
			stream.barrier(zeros.buffer, gpu::BarrierType::READ);
			stream.copy(m_feedback_buffer, zeros.buffer, FEEDBACK_BITS, zeros.offset, bits_bytes);
		}
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
		for(u8 i = 0; i < m_layers.size(); ++i) {
			if(m_layers[i] == name) return i;
		}
		ASSERT(m_layers.size() < 0xff);
		m_layers.emplace(name);
		return m_layers.size() - 1;
	}

	void enableBuiltinTAA(bool enable) override {
		m_builtin_taa_enabled = enable;
	}

	bool isBuiltinTAAEnabled() const override { return m_builtin_taa_enabled; }

	u32 getMaterialReloadCounter() const override { return m_material_reload_counter; }
	void notifyMaterialReload() override { ++m_material_reload_counter; }

	gpu::TextureHandle createTexture(u32 w, u32 h, u32 depth, gpu::TextureFormat format, gpu::TextureFlags flags, const MemRef& memory, const char* debug_name) override
	{
		gpu::TextureHandle handle = gpu::allocTextureHandle();
		if(!handle) return handle;

		DrawStream& stream = getDrawStream();
		stream.createTexture(handle, w, h, depth, format, flags, debug_name);
		if (memory.data && memory.size) {
			ASSERT(depth == 1);
			stream.update(handle, 0, 0, 0, 0, w, h, format, memory.data, memory.size);
			ASSERT(isFlagSet(flags, gpu::TextureFlags::NO_MIPS));
		}
		if (memory.own) stream.freeMemory(memory.data, m_allocator);
		return handle;
	}


	void setupJob(void* user_ptr, void(*task)(void*)) override {
		jobs::run(user_ptr, task, &m_cpu_frame->setup_done);
	}

	void addPlugin(RenderPlugin& plugin) override {
		m_plugins.push(&plugin);
	}

	void removePlugin(RenderPlugin& plugin) override {
		m_plugins.eraseItem(&plugin);
	}

	Span<RenderPlugin*> getPlugins() override { return m_plugins; }
	EvoxRender& getRenderScript() override { return m_render_script; }


	ResourceManager& getTextureManager() override { return m_texture_manager; }
	FontManager& getFontManager() override { return *m_font_manager; }

	void createModules(World& world) override
	{
		UniquePtr<RenderModule> module = RenderModule::createInstance(*this, m_engine, world, m_allocator);
		world.addModule(module.move());
	}

	DrawStream& getDrawStream() override {
		wait(&m_cpu_frame->can_setup);
		return m_cpu_frame->draw_stream;
	}

	DrawStream& getEndFrameDrawStream() override {
		wait(&m_cpu_frame->can_setup);
		return m_cpu_frame->end_frame_draw_stream;
	}

	const char* getName() const override { return "renderer"; }
	Engine& getEngine() override { return m_engine; }
	int getShaderDefinesCount() const override { return m_shader_defines.size(); }
	const char* getShaderDefine(int define_idx) const override { return m_shader_defines[define_idx]; }

	gpu::ProgramHandle queueShaderCompile(Shader& shader, const ShaderKey& key) override {
		ASSERT(shader.isReady());
		jobs::MutexGuard lock(m_cpu_frame->shader_mutex);
		
		for (const auto& i : m_cpu_frame->to_compile_shaders) {
			if (i.content_hash == shader.m_content_hash && key == i.key) {
				return i.program;
			}
		}
		gpu::ProgramHandle program = gpu::allocProgramHandle();
		shader.compile(program, key, m_cpu_frame->begin_frame_draw_stream);
		m_cpu_frame->to_compile_shaders.push({&shader, shader.m_content_hash, program, key});
		return program;
	}


	u8 getShaderDefineIdx(const char* define) override {
		// Fast path: check existing entries without locking
		i32 count = (i32)m_num_shader_defines;
		for (i32 i = 0; i < count; ++i) {
			if (m_shader_defines[i] == define) return u8(i);
		}

		// Not found, take lock and insert if still missing
		jobs::MutexGuard lock(m_shader_defines_mutex);
		for (i32 i = 0; i < (i32)m_shader_defines.size(); ++i) {
			if (m_shader_defines[i] == define) return u8(i);
		}

		if (m_shader_defines.size() >= MAX_SHADER_DEFINES) {
			ASSERT(false);
			logError("Too many shader defines.");
		}

		m_shader_defines.emplace(define);
		ASSERT(m_shader_defines.size() <= 32); // reserved in constructor
		m_num_shader_defines = (i32)m_shader_defines.size();
		return u8(m_shader_defines.size() - 1);
	}

	void render() {
		PROFILE_BLOCK("render submit");
		jobs::MutexGuard guard(m_render_mutex);

		FrameData& frame = popGPUQueue();
		profiler::pushInt("Frame", frame.frame_number);
		frame.transient_pool.prepareToRender();
		frame.uniform_pool.prepareToRender();
		
		gpu::MemoryStats mem_stats;
		if (gpu::getMemoryStats(mem_stats)) {
			//static u32 total_counter = profiler::createCounter("Total GPU memory (MB)", 0);
			static u32 available_counter = profiler::createCounter("Available GPU memory (MB)", 0);
			//static u32 dedicated_counter = profiler::createCounter("Dedicate Vid memory (MB)", 0);
			static u32 buffer_counter = profiler::createCounter("Buffer memory (MB)", 0);
			static u32 texture_counter = profiler::createCounter("Texture memory (MB)", 0);
			static u32 rt_counter = profiler::createCounter("Render target memory (MB)", 0);
			auto to_MB = [](u64 B){
				return float(double(B) / (1024.0 * 1024.0));
			};
			//profiler::pushCounter(total_counter, to_MB(mem_stats.total_available_mem));
			profiler::pushCounter(available_counter, to_MB(mem_stats.current_available_mem));
			//profiler::pushCounter(dedicated_counter, to_MB(mem_stats.dedicated_vidmem));
			profiler::pushCounter(buffer_counter, to_MB(mem_stats.buffer_mem));
			profiler::pushCounter(texture_counter, to_MB(mem_stats.texture_mem));
			profiler::pushCounter(rt_counter, to_MB(mem_stats.render_target_mem));
		}

		m_profiler.beginQuery("frame", 0, false);
		frame.begin_frame_draw_stream.run();
		frame.begin_frame_draw_stream.reset();

		{
			PROFILE_BLOCK("draw stream");
			frame.draw_stream.run();
			profiler::pushInt("Drawcalls", frame.draw_stream.num_drawcalls);
			
			static u32 counter_time = profiler::createCounter("GPU upload (ms/frame)", 0);
			static u32 counter_size = profiler::createCounter("GPU upload (MB/frame)", 0);
			const float upload_duration = 1000.f *  float(frame.draw_stream.upload_duration / double(profiler::frequency()));
			profiler::pushCounter(counter_time, upload_duration);
			profiler::pushCounter(counter_size, frame.draw_stream.upload_size / (1024.f * 1024.f));
			frame.draw_stream.reset();
		}

		frame.end_frame_draw_stream.run();
		frame.end_frame_draw_stream.reset();

		frame.arena_allocator.reset();
		m_profiler.endQuery();

		frame.gpu_frame = gpu::present();
		m_profiler.frame();

		if (gpu::frameFinished(frame.gpu_frame)) {
			frame.gpu_frame = 0xFFffFFff;
			frame.transient_pool.renderDone();
			frame.uniform_pool.renderDone();
			jobs::turnGreen(&frame.can_setup);
			pushFreeFrame(frame);
		}
		else {
			m_frame_thread.frames.push(&frame);
			m_frame_thread.semaphore.signal();
		}
	}

	ArenaAllocator& getCurrentFrameAllocator() override { return m_cpu_frame->arena_allocator; }

	void waitForCommandSetup() override
	{
		jobs::wait(&m_cpu_frame->setup_done);
	}

	void waitCanSetup() override
	{
		jobs::wait(&m_cpu_frame->can_setup);
	}

	void waitForRender() override {
		jobs::wait(&m_last_render);
	}

	i32 getFrameIndex(FrameData* frame) const {
		for (i32 i = 0; i < (i32)lengthOf(m_frames); ++i) {
			if (frame == m_frames[i].get()) return i;
		}
		ASSERT(false);
		return -1;
	}

	u32 frameNumber() const override { return m_cpu_frame->frame_number; }

	void pushFreeFrame(FrameData& frame) {
		jobs::MutexGuard guard(m_frames_mutex);
		m_free_frames.push(&frame);
		jobs::turnGreen(&m_has_free_frames);
	}

	FrameData* popFreeFrame() {
		jobs::MutexGuard guard(m_frames_mutex);
		if (m_free_frames.empty()) {
			jobs::exit(&m_frames_mutex);
			jobs::wait(&m_has_free_frames);
			jobs::enter(&m_frames_mutex);
		}
		FrameData* frame = m_free_frames.back();
		m_free_frames.pop();
		if (m_free_frames.empty()) jobs::turnRed(&m_has_free_frames);
		return frame;
	}

	FrameData& popGPUQueue() {
		jobs::MutexGuard guard(m_frames_mutex);
		FrameData* f = m_gpu_queue;
		m_gpu_queue = nullptr;
		jobs::turnGreen(&m_gpu_queue_empty);
		ASSERT(f);
		return *f;
	}

	void pushToGPUQueue(FrameData& frame) {
		jobs::MutexGuard guard(m_frames_mutex);
		if (m_gpu_queue) {
			jobs::exit(&m_frames_mutex);
			jobs::wait(&m_gpu_queue_empty);
			jobs::enter(&m_frames_mutex);
			ASSERT(!m_gpu_queue);
		}
		m_gpu_queue = &frame;
		jobs::turnRed(&m_gpu_queue_empty);
		jobs::runLambda([this](){
			render();
		}, &m_last_render, 1);
	}

	void frame() override
	{
		PROFILE_FUNCTION();
		
		jobs::wait(&m_cpu_frame->setup_done);
		clearBuffers();

		m_cpu_frame->draw_stream.useProgram(gpu::INVALID_PROGRAM);
		m_cpu_frame->draw_stream.bindIndexBuffer(gpu::INVALID_BUFFER);
		for (u32 i = 0; i < (u32)UniformBuffer::COUNT; ++i) {
			m_cpu_frame->draw_stream.bindUniformBuffer(i, gpu::INVALID_BUFFER, 0, 0);
		}

		for (const auto& i : m_cpu_frame->to_compile_shaders) {
			i.shader->m_programs.push({i.key, i.program});
		}
		m_cpu_frame->to_compile_shaders.clear();

		jobs::turnRed(&m_cpu_frame->can_setup);
		pushToGPUQueue(*m_cpu_frame);

		m_cpu_frame = popFreeFrame();
		++m_frame_number;
		profiler::pushInt("Frame", m_cpu_frame->frame_number);
		m_cpu_frame->frame_number = m_frame_number;
		profiler::pushInt("Reused as", m_cpu_frame->frame_number);

		for (RenderPlugin* plugin : m_plugins) {
			plugin->frame(*this);
		}

		updateClusterStreaming();

	}

	// wait till gpu is done with a frame and reuse it
	struct FrameThread : Thread {
		FrameThread(RendererImpl& renderer)
			: Thread(renderer.m_allocator)
			, renderer(renderer)
			, semaphore(0, 3)
			, frames(renderer.m_allocator)
		{}

		int task() {
			for (;;) {
				semaphore.wait();
				FrameData* f = [&]() -> FrameData* {
					MutexGuard guard(mutex);
					if (frames.empty()) return nullptr;
					FrameData* res = frames[0];
					frames.erase(0);
					return res;
				}();

				if (!f) {
					ASSERT(finished);
					break;
				}

				{
					static int i = 0;
					++i;
					if (i == 10) {
						i = 0;
						gpu::pushGPUCounters();
					}
				}

				// wait until gpu is done with the frame, so we are sure it's not accessing frame's buffers anymore
				gpu::waitFrame(f->gpu_frame);
				
				PROFILE_BLOCK("frame finished");
				profiler::pushInt("Frame", f->frame_number);
				
				// If overflowed buffers exist, we must reuse the frame in the render thread
				// because TransientBuffer::renderDone calls gpu::destroy
				const bool can_run_on_any_worker = !f->transient_pool.m_overflow.buffer && !f->uniform_pool.m_overflow.buffer;

				// running this on render thread might wait till other jobs are done on render thread, causing delay
				// therefore we try to run on any worker if we can
				jobs::runLambda([f]() {
					PROFILE_BLOCK("reuse frame");
					profiler::pushInt("Frame", f->frame_number);
					f->gpu_frame = 0xFFffFFff;
					f->transient_pool.renderDone();
					f->uniform_pool.renderDone();
					jobs::turnGreen(&f->can_setup);
					f->renderer.pushFreeFrame(*f);
				}, nullptr, can_run_on_any_worker ? jobs::ANY_WORKER : 1);
			}
			return 0;
		}

		void push(FrameData* frame) {
			MutexGuard guard(mutex);
			frames.push(frame);
		}

		RendererImpl& renderer;
		Semaphore semaphore;
		Array<FrameData*> frames;
		Mutex mutex;
		volatile bool finished = false;
	};

	Engine& m_engine;
	TagAllocator m_allocator;
	Array<StaticString<32>> m_shader_defines;
	AtomicI32 m_num_shader_defines = 0; // used for fast path without locking
	jobs::Mutex m_render_mutex;
	jobs::Mutex m_shader_defines_mutex;
	Array<StaticString<32>> m_layers;
	FontManager* m_font_manager;
	RenderResourceManager<Model> m_model_manager;
	RenderResourceManager<ParticleSystemResource> m_particle_emitter_manager;
	RenderResourceManager<Shader> m_shader_manager;
	SpriteResourceManager m_sprite_manager;
	RenderResourceManager<Texture> m_texture_manager;
	RenderResourceManager<Material> m_material_manager;
	u32 m_frame_number = 0;
	enum : u32 {
		CLUSTER_DATA_POOL_SIZE = 512 * 1024 * 1024,
		CLUSTER_DATA_ALIGN = 16,
		CLUSTER_DATA_FREE_DELAY = 4 // frames
	};
	jobs::Mutex m_cluster_data_mutex;
	gpu::BufferHandle m_cluster_data_buffer = gpu::INVALID_BUFFER;
	Array<ClusterDataRange> m_cluster_data_free;
	Array<ClusterDataRange> m_cluster_data_pending;

	// streaming of clusters
	enum : u32 {
		STREAM_MAX_GROUPS = 1 << 20,	// groups with ids in the feedback buffer, 8 MB
		STREAM_LOADS_PER_FRAME = 32,	// pages
		STREAM_ID_REUSE_DELAY = 64,		// frames before freed ids are reused, much more than the latency of the readback, so the ids of the feedback that is read back are never the ones of another stream
		STREAM_MIN_RESIDENCY = 30		// frames a loaded page stays, unless nothing else can be unloaded
	};
	// The feedback buffer, the same as FEEDBACK_* in nanite.hlsl. The GPU appends the requests (ids of groups that are needed) to a list, once for each id in a frame, and sets a bit for each
	// group that is used. The CPU reads back the counters, the list and the bits of the ids that exist (not the stamps) every frame and resets the counters and the bits.
	enum : u32 {
		FEEDBACK_REQUEST_CAPACITY = 4096,
		FEEDBACK_REQUESTS = 32,		// after the counters: requests, unused, selected clusters, selected dropped, triangles
		FEEDBACK_BITS = FEEDBACK_REQUESTS + FEEDBACK_REQUEST_CAPACITY * 8,
		FEEDBACK_STAMPS = FEEDBACK_BITS + STREAM_MAX_GROUPS / 8,	// 4 bytes for each id, GPU only
		FEEDBACK_BUFFER_BYTES = FEEDBACK_STAMPS + STREAM_MAX_GROUPS * 4
	};
	static constexpr u64 STREAM_HEADROOM = 8ull << 20;	// the streamer keeps this much of the buffer of cluster data free for the models that load (it unloads pages if there is less)
	struct StreamIds { u32 first; u32 count; u32 frame; };	// frame: when the ids were freed
	struct StreamRef { ClusterStream* stream; u32 page; float priority; };
	bool m_stream_enabled = !CommandLineParser::isOn("-cluster_stream_off");
	bool m_stream_load_all = CommandLineParser::isOn("-cluster_stream_all");	// for tests: every page is loaded by the streamer, the budget is ignored
	u64 m_stream_budget = getClusterStreamBudget();
	jobs::Mutex m_stream_mutex;
	Array<ClusterStream*> m_streams;		// the complete ones
	Array<StreamIds> m_stream_id_free;
	u32 m_stream_id_end = 0;
	Array<StreamRef> m_stream_wanted;
	Array<ClusterStream*> m_stream_by_id;	// the streams with ids, sorted by the first id, only in processClusterFeedback
	u32 m_stream_feedback_requests = 0;		// in the last feedback that was processed
	u32 m_stream_feedback_used = 0;
	u32 m_stream_feedback_dropped = 0;		// entries that did not fit in the lists
	u32 m_clusters_selected = 0;			// clusters selected by the culling in all views of a frame, from the same feedback
	u32 m_clusters_selected_dropped = 0;	// the ones that did not fit in the list of selected clusters
	u32 m_clusters_triangles = 0;		// triangles of the selected clusters that fit in the list (the drawn ones)
	u32 m_stream_loads = 0;
	u32 m_stream_evictions = 0;
	u32 m_feedback_stamp = 0;				// the stamp of the frame in which the last feedback of the GPU was processed
	gpu::BufferHandle m_feedback_buffer = gpu::INVALID_BUFFER;
	Mutex m_feedback_mutex;
	Array<u8> m_feedback_incoming;			// written by the render thread
	Array<u8> m_feedback_local;
	float m_lod_multiplier = 1;
	StaticString<MAX_PATH> m_screenshot_path;
	u32 m_screenshot_max_width = 0;
	u32 m_screenshot_max_height = 0;
	AtomicI32 m_screenshot_state = (i32)ScreenshotState::NONE;
	jobs::Counter m_init_signal;
	HashMap<RuntimeHash, String> m_semantic_defines;

	Array<RenderPlugin*> m_plugins;
	EvoxRender m_render_script;
	Local<FrameData> m_frames[2];
	jobs::Signal m_gpu_queue_empty;
	FrameData* m_gpu_queue = nullptr;
	FrameData* m_cpu_frame = nullptr;
	StackArray<FrameData*, 3> m_free_frames;
	jobs::Mutex m_frames_mutex;
	jobs::Signal m_has_free_frames;
	jobs::Counter m_last_render;

	GPUProfiler m_profiler;
	FrameThread m_frame_thread;

	struct MaterialBuffer {
		MaterialBuffer(IAllocator& alloc) 
			: map(alloc)
			, data(alloc)
		{}

		struct Data {
			Data() {}
			u32 ref_count;
			bool is_instance;
			union {
				RuntimeHash hash;
				u32 next_free;
			};
		};

		gpu::BufferHandle buffer = gpu::INVALID_BUFFER;
		Array<Data> data;
		int first_free;
		HashMap<RuntimeHash, u32> map;
	} m_material_buffer;

	u32 m_material_reload_counter = 0;

	Array<Renderbuffer> m_renderbuffers;
	// built-in postprocesses, the rest (film grain, DOF, bloom, SSAO, TAA, ...) are scripts in data/scripts/core/postprocess
	// environment
	CubemapSky m_cubemap_sky;
	// global
	TDAO m_tdao;
	bool m_builtin_taa_enabled = true;
};

FrameData::FrameData(struct RendererImpl& renderer, IAllocator& allocator, PageAllocator& page_allocator) 
	: renderer(renderer)
	, to_compile_shaders(allocator)
	, arena_allocator(1024 * 1024 * 64, allocator, "frame data")
	, draw_stream(renderer)
	, begin_frame_draw_stream(renderer)
	, end_frame_draw_stream(renderer)
{
	jobs::turnRed(&can_setup);
}

LUMIX_PLUGIN_ENTRY(renderer) {
	PROFILE_FUNCTION();
	return LUMIX_NEW(engine.getAllocator(), RendererImpl)(engine);
}


} // namespace Lumix



