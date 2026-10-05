#include "evox/evox_postprocess.h"
#include "../../external/evox/arena.h"
#include "core/log.h"
#include "core/profiler.h"
#include "engine/engine.h"
#include "engine/resource_manager.h"
#include "evox/capi.h"
#include "evox/evox_resource.h"
#include "renderer/pipeline.h"
#include "renderer/render_module.h"
#include "renderer/shader.h"
#include <string.h>

namespace Lumix {

static constexpr const char* ROOT_PATH = EVOX_POSTPROCESS_PATH;
static constexpr const char* ENTRY_FUNCTION = "main";
static constexpr const char* EFFECT_TYPE = "core:postprocess.Postprocess";
static constexpr const char* RENDER_BUFFER_TYPE = "core:postprocess.RenderBuffer";
static constexpr const char* STORAGE_TYPE = "core:postprocess.Storage";

// Scripts see render buffers as handle + 1, so that zero (e.g. in zero-initialized state) is an invalid buffer.
static u32 toScript(RenderBufferHandle handle) { return u32(handle) + 1; }
static RenderBufferHandle fromScript(u32 handle) { return RenderBufferHandle(handle - 1); }
// field names of core:postprocess.Postprocess
static constexpr const char* HOOK_FIELDS[] = {
	"beforeLightPass",
	"beforeTransparent",
	"antialias",
	"beforeTonemap",
	"tonemap",
	"afterTonemap",
	"debugUI",
	"debugOutput",
};
static_assert(lengthOf(HOOK_FIELDS) == EvoxPostprocess::HOOK_COUNT);

// Native side of core:postprocess.Context, valid only for the duration of a single script call.
struct EvoxPostprocessContext {
	EvoxPostprocess* plugin;
	Pipeline* pipeline;
	// null in hooks that don't have it (tonemap)
	const GBuffer* gbuffer;
	RenderBufferHandle input;
	// buffers handed out for writing (rwBindless) since the last dispatch, they get a memory barrier after it
	RenderBufferHandle written[8];
	u32 written_count;
	gpu::BufferHandle written_storage[4];
	u32 written_storage_count;
	// what the last hook returned: a script render buffer handle (0 if none), or 0 / 1 for debugOutput
	u32 result;
};

static ex_string_view toEvox(const char* value) {
	return {value, (i64)strlen(value)};
}

static void logCallFailure(const char* what, ex_task* task, ex_call_result result) {
	logError("Evox postprocess ", what, " failed (result ", (int)result, ")");
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

EvoxPostprocess::EvoxPostprocess(Engine& engine, IAllocator& allocator)
	: m_engine(engine)
	, m_allocator(allocator)
	, m_effects(allocator)
	, m_state(allocator)
	, m_pending_release(allocator)
	, m_storage(allocator)
	, m_pending_destroy(allocator)
	, m_shaders(allocator)
	, m_debug_selection(allocator)
{}

void EvoxPostprocess::detach() {
	m_effects.clear();
	// state types belong to the bytecode
	for (StateBlock& block : m_state) freeState(block);
	m_state.clear();
	for (gpu::BufferHandle& buffer : m_storage) {
		if (buffer != gpu::INVALID_BUFFER) m_pending_destroy.push(buffer);
	}
	m_storage.clear();
	m_debug_selection.clear();
	m_effect_type = nullptr;
	if (m_task) { ex_task_destroy(m_task); m_task = nullptr; }
	m_runtime = nullptr;
	m_bytecode = nullptr;
}

void EvoxPostprocess::attach(ex_bytecode* bytecode, ex_runtime* runtime) {
	detach();
	if (!bytecode || !runtime) return;
	if (ex_runtime_find_function(runtime, toEvox(ROOT_PATH), toEvox(ENTRY_FUNCTION)) < 0) return;
	m_bytecode = bytecode;
	m_runtime = runtime;
	m_task = ex_task_create(m_runtime);
	if (!m_task) {
		detach();
		return;
	}
	runMain();
}

void EvoxPostprocess::recreateTask() {
	if (m_task) ex_task_destroy(m_task);
	m_task = m_runtime ? ex_task_create(m_runtime) : nullptr;
}

// Calls `main(registry)` of the script, which registers the effects.
void EvoxPostprocess::runMain() {
	const i32 entry = ex_runtime_find_function(m_runtime, toEvox(ROOT_PATH), toEvox(ENTRY_FUNCTION));
	if (entry < 0 || ex_function_result_kind(m_runtime, (u32)entry) != EX_TYPE_VOID) {
		logError(ROOT_PATH, " does not define `fn ", ENTRY_FUNCTION, "(registry : Registry) : void`");
		return;
	}

	// find the effect struct, scripts that don't use it can't register anything
	m_effect_type = nullptr;
	for (u32 i = 0, count = ex_bytecode_type_count(m_bytecode); i < count; ++i) {
		const ex_type* type = ex_bytecode_type(m_bytecode, i);
		if (ex_type_get_kind(type) != EX_TYPE_STRUCT) continue;
		const ex_string_view name = ex_type_get_name(type);
		if (StringView(name.begin, (u64)name.length) == EFFECT_TYPE) {
			m_effect_type = type;
			break;
		}
	}
	for (i32& f : m_field_of_hook) f = -1;
	if (m_effect_type) {
		for (u32 field = 0, count = ex_type_struct_field_count(m_effect_type); field < count; ++field) {
			const ex_string_view name = ex_type_struct_field_name(m_effect_type, field);
			for (u32 hook = 0; hook < HOOK_COUNT; ++hook) {
				if (StringView(name.begin, (u64)name.length) == HOOK_FIELDS[hook]) m_field_of_hook[hook] = (i32)field;
			}
		}
	}

	EvoxPostprocess* registry = this;
	const ex_call_result result = ex_call_function(m_task, (u32)entry, &registry, sizeof(registry));
	if (result != EX_CALL_RESULT_OK) {
		logCallFailure(ENTRY_FUNCTION, m_task, result);
		recreateTask();
	}
}

void EvoxPostprocess::addEffect(const void* memory) {
	const u8* effect = (const u8*)memory;
	Effect& out = m_effects.emplace();
	for (u32 hook = 0; hook < HOOK_COUNT; ++hook) {
		out.functions[hook] = INVALID_FUNCTION;
		if (m_field_of_hook[hook] < 0) continue;

		const ex_type* field_type = ex_type_struct_field_type(m_effect_type, m_field_of_hook[hook]);
		if (ex_type_get_kind(field_type) != EX_TYPE_NULLABLE) continue;
		const void* value = effect + ex_type_struct_field_offset(m_effect_type, m_field_of_hook[hook]);
		if (ex_type_nullable_is_null(field_type, value)) continue;

		// a function value is the index of the function in the bytecode
		const ex_type* inner_type = ex_type_nullable_inner_type(field_type);
		if (ex_type_get_size(inner_type) != sizeof(u32)) continue;
		memcpy(&out.functions[hook], ex_type_nullable_value_ptr(field_type, value), sizeof(u32));
	}
}

u8* EvoxPostprocess::getState(Pipeline& pipeline, u32 type_index, u32& size) {
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

void EvoxPostprocess::queueBuffersForRelease(const ex_type* type, const u8* memory) {
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

void EvoxPostprocess::freeState(StateBlock& block) {
	queueBuffersForRelease(block.type, block.memory);
	m_allocator.deallocate(block.memory);
	block.memory = nullptr;
}

u32 EvoxPostprocess::createStorage(Renderer& renderer, u32 size) {
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

gpu::BufferHandle EvoxPostprocess::getStorage(u32 id) const {
	if (id == 0 || id > (u32)m_storage.size()) return gpu::INVALID_BUFFER;
	return m_storage[id - 1];
}

void EvoxPostprocess::releaseStorage(u32 id) {
	if (id == 0 || id > (u32)m_storage.size()) return;
	if (m_storage[id - 1] == gpu::INVALID_BUFFER) return;
	m_pending_destroy.push(m_storage[id - 1]);
	m_storage[id - 1] = gpu::INVALID_BUFFER;
}

void EvoxPostprocess::destroyPending(Renderer& renderer) {
	for (RenderBufferHandle handle : m_pending_release) renderer.releaseRenderbuffer(handle);
	m_pending_release.clear();
	for (gpu::BufferHandle buffer : m_pending_destroy) renderer.getEndFrameDrawStream().destroy(buffer);
	m_pending_destroy.clear();
}

void EvoxPostprocess::pipelineDestroyed(Pipeline& pipeline) {
	for (i32 i = m_debug_selection.size() - 1; i >= 0; --i) {
		if (m_debug_selection[i].pipeline == &pipeline) m_debug_selection.swapAndPop(i);
	}
	for (i32 i = m_state.size() - 1; i >= 0; --i) {
		if (m_state[i].pipeline != &pipeline) continue;
		freeState(m_state[i]);
		m_state.swapAndPop(i);
	}
}

Shader* EvoxPostprocess::loadShader(StringView path) {
	const Path shader_path(path);
	for (const ShaderEntry& entry : m_shaders) {
		if (entry.path == shader_path) return entry.shader;
	}
	Shader* shader = m_engine.getResourceManager().load<Shader>(shader_path);
	if (shader) m_shaders.push({shader_path, shader});
	return shader;
}

void EvoxPostprocess::shutdown(Renderer& renderer) {
	detach();
	for (const ShaderEntry& entry : m_shaders) entry.shader->decRefCount();
	m_shaders.clear();
	destroyPending(renderer);
}

void EvoxPostprocess::renderBeforeLightPass(const GBuffer& gbuffer, Pipeline& pipeline) {
	run(BEFORE_LIGHT_PASS, &gbuffer, INVALID_RENDERBUFFER, pipeline);
}

RenderBufferHandle EvoxPostprocess::renderBeforeTransparent(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) {
	const RenderBufferHandle output = run(BEFORE_TRANSPARENT, &gbuffer, input, pipeline);
	return output == INVALID_RENDERBUFFER ? input : output;
}

RenderBufferHandle EvoxPostprocess::renderAA(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) {
	return run(ANTIALIAS, &gbuffer, input, pipeline);
}

RenderBufferHandle EvoxPostprocess::renderBeforeTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) {
	const RenderBufferHandle output = run(BEFORE_TONEMAP, &gbuffer, input, pipeline);
	return output == INVALID_RENDERBUFFER ? input : output;
}

bool EvoxPostprocess::tonemap(RenderBufferHandle input, RenderBufferHandle& output, Pipeline& pipeline) {
	const RenderBufferHandle result = run(TONEMAP, nullptr, input, pipeline);
	if (result == INVALID_RENDERBUFFER) return false;
	output = result;
	return true;
}

RenderBufferHandle EvoxPostprocess::renderAfterTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) {
	const RenderBufferHandle output = run(AFTER_TONEMAP, &gbuffer, input, pipeline);
	return output == INVALID_RENDERBUFFER ? input : output;
}

RenderBufferHandle EvoxPostprocess::run(Hook hook, const GBuffer* gbuffer, RenderBufferHandle input, Pipeline& pipeline) {
	if (!m_pending_release.empty() || !m_pending_destroy.empty()) destroyPending(pipeline.getRenderer());
	if (!m_task) return INVALID_RENDERBUFFER;

	bool any = false;
	for (const Effect& effect : m_effects) any = any || effect.functions[hook] != INVALID_FUNCTION;
	if (!any) return INVALID_RENDERBUFFER;
	PROFILE_FUNCTION();

	EvoxPostprocessContext context = {this, &pipeline, gbuffer, input, {}, 0, {}, 0, 0};
	RenderBufferHandle output = INVALID_RENDERBUFFER;
	for (Effect& effect : m_effects) {
		if (effect.functions[hook] == INVALID_FUNCTION) continue;
		if (!callHook(effect, hook, context, context.input)) {
			if (!m_task) return output;
			continue;
		}
		if (hook == BEFORE_LIGHT_PASS || context.result == 0) continue;

		output = fromScript(context.result);
		// the first effect that did antialiasing / tonemapping wins, other effects chain
		if (hook == ANTIALIAS || hook == TONEMAP) break;
		context.input = output;
	}
	return output;
}

bool EvoxPostprocess::callHook(Effect& effect, Hook hook, EvoxPostprocessContext& context, RenderBufferHandle input) {
	const bool has_input = hook != BEFORE_LIGHT_PASS && hook != DEBUG_UI;
	// arguments are packed: Context (pointer) followed by RenderBuffer (u32)
	u8 args[sizeof(void*) + sizeof(u32)];
	EvoxPostprocessContext* context_ptr = &context;
	memcpy(args, &context_ptr, sizeof(context_ptr));
	u32 args_size = sizeof(context_ptr);
	if (has_input) {
		const u32 input_handle = toScript(input);
		memcpy(args + args_size, &input_handle, sizeof(input_handle));
		args_size += sizeof(input_handle);
	}

	context.result = 0;
	const ex_call_result result = ex_call_function(m_task, effect.functions[hook], args, args_size);
	if (result != EX_CALL_RESULT_OK) {
		// disable the hook until the script is reloaded, otherwise we'd spam the log every frame
		effect.functions[hook] = INVALID_FUNCTION;
		logCallFailure(HOOK_FIELDS[hook], m_task, result);
		recreateTask();
		return false;
	}
	if (hook == BEFORE_LIGHT_PASS || hook == DEBUG_UI) return true;

	u32 size = 0;
	const void* returned = ex_task_result(m_task, &size);
	if (hook == DEBUG_OUTPUT) {
		// bool
		if (returned && size == sizeof(u8)) context.result = *(const u8*)returned != 0 ? 1 : 0;
	}
	else if (returned && size == sizeof(u32)) {
		memcpy(&context.result, returned, sizeof(u32));
	}
	return true;
}

void EvoxPostprocess::showDebug(Pipeline& pipeline, StringView name) {
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

bool EvoxPostprocess::isDebugShown(Pipeline& pipeline, StringView name) const {
	if (pipeline.m_debug_show_plugin != this) return false;
	for (const DebugSelection& selection : m_debug_selection) {
		if (selection.pipeline == &pipeline) return StringView(selection.name) == name;
	}
	return false;
}

// Called from the debug popup of Studio's views. The hooks draw ImGui widgets and use ctx.showDebug / ctx.isDebugShown
// to switch the debug view to one of their buffers.
void EvoxPostprocess::debugUI(Pipeline& pipeline) {
	if (!m_task) return;
	EvoxPostprocessContext context = {this, &pipeline, nullptr, INVALID_RENDERBUFFER, {}, 0, {}, 0, 0};
	for (Effect& effect : m_effects) {
		if (effect.functions[DEBUG_UI] == INVALID_FUNCTION) continue;
		if (!callHook(effect, DEBUG_UI, context, INVALID_RENDERBUFFER) && !m_task) return;
	}
}

// `input` is the image of the pipeline, a hook that is the current debug view overwrites it and returns true.
bool EvoxPostprocess::debugOutput(RenderBufferHandle input, Pipeline& pipeline) {
	if (pipeline.m_debug_show_plugin != this || !m_task) return false;
	EvoxPostprocessContext context = {this, &pipeline, nullptr, input, {}, 0, {}, 0, 0};
	for (Effect& effect : m_effects) {
		if (effect.functions[DEBUG_OUTPUT] == INVALID_FUNCTION) continue;
		if (!callHook(effect, DEBUG_OUTPUT, context, input)) {
			if (!m_task) return false;
			continue;
		}
		if (context.result) return true;
	}
	return false;
}


namespace Evox {

namespace {

static void panic(ex_call_frame& frame, const char* message) {
	*frame.panic = {message, (i64)strlen(message)};
}

static EvoxPostprocessContext* readContext(ex_call_frame& frame) {
	EX_ARG(frame, EvoxPostprocessContext*, ctx);
	if (!ctx) panic(frame, "Invalid postprocess context");
	return ctx;
}

// Makes writes of the previous dispatch visible to whatever reads the buffers next.
static void flushWrites(EvoxPostprocessContext& ctx) {
	Renderer& renderer = ctx.pipeline->getRenderer();
	DrawStream& stream = renderer.getDrawStream();
	for (u32 i = 0; i < ctx.written_count; ++i) stream.memoryBarrier(renderer.toTexture(ctx.written[i]));
	ctx.written_count = 0;
	for (u32 i = 0; i < ctx.written_storage_count; ++i) stream.memoryBarrier(ctx.written_storage[i]);
	ctx.written_storage_count = 0;
}

// `registry` is the pointer passed to `main`, `effect` (core:postprocess.Postprocess) follows it in the arguments.
static void postprocessAdd(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, EvoxPostprocess*, plugin);
	if (!plugin || plugin->getEffectSize() == 0) {
		panic(frame, "Invalid postprocess registry");
		return;
	}
	plugin->addEffect(frame.args);
}

static void postprocessInput(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, toScript(ctx->input));
}

static void postprocessDepth(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	if (!ctx->gbuffer) {
		panic(frame, "Depth is not available in this hook");
		return;
	}
	EX_RESULT(frame, toScript(ctx->gbuffer->DS));
}

static void postprocessBuiltinTaaEnabled(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, u8(ctx->pipeline->getRenderer().isBuiltinTAAEnabled() ? 1 : 0));
}

static void postprocessGBuffer(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, index);
	if (!ctx->gbuffer || index > 3) {
		panic(frame, "G-buffer is not available in this hook");
		return;
	}
	const RenderBufferHandle buffers[] = {ctx->gbuffer->A, ctx->gbuffer->B, ctx->gbuffer->C, ctx->gbuffer->D};
	EX_RESULT(frame, toScript(buffers[index]));
}

static void postprocessClear(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, rb);
	EX_ARG(frame, float, r);
	EX_ARG(frame, float, g);
	EX_ARG(frame, float, b);
	EX_ARG(frame, float, a);
	if (rb == 0) {
		panic(frame, "Invalid render buffer");
		return;
	}
	const RenderBufferHandle handle = fromScript(rb);
	ctx->pipeline->getRenderer().setRenderTargets(Span(&handle, 1));
	ctx->pipeline->clear(gpu::ClearFlags::ALL, r, g, b, a, 0);
}

static void postprocessRenderModule(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	RenderModule* module = ctx->pipeline->getModule();
	EX_RESULT(frame, module);
}

static void postprocessPipelineType(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, (i32)ctx->pipeline->getType());
}

static void postprocessWidth(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, u32(ctx->pipeline->getViewport().w));
}

static void postprocessHeight(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, u32(ctx->pipeline->getViewport().h));
}

static void postprocessDisplayWidth(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, u32(ctx->pipeline->getDisplaySize().x));
}

static void postprocessDisplayHeight(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_RESULT(frame, u32(ctx->pipeline->getDisplaySize().y));
}

static void postprocessEnablePixelJitter(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u8, enable);
	ctx->pipeline->enablePixelJitter(enable != 0);
}

static void postprocessLoadShader(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
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

static void postprocessIsReady(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, Shader*, shader);
	EX_RESULT(frame, u8(shader && shader->isReady() ? 1 : 0));
}

static void postprocessBindless(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, rb);
	DrawStream& stream = ctx->pipeline->getRenderer().getDrawStream();
	EX_RESULT(frame, ctx->pipeline->toBindless(fromScript(rb), stream).value);
}

static void postprocessRWBindless(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
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

static void postprocessBeginBlock(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_STRING_ARG(frame, name);
	char tmp[64];
	const u32 len = name.length > 0 ? (u32)(name.length < (i64)sizeof(tmp) - 1 ? name.length : (i64)sizeof(tmp) - 1) : 0;
	if (len) memcpy(tmp, name.begin, len);
	tmp[len] = '\0';
	ctx->pipeline->beginBlock(tmp);
}

static void postprocessEndBlock(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	ctx->pipeline->endBlock();
}

static void postprocessStateRaw(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
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

// Formats that make sense for a color buffer written by compute shaders.
static bool isSupportedFormat(gpu::TextureFormat format) {
	switch (format) {
		case gpu::TextureFormat::R8:
		case gpu::TextureFormat::RG8:
		case gpu::TextureFormat::RGBA8:
		case gpu::TextureFormat::R16:
		case gpu::TextureFormat::RG16:
		case gpu::TextureFormat::RGBA16:
		case gpu::TextureFormat::R16F:
		case gpu::TextureFormat::RG16F:
		case gpu::TextureFormat::RGBA16F:
		case gpu::TextureFormat::R32F:
		case gpu::TextureFormat::RG32F:
		case gpu::TextureFormat::RGBA32F:
		case gpu::TextureFormat::R11G11B10F:
			return true;
		default:
			return false;
	}
}

static void createBufferImpl(ex_call_frame& frame, gpu::TextureFlags flags) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, width);
	EX_ARG(frame, u32, height);
	// core:renderer/gpu/textureformat.TextureFormat is generated from gpu::TextureFormat
	EX_ARG(frame, u32, format);
	const gpu::TextureFormat gpu_format = (gpu::TextureFormat)format;
	if (width == 0 || height == 0 || width > 16384 || height > 16384 || !isSupportedFormat(gpu_format)) {
		panic(frame, "Invalid render buffer size or format");
		return;
	}
	const RenderBufferHandle rb = ctx->pipeline->getRenderer().createRenderbuffer({
		.size = IVec2((i32)width, (i32)height),
		.format = gpu_format,
		.flags = flags,
		.debug_name = "evox"
	});
	EX_RESULT(frame, toScript(rb));
}

// compute shaders can write to it, it can't be a render target (some formats don't work as both)
static void postprocessCreateBuffer(ex_runtime*, ex_call_frame frame) {
	createBufferImpl(frame, gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::COMPUTE_WRITE);
}

// like a buffer, but can also be cleared and drawn into
static void postprocessCreateRenderTarget(ex_runtime*, ex_call_frame frame) {
	createBufferImpl(frame, gpu::TextureFlags::RENDER_TARGET | gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::COMPUTE_WRITE);
}

static void postprocessReleaseBuffer(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, rb);
	ctx->pipeline->getRenderer().releaseRenderbuffer(fromScript(rb));
}

static void postprocessBlit(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
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

static void postprocessCreateStorage(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, size);
	if (size == 0 || size > 64 * 1024 * 1024) {
		panic(frame, "Invalid storage size");
		return;
	}
	EX_RESULT(frame, ctx->plugin->createStorage(ctx->pipeline->getRenderer(), size));
}

static void postprocessReleaseStorage(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, id);
	ctx->plugin->releaseStorage(id);
}

static void postprocessBindlessStorage(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
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

static void postprocessRWBindlessStorage(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
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

static void postprocessSetRenderTarget(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, u32, rb);
	if (rb == 0) {
		panic(frame, "Invalid render buffer");
		return;
	}
	const RenderBufferHandle handle = fromScript(rb);
	ctx->pipeline->getRenderer().setRenderTargets(Span(&handle, 1));
}

static bool readDefine(ex_string_view define, char (&out)[64]) {
	if (define.length < 0 || define.length >= (i64)sizeof(out) || (define.length > 0 && !define.begin)) return false;
	if (define.length > 0) memcpy(out, define.begin, define.length);
	out[define.length] = '\0';
	return true;
}

static void postprocessDispatch(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
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

// Draws a fullscreen triangle with a surface shader (`mainVS`/`mainPS`) into the current render target.
static void postprocessDraw(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_ARG(frame, Shader*, shader);
	EX_STRING_ARG(frame, define_view);
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
	ctx->pipeline->drawArray(0, 3, *shader, define_mask, gpu::StateFlags::NONE);
}

// Makes the debug view of the pipeline show the output called `name` (see `debugOutput`). The names only have to be
// unique among the scripts.
static void postprocessShowDebug(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_STRING_ARG(frame, name);
	if (name.length < 0 || (name.length > 0 && !name.begin)) {
		panic(frame, "Invalid debug name");
		return;
	}
	ctx->plugin->showDebug(*ctx->pipeline, StringView(name.begin, (u64)name.length));
}

static void postprocessIsDebugShown(ex_runtime*, ex_call_frame frame) {
	EvoxPostprocessContext* ctx = readContext(frame);
	if (!ctx) return;
	EX_STRING_ARG(frame, name);
	if (name.length < 0 || (name.length > 0 && !name.begin)) {
		panic(frame, "Invalid debug name");
		return;
	}
	EX_RESULT(frame, u8(ctx->plugin->isDebugShown(*ctx->pipeline, StringView(name.begin, (u64)name.length)) ? 1 : 0));
}

} // anonymous namespace

void registerPostprocessFunctions(HashMap<NativeFunctionKey, ex_native_fn, NativeFunctionKeyHash>& functions) {
	functions.insert({"core:postprocess", "add"}, &postprocessAdd);
	functions.insert({"core:postprocess", "gbufferRaw"}, &postprocessGBuffer);
	functions.insert({"core:postprocess", "builtinTaaEnabled"}, &postprocessBuiltinTaaEnabled);
	functions.insert({"core:postprocess", "clear"}, &postprocessClear);
	functions.insert({"core:postprocess", "stateRaw"}, &postprocessStateRaw);
	functions.insert({"core:postprocess", "createBuffer"}, &postprocessCreateBuffer);
	functions.insert({"core:postprocess", "createRenderTarget"}, &postprocessCreateRenderTarget);
	functions.insert({"core:postprocess", "releaseBuffer"}, &postprocessReleaseBuffer);
	functions.insert({"core:postprocess", "blit"}, &postprocessBlit);
	functions.insert({"core:postprocess", "input"}, &postprocessInput);
	functions.insert({"core:postprocess", "depth"}, &postprocessDepth);
	functions.insert({"core:postprocess", "renderModule"}, &postprocessRenderModule);
	functions.insert({"core:postprocess", "pipelineType"}, &postprocessPipelineType);
	functions.insert({"core:postprocess", "width"}, &postprocessWidth);
	functions.insert({"core:postprocess", "displayWidth"}, &postprocessDisplayWidth);
	functions.insert({"core:postprocess", "displayHeight"}, &postprocessDisplayHeight);
	functions.insert({"core:postprocess", "enablePixelJitter"}, &postprocessEnablePixelJitter);
	functions.insert({"core:postprocess", "height"}, &postprocessHeight);
	functions.insert({"core:postprocess", "loadShader"}, &postprocessLoadShader);
	functions.insert({"core:postprocess", "isReady"}, &postprocessIsReady);
	functions.insert({"core:postprocess", "bindless"}, &postprocessBindless);
	functions.insert({"core:postprocess", "rwBindless"}, &postprocessRWBindless);
	functions.insert({"core:postprocess", "beginBlock"}, &postprocessBeginBlock);
	functions.insert({"core:postprocess", "endBlock"}, &postprocessEndBlock);
	functions.insert({"core:postprocess", "dispatchRaw"}, &postprocessDispatch);
	functions.insert({"core:postprocess", "drawRaw"}, &postprocessDraw);
	functions.insert({"core:postprocess", "createStorage"}, &postprocessCreateStorage);
	functions.insert({"core:postprocess", "releaseStorage"}, &postprocessReleaseStorage);
	functions.insert({"core:postprocess", "bindlessStorage"}, &postprocessBindlessStorage);
	functions.insert({"core:postprocess", "rwBindlessStorage"}, &postprocessRWBindlessStorage);
	functions.insert({"core:postprocess", "setRenderTarget"}, &postprocessSetRenderTarget);
	functions.insert({"core:postprocess", "showDebug"}, &postprocessShowDebug);
	functions.insert({"core:postprocess", "isDebugShown"}, &postprocessIsDebugShown);
}

} // namespace Evox

} // namespace Lumix
