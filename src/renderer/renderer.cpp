#include "renderer.h"

#include "engine/allocators.h"
#include "engine/array.h"
#include "engine/command_line_parser.h"
#include "engine/debug.h"
#include "engine/engine.h"
#include "engine/hash.h"
#include "engine/log.h"
#include "engine/atomic.h"
#include "engine/job_system.h"
#include "engine/sync.h"
#include "engine/thread.h"
#include "engine/os.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/universe.h"
#include "renderer/font.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/particle_system.h"
#include "renderer/render_scene.h"
#include "renderer/shader.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"


namespace Lumix
{


static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
static const char* downscale_src = R"#(
	layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
	layout (rgba8, binding = 0) uniform readonly image2D u_src;
	layout (rgba8, binding = 1) uniform writeonly image2D u_dst;
	layout(std140, binding = 4) uniform Data {
		ivec2 u_scale;
	};
	void main() {
		vec4 accum = vec4(0);
		for (int j = 0; j < u_scale.y; ++j) {
			for (int i = 0; i < u_scale.x; ++i) {
				vec4 v = imageLoad(u_src, ivec2(gl_GlobalInvocationID.xy) * u_scale + ivec2(i, j));
				accum += v;
			}
		}
		accum *= 1.0 / (u_scale.x * u_scale.y);
		imageStore(u_dst, ivec2(gl_GlobalInvocationID.xy), accum);
	}
)#";


template <u32 ALIGN>
struct TransientBuffer {
	static constexpr u32 INIT_SIZE = 1024 * 1024;
	static constexpr u32 OVERFLOW_BUFFER_SIZE = 512 * 1024 * 1024;
	
	void init(gpu::BufferFlags flags) {
		m_flags = flags;
		m_buffer = gpu::allocBufferHandle();
		m_offset = 0;
		gpu::createBuffer(m_buffer, gpu::BufferFlags::MAPPABLE | flags, INIT_SIZE, nullptr);
		m_size = INIT_SIZE;
		m_ptr = (u8*)gpu::map(m_buffer, INIT_SIZE);
	}

	Renderer::TransientSlice alloc(u32 size) {
		Renderer::TransientSlice slice;
		size = (size + (ALIGN - 1)) & ~(ALIGN - 1);
		slice.offset = atomicAdd(&m_offset, size);
		slice.size = size;
		if (slice.offset + size <= m_size) {
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
		gpu::unmap(m_buffer);
		m_ptr = nullptr;

		if (m_overflow.buffer) {
			gpu::createBuffer(m_overflow.buffer, gpu::BufferFlags::MAPPABLE | m_flags, nextPow2(m_overflow.size + m_size), nullptr);
			void* mem = gpu::map(m_overflow.buffer, m_overflow.size + m_size);
			if (mem) {
				memcpy(mem, m_overflow.data, m_overflow.size);
				gpu::unmap(m_overflow.buffer);
			}
			os::memRelease(m_overflow.data, OVERFLOW_BUFFER_SIZE);
			m_overflow.data = nullptr;
			m_overflow.commit = 0;
		}
	}

	void renderDone() {
		if (m_overflow.buffer) {
			m_size = nextPow2(m_overflow.size + m_size);
			gpu::destroy(m_buffer);
			m_buffer = m_overflow.buffer;
			m_overflow.buffer = gpu::INVALID_BUFFER;
			m_overflow.size = 0;
		}

		ASSERT(!m_ptr);
		m_ptr = (u8*)gpu::map(m_buffer, m_size);
		m_offset = 0;
	}

	gpu::BufferHandle m_buffer = gpu::INVALID_BUFFER;
	i32 m_offset = 0;
	u32 m_size = 0;
	u8* m_ptr = nullptr;
	jobs::Mutex m_mutex;
	gpu::BufferFlags m_flags = gpu::BufferFlags::NONE;

	struct {
		gpu::BufferHandle buffer = gpu::INVALID_BUFFER;
		u8* data = nullptr;
		u32 size = 0;
		u32 commit = 0;
	} m_overflow;
};


struct FrameData {
	FrameData(struct RendererImpl& renderer, IAllocator& allocator) 
		: jobs(allocator)
		, renderer(renderer)
		, to_compile_shaders(allocator)
		, material_updates(allocator)
		, job_allocator(1024 * 1024 * 64)
	{}

	struct ShaderToCompile {
		Shader* shader;
		gpu::VertexDecl decl;
		u32 defines;
		gpu::ProgramHandle program;
		Shader::Sources sources;
	};

	struct MaterialUpdates {
		u32 idx;
		float values[Material::MAX_UNIFORMS_FLOATS];
	};

	TransientBuffer<16> transient_buffer;
	TransientBuffer<256> uniform_buffer;
	u32 gpu_frame = 0xffFFffFF;

	LinearAllocator job_allocator;
	Array<MaterialUpdates> material_updates;
	Array<Renderer::RenderJob*> jobs;
	jobs::Mutex shader_mutex;
	Array<ShaderToCompile> to_compile_shaders;
	RendererImpl& renderer;
	jobs::Signal can_setup;
	jobs::Signal setup_done;
	u32 frame_number = 0;
};


template <typename T>
struct RenderResourceManager : ResourceManager
{
	RenderResourceManager(Renderer& renderer, IAllocator& allocator) 
		: ResourceManager(allocator)
		, m_renderer(renderer)
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
		gpu::QueryHandle q = gpu::createQuery(gpu::QueryType::TIMESTAMP);
		gpu::queryTimestamp(q);
		const u64 cpu_timestamp = os::Timer::getRawTimestamp();

		u32 try_num = 0;
		while (!gpu::isQueryReady(q) && try_num < 10) {
			gpu::swapBuffers();
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
				m_stats_query = gpu::INVALID_QUERY;
			}
		}
	}


	void frame()
	{
		PROFILE_FUNCTION();
		jobs::MutexGuard lock(m_mutex);
		while (!m_queries.empty()) {
			Query q = m_queries[0];
			
			if (!gpu::isQueryReady(q.handle)) break;

			if (q.is_end) {
				if (q.stats && !gpu::isQueryReady(q.stats)) break;

				const u64 timestamp = toCPUTimestamp(gpu::getQueryResult(q.handle));
				if (q.stats) {
					profiler::gpuStats(gpu::getQueryResult(q.stats));
					m_stats_pool.push(q.stats);
				}
				profiler::endGPUBlock(timestamp);
			}
			else {
				const u64 timestamp = toCPUTimestamp(gpu::getQueryResult(q.handle));
				profiler::beginGPUBlock(q.name, timestamp, q.profiler_link);
			}
			m_pool.push(q.handle);
			m_queries.erase(0);
		}
	}


	Array<Query> m_queries;
	Array<gpu::QueryHandle> m_pool;
	Array<gpu::QueryHandle> m_stats_pool;
	jobs::Mutex m_mutex;
	i64 m_gpu_to_cpu_offset;
	u32 m_stats_counter = 0;
	gpu::QueryHandle m_stats_query = gpu::INVALID_QUERY;
};


struct RendererImpl final : Renderer
{
	explicit RendererImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_texture_manager(*this, m_allocator)
		, m_pipeline_manager(*this, m_allocator)
		, m_model_manager(*this, m_allocator)
		, m_particle_emitter_manager(*this, m_allocator)
		, m_material_manager(*this, m_allocator)
		, m_shader_manager(*this, m_allocator)
		, m_font_manager(nullptr)
		, m_shader_defines(m_allocator)
		, m_profiler(m_allocator)
		, m_layers(m_allocator)
		, m_material_buffer(m_allocator)
		, m_plugins(m_allocator)
		, m_free_sort_keys(m_allocator)
		, m_sort_key_to_mesh_map(m_allocator)
	{
		RenderScene::reflect();

		LUMIX_GLOBAL_FUNC(Model::getBoneCount);
		LUMIX_GLOBAL_FUNC(Model::getBoneName);
		LUMIX_GLOBAL_FUNC(Model::getBoneParent);

		m_shader_defines.reserve(32);

		gpu::preinit(m_allocator, shouldLoadRenderdoc());
		m_frames[0].create(*this, m_allocator);
		m_frames[1].create(*this, m_allocator);
		m_frames[2].create(*this, m_allocator);
	}

	float getLODMultiplier() const override { return m_lod_multiplier; }
	void setLODMultiplier(float value) override { m_lod_multiplier = maximum(0.f, value); }

	u32 getVersion() const override { return 0; }
	void serialize(OutputMemoryStream& stream) const override {}
	bool deserialize(u32 version, InputMemoryStream& stream) override { return version == 0; }

	~RendererImpl()
	{
		m_particle_emitter_manager.destroy();
		m_pipeline_manager.destroy();
		m_texture_manager.destroy();
		m_model_manager.destroy();
		m_material_manager.destroy();
		m_shader_manager.destroy();
		m_font_manager->destroy();
		LUMIX_DELETE(m_allocator, m_font_manager);

		frame();
		frame();
		frame();

		waitForRender();
		
		jobs::Signal signal;
		jobs::runLambda([this]() {
			for (const Local<FrameData>& frame : m_frames) {
				gpu::destroy(frame->transient_buffer.m_buffer);
				gpu::destroy(frame->uniform_buffer.m_buffer);
			}
			gpu::destroy(m_material_buffer.buffer);
			gpu::destroy(m_material_buffer.staging_buffer);
			gpu::destroy(m_downscale_program);
			m_profiler.clear();
			gpu::shutdown();
		}, &signal, 1);
		jobs::wait(&signal);
	}

	static bool shouldLoadRenderdoc() {
		char cmd_line[4096];
		os::getCommandLine(Span(cmd_line));
		CommandLineParser cmd_line_parser(cmd_line);
		while (cmd_line_parser.next()) {
			if (cmd_line_parser.currentEquals("-renderdoc")) {
				return true;
			}
		}
		return false;
	}

	void init() override {
		struct InitData {
			gpu::InitFlags flags = gpu::InitFlags::VSYNC;
			RendererImpl* renderer;
		} init_data;
		init_data.renderer = this;
		
		char cmd_line[4096];
		os::getCommandLine(Span(cmd_line));
		CommandLineParser cmd_line_parser(cmd_line);
		while (cmd_line_parser.next()) {
			if (cmd_line_parser.currentEquals("-no_vsync")) {
				init_data.flags = init_data.flags & ~gpu::InitFlags::VSYNC;
			}
			else if (cmd_line_parser.currentEquals("-debug_opengl")) {
				init_data.flags = init_data.flags | gpu::InitFlags::DEBUG_OUTPUT;
			}
		}

		jobs::Signal signal;
		jobs::runLambda([&init_data]() {
			PROFILE_BLOCK("init_render");
			RendererImpl& renderer = *(RendererImpl*)init_data.renderer;
			Engine& engine = renderer.getEngine();
			void* window_handle = engine.getWindowHandle();
			if (!gpu::init(window_handle, init_data.flags)) {
				os::messageBox("Failed to initialize renderer. More info in lumix.log.");
			}

			gpu::MemoryStats mem_stats;
			if (gpu::getMemoryStats(mem_stats)) {
				logInfo("Initial GPU memory stats:\n",
					"total: ", (mem_stats.total_available_mem / (1024.f * 1024.f)), "MB\n"
					"currect: ", (mem_stats.current_available_mem / (1024.f * 1024.f)), "MB\n"
					"dedicated: ", (mem_stats.dedicated_vidmem/ (1024.f * 1024.f)), "MB\n");
			}

			for (const Local<FrameData>& frame : renderer.m_frames) {
				frame->transient_buffer.init(gpu::BufferFlags::NONE);
				frame->uniform_buffer.init(gpu::BufferFlags::UNIFORM_BUFFER);
			}
			renderer.m_cpu_frame = renderer.m_frames[0].get();
			renderer.m_gpu_frame = renderer.m_frames[0].get();

			renderer.m_profiler.init();

			MaterialBuffer& mb = renderer.m_material_buffer;
			mb.buffer = gpu::allocBufferHandle();
			mb.staging_buffer = gpu::allocBufferHandle();
			mb.map.insert(RuntimeHash(), 0);
			mb.data.resize(400);
			mb.data[0].hash = RuntimeHash();
			mb.data[0].ref_count = 1;
			mb.first_free = 1;
			for (int i = 1; i < 400; ++i) {
				mb.data[i].ref_count = 0;
				mb.data[i].next_free = i + 1;
			}
			mb.data.back().next_free = -1;
			gpu::createBuffer(mb.buffer
				, gpu::BufferFlags::UNIFORM_BUFFER
				, Material::MAX_UNIFORMS_BYTES * 400
				, nullptr
			);
			gpu::createBuffer(mb.staging_buffer
				, gpu::BufferFlags::UNIFORM_BUFFER
				, Material::MAX_UNIFORMS_BYTES
				, nullptr
			);

			renderer.m_downscale_program = gpu::allocProgramHandle();
			const gpu::ShaderType type = gpu::ShaderType::COMPUTE;
			const char* srcs[] = { downscale_src };
			gpu::createProgram(renderer.m_downscale_program, {}, srcs, &type, 1, nullptr, 0, "downscale");

			float default_mat[Material::MAX_UNIFORMS_FLOATS] = {};
			gpu::update(mb.buffer, &default_mat, sizeof(default_mat));
		}, &signal, 1);
		jobs::wait(&signal);

		ResourceManagerHub& manager = m_engine.getResourceManager();
		m_pipeline_manager.create(PipelineResource::TYPE, manager);
		m_texture_manager.create(Texture::TYPE, manager);
		m_model_manager.create(Model::TYPE, manager);
		m_material_manager.create(Material::TYPE, manager);
		m_particle_emitter_manager.create(ParticleEmitterResource::TYPE, manager);
		m_shader_manager.create(Shader::TYPE, manager);
		m_font_manager = LUMIX_NEW(m_allocator, FontManager)(*this, m_allocator);
		m_font_manager->create(FontResource::TYPE, manager);

		RenderScene::registerLuaAPI(m_engine.getState(), *this);

		m_layers.emplace("default");
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
		ret.data = m_allocator.allocate(size);
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


	void getTextureImage(gpu::TextureHandle texture, u32 w, u32 h, gpu::TextureFormat out_format, Span<u8> data) override
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::pushDebugGroup("get image data");
				gpu::TextureHandle staging = gpu::allocTextureHandle();
				const gpu::TextureFlags flags = gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::READBACK;
				gpu::createTexture(staging, w, h, 1, out_format, flags, "staging_buffer");
				gpu::copy(staging, handle, 0, 0);
				gpu::readTexture(staging, 0, buf);
				gpu::destroy(staging);
				gpu::popDebugGroup();
			}

			gpu::TextureHandle handle;
			gpu::TextureFormat out_format;
			u32 w;
			u32 h;
			Span<u8> buf;
		};

		
		Cmd& cmd = createJob<Cmd>();
		cmd.handle = texture;
		cmd.w = w;
		cmd.h = h;
		cmd.buf = data;
		cmd.out_format = out_format;
		queue(cmd, 0);
	}

	void updateBuffer(gpu::BufferHandle handle, const MemRef& mem) override {
		ASSERT(mem.size > 0);
		ASSERT(handle);

		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::update(handle, mem.data, mem.size);
				if (mem.own) {
					renderer->free(mem);
				}
			}

			gpu::BufferHandle handle;
			MemRef mem;
			RendererImpl* renderer;
		};

		Cmd& cmd = createJob<Cmd>();
		cmd.handle = handle;
		cmd.mem = mem;
		cmd.renderer = this;

		queue(cmd, 0);
	}

	void updateTexture(gpu::TextureHandle handle, u32 slice, u32 x, u32 y, u32 w, u32 h, gpu::TextureFormat format, const MemRef& mem) override
	{
		ASSERT(mem.size > 0);
		ASSERT(handle);

		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::update(handle, 0, x, y, slice, w, h, format, mem.data, mem.size);
				if (mem.own) {
					renderer->free(mem);
				}
			}

			gpu::TextureHandle handle;
			u32 x, y, w, h, slice;
			gpu::TextureFormat format;
			MemRef mem;
			RendererImpl* renderer;
		};

		Cmd& cmd = createJob<Cmd>();
		cmd.handle = handle;
		cmd.x = x;
		cmd.y = y;
		cmd.w = w;
		cmd.h = h;
		cmd.slice = slice;
		cmd.format = format;
		cmd.mem = mem;
		cmd.renderer = this;

		queue(cmd, 0);
	}


	gpu::TextureHandle loadTexture(const gpu::TextureDesc& desc, const MemRef& memory, gpu::TextureFlags flags, const char* debug_name) override
	{
		ASSERT(memory.size > 0);

		const gpu::TextureHandle handle = gpu::allocTextureHandle();
		if (!handle) return handle;

		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				if (!gpu::createTexture(handle, desc.width, desc.height, desc.depth, desc.format, flags, debug_name)) {
					if(memory.own) renderer->free(memory);
					logError("Failed to create texture ", debug_name);
					return;
				}
				
				const u8* ptr = (const u8*)memory.data;
				for (u32 layer = 0; layer < desc.depth; ++layer) {
					for(int side = 0; side < (desc.is_cubemap ? 6 : 1); ++side) {
						const u32 z = layer * (desc.is_cubemap ? 6 : 1) + side;
						for (u32 mip = 0; mip < desc.mips; ++mip) {
							const u32 w = maximum(desc.width >> mip, 1);
							const u32 h = maximum(desc.height >> mip, 1);
							const u32 mip_size_bytes = gpu::getSize(desc.format, w, h);
							gpu::update(handle, mip, 0, 0, z, w, h, desc.format, ptr, mip_size_bytes);
							ptr += mip_size_bytes;
						}
					}
				}
				if(memory.own) renderer->free(memory);
			}

			StaticString<LUMIX_MAX_PATH> debug_name;
			gpu::TextureHandle handle;
			MemRef memory;
			gpu::TextureFlags flags;
			gpu::TextureDesc desc;
			RendererImpl* renderer; 
		};

		Cmd& cmd = createJob<Cmd>();
		cmd.debug_name = debug_name;
		cmd.handle = handle;
		cmd.memory = memory;
		cmd.flags = flags;
		if (desc.is_cubemap) cmd.flags = cmd.flags | gpu::TextureFlags::IS_CUBE;
		if (desc.mips < 2) cmd.flags = cmd.flags | gpu::TextureFlags::NO_MIPS;
		cmd.renderer = this;
		cmd.desc = desc;
		queue(cmd, 0);

		return handle;
	}


	TransientSlice allocTransient(u32 size) override
	{
		jobs::wait(&m_cpu_frame->can_setup);
		return m_cpu_frame->transient_buffer.alloc(size);
	}

	TransientSlice allocUniform(u32 size) override
	{
		jobs::wait(&m_cpu_frame->can_setup);
		return m_cpu_frame->uniform_buffer.alloc(size);
	}
	
	gpu::BufferHandle getMaterialUniformBuffer() override {
		return m_material_buffer.buffer;
	}

	u32 createMaterialConstants(Span<const float> data) override {
		const RuntimeHash hash((const u8*)&data, data.length() * sizeof(float));
		auto iter = m_material_buffer.map.find(hash);
		u32 idx;
		if(iter.isValid()) {
			idx = iter.value();
		}
		else {
			if (m_material_buffer.first_free == -1) {
				ASSERT(false);
				++m_material_buffer.data[0].ref_count;
				return 0;
			}
			idx = m_material_buffer.first_free;
			m_material_buffer.first_free = m_material_buffer.data[m_material_buffer.first_free].next_free;
			m_material_buffer.data[idx].ref_count = 0;
			m_material_buffer.data[idx].hash = RuntimeHash((const u8*)&data, sizeof(data));
			m_material_buffer.map.insert(hash, idx);
			
			FrameData::MaterialUpdates& mu = m_cpu_frame->material_updates.emplace();
			mu.idx = idx;
			ASSERT(data.length() * sizeof(float) <= sizeof(mu.values));
			memcpy(mu.values, data.begin(), data.length() * sizeof(float));
		}
		++m_material_buffer.data[idx].ref_count;
		return idx;
	}

	void destroyMaterialConstants(u32 idx) override {
		--m_material_buffer.data[idx].ref_count;
		if (m_material_buffer.data[idx].ref_count > 0) return;
			
		const RuntimeHash hash = m_material_buffer.data[idx].hash;
		m_material_buffer.data[idx].next_free = m_material_buffer.first_free;
		m_material_buffer.first_free = idx;
		m_material_buffer.map.erase(hash);
	}


	gpu::BufferHandle createBuffer(const MemRef& memory, gpu::BufferFlags flags) override
	{
		gpu::BufferHandle handle = gpu::allocBufferHandle();
		if(!handle) return handle;

		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::createBuffer(handle, flags, memory.size, memory.data);
				if (memory.own) {
					renderer->free(memory);
				}
			}

			gpu::BufferHandle handle;
			MemRef memory;
			gpu::BufferFlags flags;
			gpu::TextureFormat format;
			Renderer* renderer;
		};

		Cmd& cmd = createJob<Cmd>();
		cmd.handle = handle;
		cmd.memory = memory;
		cmd.renderer = this;
		cmd.flags = flags;
		queue(cmd, 0);

		return handle;
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


	void runInRenderThread(void* user_ptr, void (*fnc)(Renderer& renderer, void*)) override
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				fnc(*renderer, ptr); 
			}

			void* ptr;
			void (*fnc)(Renderer&, void*);
			Renderer* renderer;

		};

		Cmd& cmd = createJob<Cmd>();
		cmd.fnc = fnc;
		cmd.ptr = user_ptr;
		cmd.renderer = this;
		queue(cmd, 0);
	}

	const Mesh** getSortKeyToMeshMap() const override {
		return m_sort_key_to_mesh_map.begin();
	}

	u32 allocSortKey(Mesh* mesh) override {
		if (!m_free_sort_keys.empty()) {
			const u32 key = m_free_sort_keys.back();
			m_free_sort_keys.pop();
			ASSERT(key != 0);
			if ((u32)m_sort_key_to_mesh_map.size() < key + 1)
				m_sort_key_to_mesh_map.resize(key + 1);
			m_sort_key_to_mesh_map[key] = mesh;
			return key;
		}
		++m_max_sort_key;
		const u32 key = m_max_sort_key;
		ASSERT(key != 0);
		if ((u32)m_sort_key_to_mesh_map.size() < key + 1)
			m_sort_key_to_mesh_map.resize(key + 1);
		m_sort_key_to_mesh_map[key] = mesh;
		return key;
	}

	void freeSortKey(u32 key) override {
		if (key != 0) {
			m_free_sort_keys.push(key);
		}
	}
	
	u32 getMaxSortKey() const override {
		return m_max_sort_key;
	}

	void destroy(gpu::ProgramHandle program) override
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				gpu::destroy(program); 
			}

			gpu::ProgramHandle program;
			RendererImpl* renderer;
		};

		Cmd& cmd = createJob<Cmd>();
		cmd.program = program;
		cmd.renderer = this;
		queue(cmd, 0);
	}

	void destroy(gpu::BufferHandle buffer) override
	{
		if (!buffer) return;
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				gpu::destroy(buffer);
			}

			gpu::BufferHandle buffer;
			RendererImpl* renderer;
		};

		Cmd& cmd = createJob<Cmd>();
		cmd.buffer = buffer;
		cmd.renderer = this;
		queue(cmd, 0);
	}

	void copy(gpu::TextureHandle dst, gpu::TextureHandle src) override {
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				gpu::copy(dst, src, 0, 0);
			}
			gpu::TextureHandle src;
			gpu::TextureHandle dst;
		};
		Cmd& cmd = createJob<Cmd>();
		cmd.src = src;
		cmd.dst = dst;
		queue(cmd, 0);
	}

	void downscale(gpu::TextureHandle src, u32 src_w, u32 src_h, gpu::TextureHandle dst, u32 dst_w, u32 dst_h) override {
		ASSERT(src_w % dst_w == 0);
		ASSERT(src_h % dst_h == 0);
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override {
				PROFILE_FUNCTION();
				
				gpu::bindUniformBuffer(4, ub_slice.buffer, ub_slice.offset, ub_slice.size);
				gpu::bindImageTexture(src, 0);
				gpu::bindImageTexture(dst, 1);
				gpu::useProgram(program);
				gpu::dispatch((dst_size.x + 15) / 16, (dst_size.y + 15) / 16, 1);
			}

			gpu::TextureHandle src;
			gpu::TextureHandle dst;
			gpu::ProgramHandle program;
			TransientSlice ub_slice;
			IVec2 src_size;
			IVec2 dst_size;
		};
		Cmd& cmd = createJob<Cmd>();
		cmd.src = src;
		cmd.dst = dst;
		IVec2 src_size((i32)src_w, (i32)src_h);
		cmd.dst_size = {(i32)dst_w, (i32)dst_h};
		cmd.program = m_downscale_program;
		const IVec2 scale = src_size / cmd.dst_size;
		cmd.ub_slice = allocUniform(sizeof(scale));
		memcpy(cmd.ub_slice.ptr, &scale, sizeof(scale));
		queue(cmd, 0);
	}


	gpu::TextureHandle createTexture(u32 w, u32 h, u32 depth, gpu::TextureFormat format, gpu::TextureFlags flags, const MemRef& memory, const char* debug_name) override
	{
		gpu::TextureHandle handle = gpu::allocTextureHandle();
		if(!handle) return handle;

		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override
			{
				PROFILE_FUNCTION();
				bool res = gpu::createTexture(handle, w, h, depth, format, flags, debug_name);
				ASSERT(res);
				if (memory.data && memory.size) {
					ASSERT(depth == 1);
					gpu::update(handle, 0, 0, 0, 0, w, h, format, memory.data, memory.size);
					if (u32(flags & gpu::TextureFlags::NO_MIPS) == 0) gpu::generateMipmaps(handle);
				}
				if (memory.own) renderer->free(memory);
			}

			StaticString<LUMIX_MAX_PATH> debug_name;
			gpu::TextureHandle handle;
			MemRef memory;
			u32 w;
			u32 h;
			u32 depth;
			gpu::TextureFormat format;
			Renderer* renderer;
			gpu::TextureFlags flags;
		};

		Cmd& cmd = createJob<Cmd>();
		cmd.debug_name = debug_name;
		cmd.handle = handle;
		cmd.memory = memory;
		cmd.format = format;
		cmd.flags = flags;
		cmd.w = w;
		cmd.h = h;
		cmd.depth = depth;
		cmd.renderer = this;
		queue(cmd, 0);

		return handle;
	}


	void destroy(gpu::TextureHandle tex) override
	{
		if (!tex) return;
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				gpu::destroy(texture); 
			}

			gpu::TextureHandle texture;
			RendererImpl* renderer;
		};

		Cmd& cmd = createJob<Cmd>();
		cmd.texture = tex;
		cmd.renderer = this;
		queue(cmd, 0);
	}


	void queue(RenderJob& cmd, i64 profiler_link) override
	{
		jobs::wait(&m_cpu_frame->can_setup);

		cmd.profiler_link = profiler_link;
		
		m_cpu_frame->jobs.push(&cmd);

		jobs::runLambda([&cmd](){
			PROFILE_BLOCK("setup_render_job");
			profiler::blockColor(0x50, 0xff, 0xff);
			cmd.setup();
		}, &m_cpu_frame->setup_done);
	}

	void addPlugin(RenderPlugin& plugin) override {
		m_plugins.push(&plugin);
	}

	void removePlugin(RenderPlugin& plugin) override {
		m_plugins.eraseItem(&plugin);
	}

	Span<RenderPlugin*> getPlugins() override { return m_plugins; }


	ResourceManager& getTextureManager() override { return m_texture_manager; }
	FontManager& getFontManager() override { return *m_font_manager; }

	void createScenes(Universe& ctx) override
	{
		UniquePtr<RenderScene> scene = RenderScene::createInstance(*this, m_engine, ctx, m_allocator);
		ctx.addScene(scene.move());
	}

	void* allocJob(u32 size, u32 align) override {
		jobs::wait(&m_cpu_frame->can_setup);
		return m_cpu_frame->job_allocator.allocate_aligned(size, align);
	}

	void deallocJob(void* job) override {
		m_cpu_frame->job_allocator.deallocate_aligned(job);
	}

	const char* getName() const override { return "renderer"; }
	Engine& getEngine() override { return m_engine; }
	int getShaderDefinesCount() const override { return m_shader_defines.size(); }
	const char* getShaderDefine(int define_idx) const override { return m_shader_defines[define_idx]; }

	gpu::ProgramHandle queueShaderCompile(Shader& shader, gpu::VertexDecl decl, u32 defines) override {
		ASSERT(shader.isReady());
		jobs::MutexGuard lock(m_cpu_frame->shader_mutex);
		
		for (const auto& i : m_cpu_frame->to_compile_shaders) {
			if (i.shader == &shader && decl.hash == i.decl.hash && defines == i.defines) {
				return i.program;
			}
		}
		gpu::ProgramHandle program = gpu::allocProgramHandle();
		m_cpu_frame->to_compile_shaders.push({&shader, decl, defines, program, shader.m_sources});
		return program;
	}

	void makeScreenshot(const Path& filename) override {  }


	u8 getShaderDefineIdx(const char* define) override
	{
		jobs::MutexGuard lock(m_shader_defines_mutex);
		for (int i = 0; i < m_shader_defines.size(); ++i)
		{
			if (m_shader_defines[i] == define)
			{
				return i;
			}
		}

		if (m_shader_defines.size() >= MAX_SHADER_DEFINES) {
			ASSERT(false);
			logError("Too many shader defines.");
		}

		m_shader_defines.emplace(define);
		ASSERT(m_shader_defines.size() <= 32); // m_shader_defines are reserved in renderer constructor, so getShaderDefine() is MT safe
		return u8(m_shader_defines.size() - 1);
	}


	void startCapture() override
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				gpu::startCapture();
			}
		};
		Cmd& cmd = createJob<Cmd>();
		queue(cmd, 0);
	}


	void stopCapture() override
	{
		struct Cmd : RenderJob {
			void setup() override {}
			void execute() override { 
				PROFILE_FUNCTION();
				gpu::stopCapture();
			}
		};
		Cmd& cmd = createJob<Cmd>();
		queue(cmd, 0);
	}

	void render() {
		jobs::MutexGuard guard(m_render_mutex);

		FrameData* next_frame = m_frames[(getFrameIndex(m_gpu_frame) + 1) % lengthOf(m_frames)].get();

		if (next_frame->gpu_frame != 0xffFFffFF && gpu::frameFinished(next_frame->gpu_frame)) {
			next_frame->gpu_frame = 0xFFffFFff;   
			next_frame->transient_buffer.renderDone();
			next_frame->uniform_buffer.renderDone();
			jobs::setGreen(&next_frame->can_setup);
		}
		
		FrameData& frame = *m_gpu_frame;
		profiler::pushInt("GPU Frame", getFrameIndex(m_gpu_frame));
		frame.transient_buffer.prepareToRender();
		frame.uniform_buffer.prepareToRender();
		
		gpu::MemoryStats mem_stats;
		if (gpu::getMemoryStats(mem_stats)) {
			//static u32 total_counter = profiler::createCounter("Total GPU memory (MB)", 0);
			static u32 available_counter = profiler::createCounter("Available GPU memory (MB)", 0);
			//static u32 dedicated_counter = profiler::createCounter("Dedicate Vid memory (MB)", 0);
			static u32 buffer_counter = profiler::createCounter("Buffer memory (MB)", 0);
			static u32 texture_counter = profiler::createCounter("Texture memory (MB)", 0);
			auto to_MB = [](u64 B){
				return float(double(B) / (1024.0 * 1024.0));
			};
			//profiler::pushCounter(total_counter, to_MB(mem_stats.total_available_mem));
			profiler::pushCounter(available_counter, to_MB(mem_stats.current_available_mem));
			//profiler::pushCounter(dedicated_counter, to_MB(mem_stats.dedicated_vidmem));
			profiler::pushCounter(buffer_counter, to_MB(mem_stats.buffer_mem));
			profiler::pushCounter(texture_counter, to_MB(mem_stats.texture_mem));
		}

		for (const auto& i : frame.to_compile_shaders) {
			Shader::compile(i.program, i.decl, i.defines, i.sources, *this);
		}
		frame.to_compile_shaders.clear();

		for (const auto& i : frame.material_updates) {
			gpu::update(m_material_buffer.staging_buffer, i.values, sizeof(i.values));
			gpu::copy(m_material_buffer.buffer, m_material_buffer.staging_buffer, i.idx * sizeof(i.values), 0, sizeof(i.values));
		}
		frame.material_updates.clear();

		gpu::useProgram(gpu::INVALID_PROGRAM);
		gpu::bindIndexBuffer(gpu::INVALID_BUFFER);
		gpu::bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
		gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
		for (u32 i = 0; i < (u32)UniformBuffer::COUNT; ++i) {
			gpu::bindUniformBuffer(i, gpu::INVALID_BUFFER, 0, 0);
		}

		m_profiler.beginQuery("frame", 0, false);
		for (RenderJob* job : frame.jobs) {
			PROFILE_BLOCK("render job");
			profiler::blockColor(0xaa, 0xff, 0xaa);
			profiler::link(job->profiler_link);
			job->execute();
			job->~RenderJob();
			frame.job_allocator.deallocate_aligned(job);
		}
		frame.job_allocator.reset();
		m_profiler.endQuery();
		frame.jobs.clear();

		jobs::enableBackupWorker(true);

		FrameData* prev_frame = m_frames[(getFrameIndex(m_gpu_frame) + lengthOf(m_frames) - 1) % lengthOf(m_frames)].get();
		if (prev_frame->gpu_frame != 0xffFFffFF && gpu::frameFinished(prev_frame->gpu_frame)) {
			prev_frame->gpu_frame = 0xFFffFFff;   
			prev_frame->transient_buffer.renderDone();
			prev_frame->uniform_buffer.renderDone();
			jobs::setGreen(&prev_frame->can_setup);
		}
		
		{
			PROFILE_BLOCK("swap buffers");
			frame.gpu_frame = gpu::swapBuffers();
		}
			
		jobs::enableBackupWorker(false);
		m_profiler.frame();

		m_gpu_frame = m_frames[(getFrameIndex(m_gpu_frame) + 1) % lengthOf(m_frames)].get();

		if (m_gpu_frame->gpu_frame != 0xffFFffFF) {
			gpu::waitFrame(m_gpu_frame->gpu_frame);
			m_gpu_frame->gpu_frame = 0xFFffFFff;   
			m_gpu_frame->transient_buffer.renderDone();
			m_gpu_frame->uniform_buffer.renderDone();
			jobs::setGreen(&m_gpu_frame->can_setup);
		}
	}
	
	LinearAllocator& getCurrentFrameAllocator() { return m_cpu_frame->job_allocator; }

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

	void frame() override
	{
		PROFILE_FUNCTION();
		
		jobs::wait(&m_cpu_frame->setup_done);
		for (const auto& i : m_cpu_frame->to_compile_shaders) {
			const u64 key = i.defines | ((u64)i.decl.hash.getHashValue() << 32);
			i.shader->m_programs.insert(key, i.program);
		}

		u32 frame_data_mem = 0;
		for (const Local<FrameData>& fd : m_frames) {
			frame_data_mem += fd->job_allocator.getCommited();
		}
		static u32 frame_data_counter = profiler::createCounter("Render frame data (kB)", 0);
		profiler::pushCounter(frame_data_counter, float(double(frame_data_mem) / 1024.0));

		jobs::setRed(&m_cpu_frame->can_setup);
		
		m_cpu_frame = m_frames[(getFrameIndex(m_cpu_frame) + 1) % lengthOf(m_frames)].get();
		++m_frame_number;
		m_cpu_frame->frame_number = m_frame_number;
		
		jobs::runLambda([this](){
			render();
		}, &m_last_render, 1);
	}

	Engine& m_engine;
	IAllocator& m_allocator;
	Array<StaticString<32>> m_shader_defines;
	jobs::Mutex m_render_mutex;
	jobs::Mutex m_shader_defines_mutex;
	Array<StaticString<32>> m_layers;
	FontManager* m_font_manager;
	MaterialManager m_material_manager;
	RenderResourceManager<Model> m_model_manager;
	RenderResourceManager<ParticleEmitterResource> m_particle_emitter_manager;
	RenderResourceManager<PipelineResource> m_pipeline_manager;
	RenderResourceManager<Shader> m_shader_manager;
	RenderResourceManager<Texture> m_texture_manager;
	gpu::ProgramHandle m_downscale_program;
	Array<u32> m_free_sort_keys;
	Array<const Mesh*> m_sort_key_to_mesh_map;
	u32 m_max_sort_key = 0;
	u32 m_frame_number = 0;
	float m_lod_multiplier = 1;

	Array<RenderPlugin*> m_plugins;
	Local<FrameData> m_frames[3];
	FrameData* m_gpu_frame = nullptr;
	FrameData* m_cpu_frame = nullptr;
	jobs::Signal m_last_render;

	GPUProfiler m_profiler;

	struct MaterialBuffer {
		MaterialBuffer(IAllocator& alloc) 
			: map(alloc)
			, data(alloc)
		{}

		struct Data {
			Data() {}
			u32 ref_count;
			union {
				RuntimeHash hash;
				u32 next_free;
			};
		};

		gpu::BufferHandle buffer = gpu::INVALID_BUFFER;
		gpu::BufferHandle staging_buffer = gpu::INVALID_BUFFER;
		Array<Data> data;
		int first_free;
		HashMap<RuntimeHash, u32> map;
	} m_material_buffer;
};


extern "C"
{
	LUMIX_PLUGIN_ENTRY(renderer)
	{
		return LUMIX_NEW(engine.getAllocator(), RendererImpl)(engine);
	}
}


} // namespace Lumix



