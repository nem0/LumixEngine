#pragma once

#include "core/array.h"
#include "core/hash_map.h"
#include "core/path.h"
#include "core/string.h"
#include "engine/resource.h"
#include "evox/capi.h"
#include "evox/evox_module.h"
#include "renderer/renderer.h"

namespace Lumix {

struct Engine;
struct EvoxPostprocessContext;
struct EvoxResource;
struct Shader;

struct EvoxPostprocess : RenderPlugin {
	using NativeFunctions = HashMap<NativeFunctionKey, ex_native_fn, NativeFunctionKeyHash>;

	enum Hook : u32 {
		BEFORE_LIGHT_PASS,
		BEFORE_TRANSPARENT,
		ANTIALIAS,
		BEFORE_TONEMAP,
		TONEMAP,
		AFTER_TONEMAP,
		DEBUG_UI,
		DEBUG_OUTPUT,
		HOOK_COUNT
	};

	EvoxPostprocess(Engine& engine, IAllocator& allocator);

	// Runs `main(registry)` of `postprocess.evox` in `runtime`, which is owned by the caller and must outlive the next
	// `detach`. Does nothing if the bytecode has no `postprocess.evox` unit.
	void attach(ex_bytecode* bytecode, ex_runtime* runtime);
	// Drops the effects and everything that belongs to the runtime. Call it before the runtime is destroyed.
	void detach();
	Shader* loadShader(StringView path);
	// Size of core:postprocess.Postprocess in bytes, 0 if the script does not use it.
	u32 getEffectSize() const { return m_effect_type ? ex_type_get_size(m_effect_type) : 0; }
	// Registers an effect from the memory of a script `Postprocess` value. Only valid while `main` runs.
	void addEffect(const void* memory);
	// GPU buffers created by scripts (core:postprocess.Storage), ids are index + 1, 0 is invalid.
	u32 createStorage(Renderer& renderer, u32 size);
	gpu::BufferHandle getStorage(u32 id) const;
	// The buffer is destroyed on the next hook call.
	void releaseStorage(u32 id);
	// Zero-initialized memory of a script type with index `type_index`, one per pipeline and type. Only valid while a
	// hook runs. Returns nullptr for an invalid type.
	u8* getState(Pipeline& pipeline, u32 type_index, u32& size);

	// Picks what the debug view of the pipeline shows: the effect output `name` of the script. See ctx.showDebug.
	void showDebug(Pipeline& pipeline, StringView name);
	bool isDebugShown(Pipeline& pipeline, StringView name) const;

	void shutdown(Renderer& renderer) override;
	void pipelineDestroyed(Pipeline& pipeline) override;
	void debugUI(Pipeline& pipeline) override;
	bool debugOutput(RenderBufferHandle input, Pipeline& pipeline) override;
	void renderBeforeLightPass(const GBuffer& gbuffer, Pipeline& pipeline) override;
	RenderBufferHandle renderBeforeTransparent(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override;
	RenderBufferHandle renderAA(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override;
	RenderBufferHandle renderBeforeTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override;
	bool tonemap(RenderBufferHandle input, RenderBufferHandle& output, Pipeline& pipeline) override;
	RenderBufferHandle renderAfterTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override;

private:
	static constexpr u32 INVALID_FUNCTION = 0xffFFffFF;

	struct Effect {
		// indices of script functions in the bytecode, INVALID_FUNCTION if the hook is not set
		u32 functions[HOOK_COUNT];
	};

	// per pipeline script state, see core:postprocess `state`
	struct StateBlock {
		Pipeline* pipeline;
		u32 type_index;
		u32 size;
		u8* memory;
		const ex_type* type;
	};

	// what the debug view of a pipeline shows, set by scripts with `showDebug`
	struct DebugSelection {
		Pipeline* pipeline;
		StaticString<64> name;
	};

	struct ShaderEntry {
		Path path;
		Shader* shader;
	};

	void runMain();
	// A task that failed (e.g. a panic) stays suspended and can't run anything else, replace it.
	void recreateTask();
	// Queues all core:postprocess.RenderBuffer in the value for release and frees the block.
	void freeState(StateBlock& block);
	void queueBuffersForRelease(const ex_type* type, const u8* memory);
	void destroyPending(Renderer& renderer);
	// Calls the hook of all effects. Returns the output of the last effect that returned a valid buffer, or
	// INVALID_RENDERBUFFER. Antialiasing and tonemap stop at the first effect with a valid output.
	RenderBufferHandle run(Hook hook, const GBuffer* gbuffer, RenderBufferHandle input, Pipeline& pipeline);
	// Calls one hook of an effect. Returns false if it failed (the hook is disabled then).
	bool callHook(Effect& effect, Hook hook, EvoxPostprocessContext& context, RenderBufferHandle input);

	Engine& m_engine;
	IAllocator& m_allocator;
	// everything runs on the update thread: hooks are called by Pipeline::render and resource callbacks come from FileSystem::processCallbacks
	// owned by the Evox system
	ex_bytecode* m_bytecode = nullptr;
	ex_runtime* m_runtime = nullptr;
	ex_task* m_task = nullptr;
	// core:postprocess.Postprocess and the index of its field for every hook (-1 if missing)
	const ex_type* m_effect_type = nullptr;
	i32 m_field_of_hook[HOOK_COUNT];
	Array<Effect> m_effects;
	Array<StateBlock> m_state;
	// render buffers owned by freed state, released on the next hook call (render buffers are not released from other threads)
	Array<RenderBufferHandle> m_pending_release;
	// gpu::INVALID_BUFFER for free slots
	Array<gpu::BufferHandle> m_storage;
	Array<gpu::BufferHandle> m_pending_destroy;
	Array<ShaderEntry> m_shaders;
	Array<DebugSelection> m_debug_selection;
};

namespace Evox {
	void registerPostprocessFunctions(EvoxPostprocess::NativeFunctions& functions);
}

} // namespace Lumix
