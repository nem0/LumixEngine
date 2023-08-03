#include "renderer.h"

#include "engine/allocators.h"
#include "engine/array.h"
#include "engine/atomic.h"
#include "engine/command_line_parser.h"
#include "engine/engine.h"
#include "engine/hash.h"
#include "engine/log.h"
#include "engine/job_system.h"
#include "engine/page_allocator.h"
#include "engine/sync.h"
#include "engine/thread.h"
#include "engine/os.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/world.h"
#include "renderer/draw_stream.h"
#include "renderer/font.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/particle_system.h"
#include "renderer/render_module.h"
#include "renderer/shader.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"


namespace Lumix {


static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");


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
		ASSERT(m_ptr);
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
	FrameData(struct RendererImpl& renderer, IAllocator& allocator, PageAllocator& page_allocator);

	struct ShaderToCompile {
		Shader* shader;
		gpu::VertexDecl decl;
		u32 defines;
		gpu::ProgramHandle program;
		gpu::StateFlags state;
	};

	TransientBuffer<16> transient_buffer;
	TransientBuffer<256> uniform_buffer;
	u32 gpu_frame = 0xffFFffFF;

	LinearAllocator linear_allocator;
	jobs::Mutex shader_mutex;
	Array<ShaderToCompile> to_compile_shaders;
	RendererImpl& renderer;
	jobs::Signal can_setup;
	jobs::Signal setup_done;
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
		, m_allocator(engine.getAllocator(), "renderer")
		, m_texture_manager("textures", *this, m_allocator)
		, m_pipeline_manager("pipelines", *this, m_allocator)
		, m_model_manager("models", *this, m_allocator)
		, m_particle_emitter_manager("particle emitters", *this, m_allocator)
		, m_material_manager(*this, m_allocator)
		, m_shader_manager("shaders", *this, m_allocator)
		, m_font_manager(nullptr)
		, m_shader_defines(m_allocator)
		, m_profiler(m_allocator)
		, m_layers(m_allocator)
		, m_material_buffer(m_allocator)
		, m_plugins(m_allocator)
		, m_free_sort_keys(m_allocator)
		, m_sort_key_to_mesh_map(m_allocator)
	{
		RenderModule::reflect();

		LUMIX_GLOBAL_FUNC(Model::getBoneCount);
		LUMIX_GLOBAL_FUNC(Model::getBoneName);
		LUMIX_GLOBAL_FUNC(Model::getBoneParent);

		m_shader_defines.reserve(32);

		gpu::preinit(m_allocator, shouldLoadRenderdoc());
		m_frames[0].create(*this, m_allocator, m_engine.getPageAllocator());
		m_frames[1].create(*this, m_allocator, m_engine.getPageAllocator());
		m_frames[2].create(*this, m_allocator, m_engine.getPageAllocator());
	}

	float getLODMultiplier() const override { return m_lod_multiplier; }
	void setLODMultiplier(float value) override { m_lod_multiplier = maximum(0.f, value); }

	void serialize(OutputMemoryStream& stream) const override {}
	bool deserialize(i32 version, InputMemoryStream& stream) override { return version == 0; }

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
		gpu::InitFlags flags = gpu::InitFlags::VSYNC;
		
		char cmd_line[4096];
		os::getCommandLine(Span(cmd_line));
		CommandLineParser cmd_line_parser(cmd_line);
		while (cmd_line_parser.next()) {
			if (cmd_line_parser.currentEquals("-no_vsync")) {
				flags = flags & ~gpu::InitFlags::VSYNC;
			}
			else if (cmd_line_parser.currentEquals("-debug_opengl")) {
				flags = flags | gpu::InitFlags::DEBUG_OUTPUT;
			}
		}

		jobs::Signal signal;
		jobs::runLambda([this, flags]() {
			PROFILE_BLOCK("init_render");
			void* window_handle = m_engine.getWindowHandle();
			if (!gpu::init(window_handle, flags)) {
				os::messageBox("Failed to initialize renderer. More info in lumix.log.");
			}

			gpu::MemoryStats mem_stats;
			if (gpu::getMemoryStats(mem_stats)) {
				logInfo("Initial GPU memory stats:\n",
					"total: ", (mem_stats.total_available_mem / (1024.f * 1024.f)), "MB\n"
					"currect: ", (mem_stats.current_available_mem / (1024.f * 1024.f)), "MB\n"
					"dedicated: ", (mem_stats.dedicated_vidmem/ (1024.f * 1024.f)), "MB\n");
			}

			for (const Local<FrameData>& frame : m_frames) {
				frame->transient_buffer.init(gpu::BufferFlags::NONE);
				frame->uniform_buffer.init(gpu::BufferFlags::UNIFORM_BUFFER);
			}
			m_profiler.init();
		}, &signal, 1);
		jobs::wait(&signal);

		m_cpu_frame = m_frames[0].get();
		m_gpu_frame = m_frames[0].get();

		MaterialBuffer& mb = m_material_buffer;
		const u32 MAX_MATERIAL_CONSTS_COUNT = 400;
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
			, gpu::BufferFlags::UNIFORM_BUFFER
			, Material::MAX_UNIFORMS_BYTES * MAX_MATERIAL_CONSTS_COUNT
			, nullptr
		);

		float default_mat[Material::MAX_UNIFORMS_FLOATS] = {};
		stream.update(mb.buffer, &default_mat, sizeof(default_mat));

		ResourceManagerHub& manager = m_engine.getResourceManager();
		m_pipeline_manager.create(PipelineResource::TYPE, manager);
		m_texture_manager.create(Texture::TYPE, manager);
		m_model_manager.create(Model::TYPE, manager);
		m_material_manager.create(Material::TYPE, manager);
		m_particle_emitter_manager.create(ParticleSystemResource::TYPE, manager);
		m_shader_manager.create(Shader::TYPE, manager);
		m_font_manager = LUMIX_NEW(m_allocator, FontManager)(*this, m_allocator);
		m_font_manager->create(FontResource::TYPE, manager);

		RenderModule::registerLuaAPI(m_engine.getState(), *this);

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

	TransientSlice allocTransient(u32 size) override
	{
		jobs::wait(&m_cpu_frame->can_setup);
		return m_cpu_frame->transient_buffer.alloc(size);
	}

	TransientSlice allocUniform(const void* data, u32 size) override {
		jobs::wait(&m_cpu_frame->can_setup);
		const TransientSlice slice = m_cpu_frame->uniform_buffer.alloc(size);
		memcpy(slice.ptr, data, size);
		return slice;
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
				return 0;
			}
			idx = m_material_buffer.first_free;
			m_material_buffer.first_free = m_material_buffer.data[m_material_buffer.first_free].next_free;
			m_material_buffer.data[idx].ref_count = 0;
			m_material_buffer.data[idx].hash = RuntimeHash(data.begin(), data.length() * sizeof(float));
			m_material_buffer.map.insert(hash, idx);
			
			jobs::wait(&m_cpu_frame->can_setup);
			const u32 size = u32(data.length() * sizeof(float));
			const TransientSlice slice = m_cpu_frame->uniform_buffer.alloc(size);
			memcpy(slice.ptr, data.begin(), size);
			m_cpu_frame->draw_stream.copy(m_material_buffer.buffer, slice.buffer, idx * Material::MAX_UNIFORMS_BYTES, slice.offset, size);
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

		DrawStream& stream = getDrawStream();
		stream.createBuffer(handle, flags, memory.size, memory.data);
		if (memory.own) stream.freeMemory(memory.data, m_allocator);
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

	gpu::TextureHandle createTexture(u32 w, u32 h, u32 depth, gpu::TextureFormat format, gpu::TextureFlags flags, const MemRef& memory, const char* debug_name) override
	{
		gpu::TextureHandle handle = gpu::allocTextureHandle();
		if(!handle) return handle;

		DrawStream& stream = getDrawStream();
		stream.createTexture(handle, w, h, depth, format, flags, debug_name);
		if (memory.data && memory.size) {
			ASSERT(depth == 1);
			stream.update(handle, 0, 0, 0, 0, w, h, format, memory.data, memory.size);
			if (u32(flags & gpu::TextureFlags::NO_MIPS) == 0) stream.generateMipmaps(handle);
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

	gpu::ProgramHandle queueShaderCompile(Shader& shader, gpu::StateFlags state, gpu::VertexDecl decl, u32 defines) override {
		ASSERT(shader.isReady());
		jobs::MutexGuard lock(m_cpu_frame->shader_mutex);
		
		for (const auto& i : m_cpu_frame->to_compile_shaders) {
			if (i.shader == &shader && decl.hash == i.decl.hash && defines == i.defines && i.state == state) {
				return i.program;
			}
		}
		gpu::ProgramHandle program = gpu::allocProgramHandle();
		shader.compile(program, state, decl, defines, m_cpu_frame->begin_frame_draw_stream);
		m_cpu_frame->to_compile_shaders.push({&shader, decl, defines, program, state});
		return program;
	}


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

		frame.draw_stream.run();
		frame.draw_stream.reset();

		frame.end_frame_draw_stream.run();
		frame.end_frame_draw_stream.reset();

		frame.linear_allocator.reset();
		m_profiler.endQuery();

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
		
		if (frame.gpu_frame != 0xffFFffFF && gpu::frameFinished(frame.gpu_frame)) {
			frame.gpu_frame = 0xFFffFFff;
			frame.transient_buffer.renderDone();
			frame.uniform_buffer.renderDone();
			jobs::setGreen(&frame.can_setup);
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
	
	LinearAllocator& getCurrentFrameAllocator() override { return m_cpu_frame->linear_allocator; }

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
		
		// we have to wait for `can_setup` in case somebody calls frame() several times in a row
		jobs::wait(&m_cpu_frame->can_setup);
		jobs::wait(&m_cpu_frame->setup_done);

		m_cpu_frame->draw_stream.useProgram(gpu::INVALID_PROGRAM);
		m_cpu_frame->draw_stream.bindIndexBuffer(gpu::INVALID_BUFFER);
		m_cpu_frame->draw_stream.bindVertexBuffer(0, gpu::INVALID_BUFFER, 0, 0);
		m_cpu_frame->draw_stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
		for (u32 i = 0; i < (u32)UniformBuffer::COUNT; ++i) {
			m_cpu_frame->draw_stream.bindUniformBuffer(i, gpu::INVALID_BUFFER, 0, 0);
		}

		for (const auto& i : m_cpu_frame->to_compile_shaders) {
			Shader::ShaderKey key;
			key.defines = i.defines;
			key.decl_hash = i.decl.hash;
			key.state = i.state;
			i.shader->m_programs.push({key, i.program});
		}
		m_cpu_frame->to_compile_shaders.clear();

		u32 frame_data_mem = 0;
		for (const Local<FrameData>& fd : m_frames) {
			frame_data_mem += fd->linear_allocator.getCommitedBytes();
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
	TagAllocator m_allocator;
	Array<StaticString<32>> m_shader_defines;
	jobs::Mutex m_render_mutex;
	jobs::Mutex m_shader_defines_mutex;
	Array<StaticString<32>> m_layers;
	FontManager* m_font_manager;
	MaterialManager m_material_manager;
	RenderResourceManager<Model> m_model_manager;
	RenderResourceManager<ParticleSystemResource> m_particle_emitter_manager;
	RenderResourceManager<PipelineResource> m_pipeline_manager;
	RenderResourceManager<Shader> m_shader_manager;
	RenderResourceManager<Texture> m_texture_manager;
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
		Array<Data> data;
		int first_free;
		HashMap<RuntimeHash, u32> map;
	} m_material_buffer;
};

FrameData::FrameData(struct RendererImpl& renderer, IAllocator& allocator, PageAllocator& page_allocator) 
	: renderer(renderer)
	, to_compile_shaders(allocator)
	, linear_allocator(1024 * 1024 * 64)
	, draw_stream(renderer)
	, begin_frame_draw_stream(renderer)
	, end_frame_draw_stream(renderer)
{}

LUMIX_PLUGIN_ENTRY(renderer)
{
	return LUMIX_NEW(engine.getAllocator(), RendererImpl)(engine);
}


} // namespace Lumix



