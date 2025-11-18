#pragma once

#include "engine/lumix.h"

#include "core/array.h"
#include "core/atomic.h"
#include "core/math.h"
#include "core/stream.h"

#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "renderer/gpu/gpu.h"
#include "renderer/renderer.h"


namespace Lumix
{


struct DVec3;
struct Material;
struct Model;
struct Renderer;


struct ParticleSystemResource final : Resource {
	enum class Version : u32{
		NOT_SUPPORTED_BEFORE = 12,

		LAST
	};
	struct Header {
		static constexpr u32 MAGIC = '_LPA';
		const u32 magic = MAGIC;
		Version version = Version::LAST;
	};

	struct Global {
		Global(IAllocator& allocator) : name(allocator) {}
		String name;
		u32 num_floats = 0;
		u32 offset = 0;
	};

	struct Emitter {
		Emitter(ParticleSystemResource& resource);
		~Emitter();
		void setMaterial(const Path& path);
		void setModel(const Path& path);
		
		ParticleSystemResource& resource;
		OutputMemoryStream instructions;
		u32 emit_offset;
		u32 output_offset;
		u32 channels_count;
		u32 emit_instructions_count;
		u32 update_instructions_count;
		u32 output_instructions_count;
		u32 emit_registers_count;
		u32 update_registers_count;
		u32 output_registers_count;
		u32 emit_inputs_count;
		u32 outputs_count;
		u32 max_ribbons;
		u32 max_ribbon_length;
		u32 init_ribbons_count;
		bool emit_on_move = false;
		Material* material = nullptr;
		Model* model = nullptr;
		u32 init_emit_count = 0;
		float emit_per_second = 100;
		gpu::VertexDecl vertex_decl;
	};
	
	struct DataStream {
		enum Type : u8 {
			NONE,
			CHANNEL,
			SYSTEM_VALUE,
			OUT,
			REGISTER,
			LITERAL,
			GLOBAL,

			ERROR
		};

		bool isError() const { return type == ERROR; }

		Type type = NONE;
		u8 index;
		float value;
	};

	enum class Flags : u32 {
		WORLD_SPACE = 1 << 0,

		NONE = 0
	};

	enum class InstructionType : u8{
		END,
		ADD,
		COS,
		SIN,
		NOISE,
		SUB,
		EMIT,
		MUL,
		MULTIPLY_ADD,
		LT,
		MOV,
		RAND,
		KILL,
		SQRT,
		GT,
		MIX,
		GRADIENT,
		DIV,
		SPLINE,
		MESH,
		MOD,
		OR,
		AND,
		BLEND,
		MAX,
		MIN,
		CMP,
		CMP_ELSE
	};

	static const ResourceType TYPE;

	ParticleSystemResource(const Path& path, ResourceManager& manager, Renderer& renderer, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }
	void unload() override;
	bool load(Span<const u8> mem) override;
	void overrideData(u32 emitter_idx
		, OutputMemoryStream&& instructions
		, u32 emit_offset
		, u32 output_offset
		, u32 channels_count
		, u32 registers_count
		, u32 outputs_count
		, u32 init_emit_count
		, u32 emit_inputs_count
		, float emit_rate
		, const Path& material
	);

	Array<Emitter>& getEmitters() { return m_emitters; }
	Flags getFlags() const { return m_flags; }
	Span<const Global> getGlobals() const { return m_globals; }

private:
	Array<Emitter> m_emitters;
	Array<Global> m_globals;
	IAllocator& m_allocator;
	Flags m_flags = Flags::NONE;
};


struct ResourceManagerHub;

enum class ParticleSystemValues : u8 {
	TIME_DELTA = 0,
	TOTAL_TIME = 1,
	EMIT_INDEX = 2,
	RIBBON_INDEX = 3,
	COUNT,

	NONE = 0xff
};

struct LUMIX_RENDERER_API ParticleSystem {
	struct Channel {
		float* data = nullptr;
		u32 name = 0;
	};

	struct Stats {
		AtomicI32 emitted = 0;
		AtomicI32 killed = 0;
		AtomicI32 processed = 0;
	};

	struct Ribbon {
		u32 offset;
		u32 length;
		u32 emit_index;
	};

	struct Emitter {
		Emitter(Emitter&& rhs);
		Emitter(ParticleSystem& system, ParticleSystemResource::Emitter& resource_emitter) 
			: system(system)
			, resource_emitter(resource_emitter)
			, ribbons(system.m_allocator)
		{}
		u32 getParticlesDataSizeBytes() const;
		void fillInstanceData(float* data, PageAllocator& page_allocator) const;
		
		ParticleSystem& system;
		ParticleSystemResource::Emitter& resource_emitter;
		Channel channels[16];
		u32 particles_count = 0;
		Array<Ribbon> ribbons;
		u32 capacity = 0;
		float emit_timer = 0;
		u32 emit_index = 0;
		DVec3 last_emit_point;
		TransientSlice slice;
	};

	ParticleSystem(EntityPtr entity, struct World& world, IAllocator& allocator);
	ParticleSystem(ParticleSystem&& rhs);
	~ParticleSystem();

	void serialize(OutputMemoryStream& blob) const;
	void deserialize(InputMemoryStream& blob, bool has_autodestroy, bool emit_rate_removed, ResourceManagerHub& manager);
	void applyTransform(const Transform& new_tr);
	bool update(float dt, PageAllocator& page_allocator);
	ParticleSystemResource* getResource() const { return m_resource; }
	void setResource(ParticleSystemResource* res);
	const Emitter& getEmitter(u32 emitter_idx) const { return m_emitters[emitter_idx]; }
	const Array<Emitter>& getEmitters() const { return m_emitters; }
	void reset();

	World& m_world;
	EntityPtr m_entity;
	bool m_autodestroy = false;
	float m_system_values[16];
	float m_total_time = 0;
	Stats m_last_update_stats;
	Array<float> m_globals;

private:
	struct RunningContext;
	struct ChunkProcessorContext;

	enum class RunResult {
		SURVIVED,
		KILLED
	};

	void operator =(ParticleSystem&& rhs) = delete;
	void onResourceChanged(Resource::State old_state, Resource::State new_state, Resource&);
	void update(float dt, u32 emitter_idx, PageAllocator& page_allocator);
	void updateRibbons(float dt, u32 emitter_idx, PageAllocator& page_allocator);
	void emitRibbonPoints(u32 emitter_idx, u32 ribbon_idx, Span<const float> emit_data, u32 count, float time_step);
	void emit(u32 emitter_idx, Span<const float> emit_data, u32 count, float time_step);
	void ensureCapacity(Emitter& emitter, u32 num_new_particles);
	RunResult run(RunningContext& ctx);
	void skipBlock(ParticleSystem::RunningContext& ctx, const InputMemoryStream& ip, InputMemoryStream& head, InputMemoryStream& tail);
	void processChunk(ChunkProcessorContext& ctx);
	void initRibbonEmitter(i32 emiter);

	IAllocator& m_allocator;
	Array<Emitter> m_emitters;
	ParticleSystemResource* m_resource = nullptr;
	Transform m_prev_frame_transform;
};


} // namespace Lumix