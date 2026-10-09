#include "renderer/evox_render.h"
#include "core/log.h"
#include "core/profiler.h"
#include "engine/engine.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "evox/capi.h"
#include "evox/evox_resource.h"
#include "renderer/pipeline.h"
#include "renderer/render_module.h"
#include "renderer/shader.h"
#include <string.h>

namespace Lumix {

static constexpr const char* ROOT_PATH = EVOX_RENDER_PATH;
static constexpr const char* MAIN_FUNCTION = "main";
static constexpr const char* DEBUG_UI_FUNCTION = "debugUI";
static constexpr const char* RENDER_BUFFER_TYPE = "core:render.RenderBuffer";
static constexpr const char* STORAGE_TYPE = "core:render.Storage";

// Scripts see render buffers as handle + 1, so that zero (e.g. in zero-initialized state) is an invalid buffer.
static u32 toScript(RenderBufferHandle handle) { return u32(handle) + 1; }
static RenderBufferHandle fromScript(u32 handle) { return RenderBufferHandle(handle - 1); }

// Native side of core:render.Context, valid only for the duration of a single script call.
struct EvoxRenderContext {
	EvoxRender* plugin;
	Pipeline* pipeline;
	// Set by the frame script; null before setGBuffer and in debugUI.
	const GBuffer* gbuffer;
	// buffers handed out for writing (rwBindless) since the last dispatch, they get a memory barrier after it
	RenderBufferHandle written[8];
	u32 written_count;
	gpu::BufferHandle written_storage[4];
	u32 written_storage_count;
	// Render buffer handle returned by main (0 if none).
	u32 result;
	CameraParams cameras[8];
	// buckets being collected for `cull`
	struct Bucket {
		char layer[32];
		char define[32];
		BucketDesc::Sort sort;
		gpu::StateFlags state;
	} buckets[8];
	u32 bucket_count;
	u32 camera_count;
	// main: the G-buffer set by the script, and whether the frame was started (and has to be ended if the script fails)
	GBuffer gbuffer_storage;
	bool frame_begun;
	// render buffers created by the script and not released yet, released if the script fails
	RenderBufferHandle created[64];
	u32 created_count;
	// profiler blocks opened by the script and not ended yet, ended if the script fails so that the blocks of the C++ code stay balanced
	u32 open_blocks;
};

static void trackCreated(EvoxRenderContext& ctx, RenderBufferHandle handle) {
	if (ctx.created_count < lengthOf(ctx.created)) ctx.created[ctx.created_count++] = handle;
}

static void untrackCreated(EvoxRenderContext& ctx, RenderBufferHandle handle) {
	for (u32 i = 0; i < ctx.created_count; ++i) {
		if (ctx.created[i] == handle) {
			ctx.created[i] = ctx.created[--ctx.created_count];
			return;
		}
	}
}

static ex_string_view toEvox(const char* value) {
	return {value, (i64)strlen(value)};
}

static void logCallFailure(const char* what, ex_task* task, ex_call_result result) {
	logError("Evox render ", what, " failed (result ", (int)result, ")");
	ex_debug_event event = {};
	if (ex_debug_pause_event(task, &event) == EX_RESULT_OK && event.message.begin) {
		logError("  ", StringView(event.message.begin, (u64)event.message.length));
	}
	for (u32 i = 0, count = ex_debug_stack_depth(task); i < count; ++i) {
		ex_debug_location location;
		if (ex_debug_frame_location(task, i, &location) != EX_RESULT_OK) continue;
		logError("  at ", StringView(location.source_name.begin, (u64)location.source_name.length), ":", location.line + 1);
	}
}

EvoxRender::EvoxRender(Engine& engine, IAllocator& allocator)
	: m_engine(engine)
	, m_allocator(allocator)
	, m_state(allocator)
	, m_pending_release(allocator)
	, m_storage(allocator)
	, m_pending_destroy(allocator)
	, m_shaders(allocator)
	, m_debug_selection(allocator)
{}

void EvoxRender::detach() {
	// state types belong to the bytecode
	for (StateBlock& block : m_state) freeState(block);
	m_state.clear();
	for (gpu::BufferHandle& buffer : m_storage) {
		if (buffer != gpu::INVALID_BUFFER) m_pending_destroy.push(buffer);
	}
	m_storage.clear();
	m_debug_selection.clear();
	if (m_task) { ex_task_destroy(m_task); m_task = nullptr; }
	m_runtime = nullptr;
	m_main_function = INVALID_FUNCTION;
	m_debug_ui_function = INVALID_FUNCTION;
	m_bytecode = nullptr;
}

void EvoxRender::attach(ex_bytecode* bytecode, ex_runtime* runtime) {
	detach();
	if (!bytecode || !runtime) return;
	const i32 main_function = ex_runtime_find_function(runtime, toEvox(ROOT_PATH), toEvox(MAIN_FUNCTION));
	if (main_function < 0) return;
	if (ex_function_result_kind(runtime, (u32)main_function) != EX_TYPE_STRUCT) {
		logError(ROOT_PATH, " does not define `fn ", MAIN_FUNCTION, "(ctx : Context) : RenderBuffer`");
		return;
	}
	m_bytecode = bytecode;
	m_runtime = runtime;
	m_main_function = (u32)main_function;
	const i32 debug_ui_function = ex_runtime_find_function(runtime, toEvox(ROOT_PATH), toEvox(DEBUG_UI_FUNCTION));
	m_debug_ui_function = debug_ui_function < 0 ? INVALID_FUNCTION : (u32)debug_ui_function;
	m_task = ex_task_create(m_runtime);
	if (!m_task) detach();
}

void EvoxRender::recreateTask() {
	if (m_task) ex_task_destroy(m_task);
	m_task = m_runtime ? ex_task_create(m_runtime) : nullptr;
}

u8* EvoxRender::getState(Pipeline& pipeline, u32 type_index, u32& size) {
	for (const StateBlock& block : m_state) {
		if (block.pipeline == &pipeline && block.type_index == type_index) {
			size = block.size;
			return block.memory;
		}
	}
	if (!m_bytecode || type_index >= ex_bytecode_type_count(m_bytecode)) return nullptr;
	const ex_type* type = ex_bytecode_type(m_bytecode, type_index);
	const u32 type_size = ex_type_get_size(type);
	if (type_size == 0) return nullptr;

	const u32 alignment = ex_type_get_alignment(type) > 16 ? ex_type_get_alignment(type) : 16;
	u8* memory = (u8*)m_allocator.allocate(type_size, alignment);
	memset(memory, 0, type_size);
	m_state.push({&pipeline, type_index, type_size, memory, type});
	size = type_size;
	return memory;
}

void EvoxRender::queueBuffersForRelease(const ex_type* type, const u8* memory) {
	switch (ex_type_get_kind(type)) {
		case EX_TYPE_STRUCT: {
			const ex_string_view name = ex_type_get_name(type);
			if (StringView(name.begin, (u64)name.length) == RENDER_BUFFER_TYPE) {
				u32 handle;
				memcpy(&handle, memory, sizeof(handle));
				if (handle != 0) m_pending_release.push(fromScript(handle));
				break;
			}
			if (StringView(name.begin, (u64)name.length) == STORAGE_TYPE) {
				u32 id;
				memcpy(&id, memory, sizeof(id));
				releaseStorage(id);
				break;
			}
			for (u32 i = 0, count = ex_type_struct_field_count(type); i < count; ++i) {
				queueBuffersForRelease(ex_type_struct_field_type(type, i), memory + ex_type_struct_field_offset(type, i));
			}
			break;
		}
		case EX_TYPE_ARRAY: {
			const ex_type* element = ex_type_array_element_type(type);
			const u32 stride = ex_type_get_size(element);
			for (u32 i = 0, count = ex_type_array_length(type); i < count; ++i) {
				queueBuffersForRelease(element, memory + i * stride);
			}
			break;
		}
		default: break;
	}
}

void EvoxRender::freeState(StateBlock& block) {
	queueBuffersForRelease(block.type, block.memory);
	m_allocator.deallocate(block.memory);
	block.memory = nullptr;
}

u32 EvoxRender::createStorage(Renderer& renderer, u32 size) {
	const gpu::BufferHandle buffer = renderer.createBuffer(Renderer::MemRef{size}, gpu::BufferFlags::SHADER_BUFFER, "evox");
	for (u32 i = 0; i < (u32)m_storage.size(); ++i) {
		if (m_storage[i] == gpu::INVALID_BUFFER) {
			m_storage[i] = buffer;
			return i + 1;
		}
	}
	m_storage.push(buffer);
	return m_storage.size();
}

gpu::BufferHandle EvoxRender::getStorage(u32 id) const {
	if (id == 0 || id > (u32)m_storage.size()) return gpu::INVALID_BUFFER;
	return m_storage[id - 1];
}

void EvoxRender::releaseStorage(u32 id) {
	if (id == 0 || id > (u32)m_storage.size()) return;
	if (m_storage[id - 1] == gpu::INVALID_BUFFER) return;
	m_pending_destroy.push(m_storage[id - 1]);
	m_storage[id - 1] = gpu::INVALID_BUFFER;
}

void EvoxRender::destroyPending(Renderer& renderer) {
	for (RenderBufferHandle handle : m_pending_release) renderer.releaseRenderbuffer(handle);
	m_pending_release.clear();
	for (gpu::BufferHandle buffer : m_pending_destroy) renderer.getEndFrameDrawStream().destroy(buffer);
	m_pending_destroy.clear();
}

void EvoxRender::pipelineDestroyed(Pipeline& pipeline) {
	for (i32 i = m_debug_selection.size() - 1; i >= 0; --i) {
		if (m_debug_selection[i].pipeline == &pipeline) m_debug_selection.swapAndPop(i);
	}
	for (i32 i = m_state.size() - 1; i >= 0; --i) {
		if (m_state[i].pipeline != &pipeline) continue;
		freeState(m_state[i]);
		m_state.swapAndPop(i);
	}
}

Shader* EvoxRender::loadShader(StringView path) {
	const Path shader_path(path);
	for (const ShaderEntry& entry : m_shaders) {
		if (entry.path == shader_path) return entry.shader;
	}
	Shader* shader = m_engine.getResourceManager().load<Shader>(shader_path);
	if (shader) m_shaders.push({shader_path, shader});
	return shader;
}

void EvoxRender::shutdown(Renderer& renderer) {
	detach();
	for (const ShaderEntry& entry : m_shaders) entry.shader->decRefCount();
	m_shaders.clear();
	destroyPending(renderer);
}

bool EvoxRender::renderFrame(RenderBufferHandle& output, Pipeline& pipeline) {
	if (!m_pending_release.empty() || !m_pending_destroy.empty()) destroyPending(pipeline.getRenderer());
	if (!m_task || m_main_function == INVALID_FUNCTION) return false;

	EvoxRenderContext context = {this, &pipeline};
	if (!callEntry(m_main_function, context)) {
		// main is disabled until the script is reloaded; the pipeline draws only the UI.
		m_main_function = INVALID_FUNCTION;
		for (; context.open_blocks > 0; --context.open_blocks) pipeline.endBlock();
		if (context.frame_begun) pipeline.endFrame3D();
		for (u32 i = 0; i < context.created_count; ++i) pipeline.getRenderer().releaseRenderbuffer(context.created[i]);
		return false;
	}
	if (!context.result) return false;
	output = fromScript(context.result);
	return true;
}

// Calls a function of render.evox that takes the context. Returns false if it failed.
bool EvoxRender::callEntry(u32 function, EvoxRenderContext& context) {
	PROFILE_FUNCTION();
	EvoxRenderContext* context_ptr = &context;
	context.result = 0;
	const ex_call_result result = ex_call_function(m_task, function, &context_ptr, sizeof(context_ptr));
	if (result != EX_CALL_RESULT_OK) {
		logCallFailure(function == m_main_function ? MAIN_FUNCTION : DEBUG_UI_FUNCTION, m_task, result);
		recreateTask();
		return false;
	}
	if (function != m_main_function) return true;

	u32 size = 0;
	const void* returned = ex_task_result(m_task, &size);
	if (returned && size == sizeof(u32)) memcpy(&context.result, returned, sizeof(u32));
	return true;
}

void EvoxRender::showDebug(Pipeline& pipeline, StringView name) {
	pipeline.m_debug_show_plugin = this;
	pipeline.m_debug_show = Pipeline::DebugShow::PLUGIN;
	for (DebugSelection& selection : m_debug_selection) {
		if (selection.pipeline == &pipeline) {
			selection.name = StaticString<64>(name);
			return;
		}
	}
	m_debug_selection.push({&pipeline, StaticString<64>(name)});
}

bool EvoxRender::isDebugShown(Pipeline& pipeline, StringView name) const {
	if (pipeline.m_debug_show_plugin != this) return false;
	for (const DebugSelection& selection : m_debug_selection) {
		if (selection.pipeline == &pipeline) return StringView(selection.name) == name;
	}
	return false;
}

// Called from the debug popup of Studio's views. `debugUI` of render.evox draws ImGui widgets and use ctx.showDebug / ctx.isDebugShown
// to switch the debug view to one of their buffers.
void EvoxRender::debugUI(Pipeline& pipeline) {
	if (!m_task || m_debug_ui_function == INVALID_FUNCTION) return;
	EvoxRenderContext context = {this, &pipeline};
	if (!callEntry(m_debug_ui_function, context)) {
		for (; context.open_blocks > 0; --context.open_blocks) pipeline.endBlock();
		m_debug_ui_function = INVALID_FUNCTION;
	}
}

namespace Evox {

namespace {

static void panic(ex_call_frame& frame, const char* message) {
	*frame.panic = {message, (i64)strlen(message)};
}

static EvoxRenderContext* readContext(ex_call_frame& frame) {
	EX_ARG(frame, EvoxRenderContext*, ctx);
	if (!ctx) panic(frame, "Invalid render context");
	return ctx;
}

// Makes writes of the previous dispatch visible to whatever reads the buffers next.
static void flushWrites(EvoxRenderContext& ctx) {
	Renderer& renderer = ctx.pipeline->getRenderer();
	DrawStream& stream = renderer.getDrawStream();
	for (u32 i = 0; i < ctx.written_count; ++i) stream.memoryBarrier(renderer.toTexture(ctx.written[i]));
	ctx.written_count = 0;
	for (u32 i = 0; i < ctx.written_storage_count; ++i) stream.memoryBarrier(ctx.written_storage[i]);
	ctx.written_storage_count = 0;
}

static void scriptDepth(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	if (!ctx->gbuffer) {
		panic(frame, "Depth is not available here (set it with setGBuffer)");
		return;
	}
	EX_RESULT(frame, toScript(ctx->gbuffer->DS));
}

static void scriptBuiltinTaaEnabled(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, u8(ctx->pipeline->getRenderer().isBuiltinTAAEnabled() ? 1 : 0));
}

static void scriptGBuffer(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, index);
	if (!ctx->gbuffer || index > 3) {
		panic(frame, "G-buffer is not available here (set it with setGBuffer)");
		return;
	}
	const RenderBufferHandle buffers[] = {ctx->gbuffer->A, ctx->gbuffer->B, ctx->gbuffer->C, ctx->gbuffer->D};
	EX_RESULT(frame, toScript(buffers[index]));
}

static void scriptRenderModule(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	RenderModule* module = ctx->pipeline->getModule();
	EX_RESULT(frame, module);
}

static void scriptPipelineType(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, (i32)ctx->pipeline->getType());
}

static void scriptWidth(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, u32(ctx->pipeline->getViewport().w));
}

static void scriptHeight(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, u32(ctx->pipeline->getViewport().h));
}

static void scriptDisplayWidth(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, u32(ctx->pipeline->getDisplaySize().x));
}

static void scriptDisplayHeight(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, u32(ctx->pipeline->getDisplaySize().y));
}

static void scriptEnablePixelJitter(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u8, enable);
	ctx->pipeline->enablePixelJitter(enable != 0);
}

static void scriptLoadShader(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_STRING_ARG(frame, path);
	if (path.length <= 0 || !path.begin) {
		panic(frame, "Invalid shader path");
		return;
	}
	Shader* shader = ctx->plugin->loadShader(StringView(path.begin, (u64)path.length));
	if (!shader) {
		panic(frame, "Could not load shader");
		return;
	}
	EX_RESULT(frame, shader);
}

static void scriptIsReady(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, Shader*, shader);
	EX_RESULT(frame, u8(shader && shader->isReady() ? 1 : 0));
}

static void scriptBindless(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, rb);
	DrawStream& stream = ctx->pipeline->getRenderer().getDrawStream();
	EX_RESULT(frame, ctx->pipeline->toBindless(fromScript(rb), stream).value);
}

static void scriptRWBindless(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, rb);
	DrawStream& stream = ctx->pipeline->getRenderer().getDrawStream();
	const RenderBufferHandle handle = fromScript(rb);
	bool known = false;
	for (u32 i = 0; i < ctx->written_count; ++i) known = known || ctx->written[i] == handle;
	if (!known) {
		if (ctx->written_count == lengthOf(ctx->written)) {
			panic(frame, "Too many written render buffers in one dispatch");
			return;
		}
		ctx->written[ctx->written_count++] = handle;
	}
	EX_RESULT(frame, ctx->pipeline->toRWBindless(handle, stream).value);
}

static void scriptBeginBlock(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_STRING_ARG(frame, name);
	EX_ARG(frame, u8, stats);
	char tmp[64];
	const u32 len = name.length > 0 ? (u32)(name.length < (i64)sizeof(tmp) - 1 ? name.length : (i64)sizeof(tmp) - 1) : 0;
	if (len) memcpy(tmp, name.begin, len);
	tmp[len] = '\0';
	ctx->pipeline->beginBlock(tmp, stats != 0);
	++ctx->open_blocks;
}

static void scriptEndBlock(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	if (ctx->open_blocks == 0) {
		panic(frame, "endBlock without beginBlock");
		return;
	}
	--ctx->open_blocks;
	ctx->pipeline->endBlock();
}

static void scriptStateRaw(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, type_index);
	u32 size = 0;
	u8* memory = ctx->plugin->getState(*ctx->pipeline, type_index, size);
	if (!memory) {
		panic(frame, "Invalid state type");
		return;
	}
	const ex_slice result = {memory, (i64)size};
	EX_RESULT(frame, result);
}

static void scriptReleaseBuffer(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, rb);
	untrackCreated(*ctx, fromScript(rb));
	ctx->pipeline->getRenderer().releaseRenderbuffer(fromScript(rb));
}

static void scriptBlit(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, src);
	EX_ARG(frame, u32, dst);
	EX_ARG(frame, u32, width);
	EX_ARG(frame, u32, height);
	if (src == 0 || dst == 0) {
		panic(frame, "Invalid render buffer");
		return;
	}
	DrawStream& stream = ctx->pipeline->getRenderer().getDrawStream();
	flushWrites(*ctx);
	ctx->pipeline->blit(ctx->pipeline->toBindless(fromScript(src), stream), ctx->pipeline->toRWBindless(fromScript(dst), stream), IVec2((i32)width, (i32)height));
	stream.memoryBarrier(ctx->pipeline->getRenderer().toTexture(fromScript(dst)));
}

static void scriptCreateStorage(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, size);
	if (size == 0 || size > 64 * 1024 * 1024) {
		panic(frame, "Invalid storage size");
		return;
	}
	EX_RESULT(frame, ctx->plugin->createStorage(ctx->pipeline->getRenderer(), size));
}

static void scriptReleaseStorage(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, id);
	ctx->plugin->releaseStorage(id);
}

static void scriptBindlessStorage(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, id);
	const gpu::BufferHandle buffer = ctx->plugin->getStorage(id);
	if (buffer == gpu::INVALID_BUFFER) {
		panic(frame, "Invalid storage");
		return;
	}
	ctx->pipeline->getRenderer().getDrawStream().barrier(buffer, gpu::BarrierType::READ);
	EX_RESULT(frame, gpu::getBindlessHandle(buffer).value);
}

static void scriptRWBindlessStorage(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, id);
	const gpu::BufferHandle buffer = ctx->plugin->getStorage(id);
	if (buffer == gpu::INVALID_BUFFER) {
		panic(frame, "Invalid storage");
		return;
	}
	ctx->pipeline->getRenderer().getDrawStream().barrier(buffer, gpu::BarrierType::WRITE);
	bool known = false;
	for (u32 i = 0; i < ctx->written_storage_count; ++i) known = known || ctx->written_storage[i] == buffer;
	if (!known) {
		if (ctx->written_storage_count == lengthOf(ctx->written_storage)) {
			panic(frame, "Too many written storage buffers in one dispatch");
			return;
		}
		ctx->written_storage[ctx->written_storage_count++] = buffer;
	}
	EX_RESULT(frame, gpu::getRWBindlessHandle(buffer).value);
}

static bool readDefine(ex_string_view define, char (&out)[64]) {
	if (define.length < 0 || define.length >= (i64)sizeof(out) || (define.length > 0 && !define.begin)) return false;
	if (define.length > 0) memcpy(out, define.begin, define.length);
	out[define.length] = '\0';
	return true;
}

static void scriptDispatch(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, Shader*, shader);
	EX_STRING_ARG(frame, define_view);
	EX_ARG(frame, ex_slice, uniform);
	EX_ARG(frame, u32, x);
	EX_ARG(frame, u32, y);
	EX_ARG(frame, u32, z);
	char define[64];
	if (!shader) {
		panic(frame, "Invalid shader");
		return;
	}
	if (!readDefine(define_view, define)) {
		panic(frame, "Invalid shader define");
		return;
	}
	if (!shader->isReady()) return;
	if (uniform.length < 0 || (uniform.length > 0 && !uniform.data) || uniform.length > 4096) {
		panic(frame, "Invalid uniform data");
		return;
	}
	ctx->pipeline->setUniformRaw(Span<const u8>(uniform.data, (u32)uniform.length));
	ctx->pipeline->dispatch(*shader, x, y, z, define[0] ? define : nullptr);
	flushWrites(*ctx);
}

// Clears the targets set by setRenderTarget / setRenderTargetWithDepth.
static void scriptClearTargets(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, flags);
	EX_ARG(frame, float, r);
	EX_ARG(frame, float, g);
	EX_ARG(frame, float, b);
	EX_ARG(frame, float, a);
	EX_ARG(frame, float, depth);
	ctx->pipeline->clear((gpu::ClearFlags)flags, r, g, b, a, depth);
}

static void scriptClearColor(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, ctx->pipeline->getClearColor());
}

static void scriptShadowAtlasBindless(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, ctx->pipeline->getShadowAtlasBindless().value);
}

static void scriptReflectionProbesBindless(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	RenderModule* module = ctx->pipeline->getModule();
	EX_RESULT(frame, module ? gpu::getBindlessHandle(module->getReflectionProbesTexture()).value : gpu::INVALID_BINDLESS_HANDLE.value);
}

// gpu::getStencilStateBits, the result goes to `drawState`
static void scriptStencilState(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, u32, write_mask);
	EX_ARG(frame, u32, func);
	EX_ARG(frame, u32, ref);
	EX_ARG(frame, u32, mask);
	EX_ARG(frame, u32, sfail);
	EX_ARG(frame, u32, dpfail);
	EX_ARG(frame, u32, dppass);
	if (func > (u32)gpu::StencilFuncs::NOT_EQUAL || sfail > (u32)gpu::StencilOps::INVERT || dpfail > (u32)gpu::StencilOps::INVERT || dppass > (u32)gpu::StencilOps::INVERT) {
		panic(frame, "Invalid stencil state");
		return;
	}
	const gpu::StateFlags state = gpu::getStencilStateBits((u8)write_mask, (gpu::StencilFuncs)func, (u8)ref, (u8)mask, (gpu::StencilOps)sfail, (gpu::StencilOps)dpfail, (gpu::StencilOps)dppass);
	EX_TYPED_RESULT(frame, u64, (u64)state);
}

// Camera handles are indices + 1 into the cameras of the context, valid during the current script call only.
static bool pushCamera(EvoxRenderContext& ctx, ex_call_frame& frame, const CameraParams& camera, u32& handle) {
	if (ctx.camera_count == lengthOf(ctx.cameras)) {
		panic(frame, "Too many cameras");
		return false;
	}
	ctx.cameras[ctx.camera_count++] = camera;
	handle = ctx.camera_count;
	return true;
}

static const CameraParams* readCamera(EvoxRenderContext& ctx, ex_call_frame& frame, u32 handle) {
	if (handle == 0 || handle > ctx.camera_count) {
		panic(frame, "Invalid camera");
		return nullptr;
	}
	return &ctx.cameras[handle - 1];
}

static void scriptMainCamera(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	u32 handle;
	if (!pushCamera(*ctx, frame, ctx->pipeline->getMainCamera(), handle)) return;
	EX_RESULT(frame, handle);
}

static void scriptPass(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, camera_handle);
	const CameraParams* camera = readCamera(*ctx, frame, camera_handle);
	if (!camera) return;
	ctx->pipeline->pass(*camera);
}

static void scriptViewport(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, x);
	EX_ARG(frame, u32, y);
	EX_ARG(frame, u32, w);
	EX_ARG(frame, u32, h);
	ctx->pipeline->viewport((i32)x, (i32)y, (i32)w, (i32)h);
}

// up to 4 color targets and an optional depth buffer
static void scriptSetRenderTargets(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, ex_slice, colors);
	EX_ARG(frame, u32, depth);
	EX_ARG(frame, u32, flags);
	if (colors.length < 0 || colors.length > 4 || (colors.length > 0 && !colors.data)) {
		panic(frame, "Invalid render targets");
		return;
	}
	RenderBufferHandle handles[4];
	const u32* scripts = (const u32*)colors.data;
	for (i64 i = 0; i < colors.length; ++i) {
		if (scripts[i] == 0) {
			panic(frame, "Invalid render buffer");
			return;
		}
		handles[i] = fromScript(scripts[i]);
	}
	const RenderBufferHandle ds = depth == 0 ? INVALID_RENDERBUFFER : fromScript(depth);
	ctx->pipeline->getRenderer().setRenderTargets(Span<const RenderBufferHandle>(handles, (u32)colors.length), ds, (gpu::FramebufferFlags)flags);
}

// Shadow map and the culled view of the stage (transparentPass).
static void scriptRenderBucket(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, view);
	EX_ARG(frame, u32, bucket);
	if (bucket >= ctx->pipeline->getBucketCount(view)) {
		panic(frame, "Invalid view or bucket");
		return;
	}
	ctx->pipeline->renderBucket(view, bucket);
}

// Binds uniform data to DRAWCALL (0) or DRAWCALL2 (1), the shader reads it from b4 / b5.
static void scriptSetUniformRaw(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, slot);
	EX_ARG(frame, ex_slice, uniform);
	if (slot > 1 || uniform.length < 0 || (uniform.length > 0 && !uniform.data) || uniform.length > 4096) {
		panic(frame, "Invalid uniform");
		return;
	}
	ctx->pipeline->setUniformRaw(Span<const u8>(uniform.data, (u32)uniform.length), slot == 0 ? UniformBuffer::DRAWCALL : UniformBuffer::DRAWCALL2);
}

static void scriptShadowCamera(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, slice);
	if (slice >= 4) {
		panic(frame, "Invalid shadow slice");
		return;
	}
	u32 handle;
	if (!pushCamera(*ctx, frame, ctx->pipeline->getShadowCamera(slice), handle)) return;
	EX_RESULT(frame, handle);
}

// Buckets are collected with addBucket and consumed by cull, strings are copied.
static void scriptAddBucket(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_STRING_ARG(frame, layer);
	EX_STRING_ARG(frame, define);
	EX_ARG(frame, u32, sort);
	EX_ARG(frame, u64, state);
	if (ctx->bucket_count == lengthOf(ctx->buckets)) {
		panic(frame, "Too many buckets");
		return;
	}
	auto& bucket = ctx->buckets[ctx->bucket_count];
	if (layer.length <= 0 || layer.length >= (i64)sizeof(bucket.layer) || define.length < 0 || define.length >= (i64)sizeof(bucket.define) || sort > (u32)BucketDesc::DEPTH) {
		panic(frame, "Invalid bucket");
		return;
	}
	memcpy(bucket.layer, layer.begin, layer.length);
	bucket.layer[layer.length] = '\0';
	if (define.length > 0) memcpy(bucket.define, define.begin, define.length);
	bucket.define[define.length] = '\0';
	bucket.sort = (BucketDesc::Sort)sort;
	bucket.state = (gpu::StateFlags)state;
	++ctx->bucket_count;
}

// Culls the scene for the camera into the buckets added since the last cull, returns the view for renderBucket.
static void scriptCull(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, camera_handle);
	const CameraParams* camera = readCamera(*ctx, frame, camera_handle);
	if (!camera) return;
	if (ctx->bucket_count == 0) {
		panic(frame, "No buckets to cull");
		return;
	}
	BucketDesc descs[8];
	static_assert(sizeof(ctx->buckets) / sizeof(ctx->buckets[0]) == lengthOf(descs));
	for (u32 i = 0; i < ctx->bucket_count; ++i) {
		descs[i] = {};
		descs[i].layer = ctx->buckets[i].layer;
		descs[i].sort = ctx->buckets[i].sort;
		descs[i].define = ctx->buckets[i].define[0] ? ctx->buckets[i].define : nullptr;
		descs[i].state = ctx->buckets[i].state;
	}
	const u32 count = ctx->bucket_count;
	ctx->bucket_count = 0;
	EX_RESULT(frame, ctx->pipeline->cull(*camera, Span<const BucketDesc>(descs, count)));
}

// space separated shader defines
static u32 defineMask(Renderer& renderer, ex_string_view defines) {
	u32 mask = 0;
	char name[64];
	u32 len = 0;
	for (i64 i = 0; i <= defines.length; ++i) {
		const char c = i < defines.length ? defines.begin[i] : ' ';
		if (c != ' ') {
			if (len < sizeof(name) - 1) name[len++] = c;
			continue;
		}
		if (len) {
			name[len] = '\0';
			mask |= 1u << renderer.getShaderDefineIdx(name);
			len = 0;
		}
	}
	return mask;
}

static void scriptRenderGrass(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, camera_handle);
	EX_ARG(frame, u64, state);
	EX_STRING_ARG(frame, defines);
	const CameraParams* camera = readCamera(*ctx, frame, camera_handle);
	if (!camera) return;
	ctx->pipeline->renderGrass(*camera, (gpu::StateFlags)state, defineMask(ctx->pipeline->getRenderer(), defines));
}

static void scriptRenderTerrains(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, camera_handle);
	EX_ARG(frame, u64, state);
	EX_STRING_ARG(frame, define_view);
	char define[64];
	if (!readDefine(define_view, define)) {
		panic(frame, "Invalid shader define");
		return;
	}
	const CameraParams* camera = readCamera(*ctx, frame, camera_handle);
	if (!camera) return;
	ctx->pipeline->renderTerrains(*camera, (gpu::StateFlags)state, define[0] ? define : nullptr);
}

// Draws the meshes of only the given entities (`renderEntities` in core:render), entities of other worlds or invalid ones are skipped.
static void scriptRenderEntities(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u64, state);
	EX_STRING_ARG(frame, define_view);
	EX_ARG(frame, ex_slice, entities);
	char define[64];
	if (!readDefine(define_view, define)) {
		panic(frame, "Invalid shader define");
		return;
	}
	if (entities.length < 0 || (entities.length > 0 && !entities.data)) {
		panic(frame, "Invalid entities");
		return;
	}
	// layout of core:entity.Entity
	struct ScriptEntity {
		i32 index;
		u32 padding;
		World* world;
	};
	World& world = ctx->pipeline->getModule()->getWorld();
	const ScriptEntity* script_entities = (const ScriptEntity*)entities.data;
	Array<EntityRef> list(ctx->pipeline->getRenderer().getAllocator());
	for (i64 i = 0; i < entities.length; ++i) {
		const ScriptEntity& entity = script_entities[i];
		if (entity.world != &world || entity.index < 0 || !world.hasEntity(EntityRef{entity.index})) continue;
		list.push(EntityRef{entity.index});
	}
	ctx->pipeline->renderEntities(Span<const EntityRef>(list.begin(), list.size()), (gpu::StateFlags)state, define[0] ? define : nullptr);
}

// Makes writes to the buffer visible to later reads (e.g. after rendering into a depth buffer that is sampled next)
static void scriptBarrierRead(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, rb);
	if (rb == 0) {
		panic(frame, "Invalid render buffer");
		return;
	}
	Renderer& renderer = ctx->pipeline->getRenderer();
	renderer.getDrawStream().barrier(renderer.toTexture(fromScript(rb)), gpu::BarrierType::READ);
}

// Render buffer of any format; `flags` are gpu::TextureFlags. Depth and color formats are both allowed, sizes up to 16384.
static void scriptCreateRenderbuffer(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, width);
	EX_ARG(frame, u32, height);
	EX_ARG(frame, u32, format);
	EX_ARG(frame, u32, flags);
	const u32 allowed = (u32)(gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::COMPUTE_WRITE | gpu::TextureFlags::RENDER_TARGET);
	if (width == 0 || height == 0 || width > 16384 || height > 16384 || (flags & ~allowed) != 0 || format > (u32)gpu::TextureFormat::RG16F || (format >= (u32)gpu::TextureFormat::BC1 && format <= (u32)gpu::TextureFormat::BC5)) {
		panic(frame, "Invalid render buffer size, format or flags");
		return;
	}
	const RenderBufferHandle rb = ctx->pipeline->getRenderer().createRenderbuffer({
		.size = IVec2((i32)width, (i32)height),
		.format = (gpu::TextureFormat)format,
		.flags = (gpu::TextureFlags)flags,
		.debug_name = "evox"
	});
	trackCreated(*ctx, rb);
	EX_RESULT(frame, toScript(rb));
}

// gpu::getBlendStateBits, the result goes to bucket / draw states
static void scriptBlendState(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, u32, src_rgb);
	EX_ARG(frame, u32, dst_rgb);
	EX_ARG(frame, u32, src_a);
	EX_ARG(frame, u32, dst_a);
	if (src_rgb > (u32)gpu::BlendFactors::ONE_MINUS_SRC1_ALPHA || dst_rgb > (u32)gpu::BlendFactors::ONE_MINUS_SRC1_ALPHA || src_a > (u32)gpu::BlendFactors::ONE_MINUS_SRC1_ALPHA || dst_a > (u32)gpu::BlendFactors::ONE_MINUS_SRC1_ALPHA) {
		panic(frame, "Invalid blend state");
		return;
	}
	const gpu::StateFlags state = gpu::getBlendStateBits((gpu::BlendFactors)src_rgb, (gpu::BlendFactors)dst_rgb, (gpu::BlendFactors)src_a, (gpu::BlendFactors)dst_a);
	EX_TYPED_RESULT(frame, u64, (u64)state);
}

// Which built-in debug view the pipeline shows, Pipeline::DebugShow
static void scriptDebugShow(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, (u32)ctx->pipeline->m_debug_show);
}

// Copies channels of src to dst: each output channel is the dot product of the source texel with the given mask (and
// an offset of 0 for color and 1 for alpha), see Pipeline::copy.
static void scriptCopyChannels(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, src);
	EX_ARG(frame, u32, dst);
	EX_ARG(frame, u32, width);
	EX_ARG(frame, u32, height);
	EX_ARG(frame, Vec4, r);
	EX_ARG(frame, Vec4, g);
	EX_ARG(frame, Vec4, b);
	if (src == 0 || dst == 0) {
		panic(frame, "Invalid render buffer");
		return;
	}
	ctx->pipeline->copy(fromScript(dst), fromScript(src), IVec2((i32)width, (i32)height), r, g, b);
}

// Natives of the frame script (`main` of render.evox), which calls the stages, effects and C++ plugins itself.

static void scriptBeginFrame(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	ctx->pipeline->beginFrame3D();
	ctx->frame_begun = true;
}

static void scriptEndFrame(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	ctx->pipeline->endFrame3D();
	ctx->frame_begun = false;
}

static void scriptBindGlobalState(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, shadowmap);
	if (shadowmap == 0) {
		panic(frame, "Invalid render buffer");
		return;
	}
	ctx->pipeline->bindGlobalState(fromScript(shadowmap));
}

static void scriptRenderDebugShapes(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, output);
	EX_ARG(frame, u32, depth);
	if (output == 0 || depth == 0) {
		panic(frame, "Invalid render buffer");
		return;
	}
	ctx->pipeline->renderDebugShapes(fromScript(output), fromScript(depth));
}

static void scriptRender2D(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, output);
	if (output == 0) {
		panic(frame, "Invalid render buffer");
		return;
	}
	ctx->pipeline->render2D(fromScript(output));
}

// The G-buffer the effects called by the script see (`gbuffer`, `depth`) and the plugins get.
static void scriptSetGBuffer(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, a);
	EX_ARG(frame, u32, b);
	EX_ARG(frame, u32, c);
	EX_ARG(frame, u32, d);
	EX_ARG(frame, u32, ds);
	if (!a || !b || !c || !d || !ds) {
		panic(frame, "Invalid G-buffer");
		return;
	}
	ctx->gbuffer_storage = {fromScript(a), fromScript(b), fromScript(c), fromScript(d), fromScript(ds)};
	ctx->gbuffer = &ctx->gbuffer_storage;
}

// Stages of the frame the C++ plugins run in: values of core:render.PluginStage
enum class PluginStage : u32 {
	BEFORE_LIGHT_PASS,
	BEFORE_TRANSPARENT,
	ANTIALIAS,
	BEFORE_TONEMAP,
	TONEMAP,
	AFTER_TONEMAP,
	RENDER_OPAQUE,
	RENDER_TRANSPARENT,
	COUNT
};

// Runs the C++ plugins of a stage (RENDER_OPAQUE and RENDER_TRANSPARENT draw geometry, they have no image). Chaining stages return the final image, the others the
// image of the first plugin that did the stage, an invalid buffer if no plugin did.
static void scriptRunPlugins(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, stage);
	EX_ARG(frame, u32, input_handle);
	if (stage >= (u32)PluginStage::COUNT) {
		panic(frame, "Invalid stage");
		return;
	}
	const PluginStage hook = (PluginStage)stage;
	const bool needs_gbuffer = hook != PluginStage::TONEMAP && hook != PluginStage::RENDER_OPAQUE && hook != PluginStage::RENDER_TRANSPARENT;
	if (needs_gbuffer && !ctx->gbuffer) {
		panic(frame, "Set the G-buffer first");
		return;
	}
	const RenderBufferHandle input = input_handle == 0 ? INVALID_RENDERBUFFER : fromScript(input_handle);
	RenderBufferHandle result = INVALID_RENDERBUFFER;
	Pipeline& pipeline = *ctx->pipeline;
	for (RenderPlugin* plugin : pipeline.getRenderer().getPlugins()) {
		if (plugin == ctx->plugin) continue;
		switch (hook) {
			case PluginStage::BEFORE_LIGHT_PASS: plugin->renderBeforeLightPass(*ctx->gbuffer, pipeline); break;
			case PluginStage::BEFORE_TRANSPARENT:
				result = plugin->renderBeforeTransparent(*ctx->gbuffer, result == INVALID_RENDERBUFFER ? input : result, pipeline);
				break;
			case PluginStage::BEFORE_TONEMAP:
				result = plugin->renderBeforeTonemap(*ctx->gbuffer, result == INVALID_RENDERBUFFER ? input : result, pipeline);
				break;
			case PluginStage::AFTER_TONEMAP:
				result = plugin->renderAfterTonemap(*ctx->gbuffer, result == INVALID_RENDERBUFFER ? input : result, pipeline);
				break;
			case PluginStage::RENDER_OPAQUE: plugin->renderOpaque(pipeline); break;
			case PluginStage::RENDER_TRANSPARENT: plugin->renderTransparent(pipeline); break;
			case PluginStage::ANTIALIAS: result = plugin->renderAA(*ctx->gbuffer, input, pipeline); break;
			case PluginStage::TONEMAP: {
				RenderBufferHandle tonemapped;
				if (plugin->tonemap(input, tonemapped, pipeline)) result = tonemapped;
				break;
			}
			default: break;
		}
		// the first plugin that did antialiasing / tonemapping wins
		if ((hook == PluginStage::ANTIALIAS || hook == PluginStage::TONEMAP) && result != INVALID_RENDERBUFFER) break;
	}
	EX_RESULT(frame, result == INVALID_RENDERBUFFER ? 0u : toScript(result));
}

// true if a C++ plugin drew its debug view into the image
static void scriptRunPluginsDebugOutput(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, image);
	if (image == 0) {
		panic(frame, "Invalid render buffer");
		return;
	}
	bool shown = false;
	for (RenderPlugin* plugin : ctx->pipeline->getRenderer().getPlugins()) {
		if (plugin == ctx->plugin) continue;
		if (plugin->debugOutput(fromScript(image), *ctx->pipeline)) {
			shown = true;
			break;
		}
	}
	EX_RESULT(frame, u8(shown ? 1 : 0));
}

// Draws a fullscreen triangle with a surface shader (`mainVS`/`mainPS`) into the current render target. The state (depth,
// stencil, blending) comes from the `state` argument.
static void scriptDrawState(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, Shader*, shader);
	EX_STRING_ARG(frame, define_view);
	EX_ARG(frame, u64, state);
	EX_ARG(frame, ex_slice, uniform);
	char define[64];
	if (!shader) {
		panic(frame, "Invalid shader");
		return;
	}
	if (!readDefine(define_view, define)) {
		panic(frame, "Invalid shader define");
		return;
	}
	if (!shader->isReady()) return;
	if (uniform.length < 0 || (uniform.length > 0 && !uniform.data) || uniform.length > 4096) {
		panic(frame, "Invalid uniform data");
		return;
	}
	const u32 define_mask = define[0] ? 1u << ctx->pipeline->getRenderer().getShaderDefineIdx(define) : 0;
	ctx->pipeline->setUniformRaw(Span<const u8>(uniform.data, (u32)uniform.length));
	ctx->pipeline->drawArray(0, 3, *shader, define_mask, (gpu::StateFlags)state);
}


// Makes the debug view of the pipeline show the output called `name` (see `debugOutput`). The names only have to be
// unique among the scripts.
static void scriptShowDebug(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_STRING_ARG(frame, name);
	if (name.length < 0 || (name.length > 0 && !name.begin)) {
		panic(frame, "Invalid debug name");
		return;
	}
	ctx->plugin->showDebug(*ctx->pipeline, StringView(name.begin, (u64)name.length));
}

static void scriptIsDebugShown(ex_runtime*, ex_call_frame frame) {
	EvoxRenderContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_STRING_ARG(frame, name);
	if (name.length < 0 || (name.length > 0 && !name.begin)) {
		panic(frame, "Invalid debug name");
		return;
	}
	EX_RESULT(frame, u8(ctx->plugin->isDebugShown(*ctx->pipeline, StringView(name.begin, (u64)name.length)) ? 1 : 0));
}

} // anonymous namespace

void registerRenderFunctions(HashMap<NativeFunctionKey, ex_native_fn, NativeFunctionKeyHash>& functions) {
	functions.insert({"core:render", "gbufferRaw"}, &scriptGBuffer);
	functions.insert({"core:render", "builtinTaaEnabled"}, &scriptBuiltinTaaEnabled);
	functions.insert({"core:render", "stateRaw"}, &scriptStateRaw);
	functions.insert({"core:render", "releaseBuffer"}, &scriptReleaseBuffer);
	functions.insert({"core:render", "blit"}, &scriptBlit);
	functions.insert({"core:render", "depth"}, &scriptDepth);
	functions.insert({"core:render", "renderModule"}, &scriptRenderModule);
	functions.insert({"core:render", "pipelineType"}, &scriptPipelineType);
	functions.insert({"core:render", "width"}, &scriptWidth);
	functions.insert({"core:render", "displayWidth"}, &scriptDisplayWidth);
	functions.insert({"core:render", "displayHeight"}, &scriptDisplayHeight);
	functions.insert({"core:render", "enablePixelJitter"}, &scriptEnablePixelJitter);
	functions.insert({"core:render", "height"}, &scriptHeight);
	functions.insert({"core:render", "loadShader"}, &scriptLoadShader);
	functions.insert({"core:render", "isReady"}, &scriptIsReady);
	functions.insert({"core:render", "bindless"}, &scriptBindless);
	functions.insert({"core:render", "rwBindless"}, &scriptRWBindless);
	functions.insert({"core:render", "beginBlockRaw"}, &scriptBeginBlock);
	functions.insert({"core:render", "endBlock"}, &scriptEndBlock);
	functions.insert({"core:render", "dispatchRaw"}, &scriptDispatch);
	functions.insert({"core:render", "drawStateRaw"}, &scriptDrawState);
	functions.insert({"core:render", "clearTargets"}, &scriptClearTargets);
	functions.insert({"core:render", "clearColor"}, &scriptClearColor);
	functions.insert({"core:render", "shadowAtlasBindless"}, &scriptShadowAtlasBindless);
	functions.insert({"core:render", "reflectionProbesBindless"}, &scriptReflectionProbesBindless);
	functions.insert({"core:render", "stencilState"}, &scriptStencilState);
	functions.insert({"core:render", "mainCamera"}, &scriptMainCamera);
	functions.insert({"core:render", "beginFrame"}, &scriptBeginFrame);
	functions.insert({"core:render", "endFrame"}, &scriptEndFrame);
	functions.insert({"core:render", "bindGlobalState"}, &scriptBindGlobalState);
	functions.insert({"core:render", "renderDebugShapes"}, &scriptRenderDebugShapes);
	functions.insert({"core:render", "render2D"}, &scriptRender2D);
	functions.insert({"core:render", "setGBuffer"}, &scriptSetGBuffer);
	functions.insert({"core:render", "runPlugins"}, &scriptRunPlugins);
	functions.insert({"core:render", "runPluginsDebugOutput"}, &scriptRunPluginsDebugOutput);
	functions.insert({"core:render", "debugShow"}, &scriptDebugShow);
	functions.insert({"core:render", "copyChannels"}, &scriptCopyChannels);
	functions.insert({"core:render", "createRenderbuffer"}, &scriptCreateRenderbuffer);
	functions.insert({"core:render", "blendState"}, &scriptBlendState);
	functions.insert({"core:render", "shadowCamera"}, &scriptShadowCamera);
	functions.insert({"core:render", "addBucket"}, &scriptAddBucket);
	functions.insert({"core:render", "cull"}, &scriptCull);
	functions.insert({"core:render", "renderGrass"}, &scriptRenderGrass);
	functions.insert({"core:render", "renderTerrains"}, &scriptRenderTerrains);
	functions.insert({"core:render", "renderEntities"}, &scriptRenderEntities);
	functions.insert({"core:render", "barrierRead"}, &scriptBarrierRead);
	functions.insert({"core:render", "pass"}, &scriptPass);
	functions.insert({"core:render", "viewport"}, &scriptViewport);
	functions.insert({"core:render", "setRenderTargetsRaw"}, &scriptSetRenderTargets);
	functions.insert({"core:render", "renderBucket"}, &scriptRenderBucket);
	functions.insert({"core:render", "setUniformRaw"}, &scriptSetUniformRaw);
	functions.insert({"core:render", "createStorage"}, &scriptCreateStorage);
	functions.insert({"core:render", "releaseStorage"}, &scriptReleaseStorage);
	functions.insert({"core:render", "bindlessStorage"}, &scriptBindlessStorage);
	functions.insert({"core:render", "rwBindlessStorage"}, &scriptRWBindlessStorage);
	functions.insert({"core:render", "showDebug"}, &scriptShowDebug);
	functions.insert({"core:render", "isDebugShown"}, &scriptIsDebugShown);
}

} // namespace Evox

} // namespace Lumix
