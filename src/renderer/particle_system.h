#pragma once


#include "engine/lumix.h"
#include "engine/array.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "renderer/gpu/gpu.h"


namespace Lumix
{


struct DVec3;
struct Material;
struct Renderer;


struct ParticleEmitterResource final : Resource {
	enum class Version : u32{
		VERTEX_DECL,
		EMIT_RATE,
		LAST
	};
	struct Header {
		static constexpr u32 MAGIC = '_LPA';
		const u32 magic = MAGIC;
		Version version = Version::LAST;
	};
	
	struct DataStream {
		enum Type : u8 {
			NONE,
			CHANNEL,
			CONST,
			OUT,
			REGISTER,
			LITERAL
		};

		Type type = NONE;
		u8 index;
		float value;
	};

	enum class InstructionType : u8{
		END,
		ADD,
		COS,
		SIN,
		NOISE,
		SUB,
		FREE1,
		MUL,
		MULTIPLY_ADD,
		LT,
		MOV,
		RAND,
		KILL,
		FREE2,
		GT,
		MIX,
		GRADIENT,
		DIV,
		SPLINE,
		MESH,
		MOD
	};

	static const ResourceType TYPE;

	ParticleEmitterResource(const Path& path, ResourceManager& manager, Renderer& renderer, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }
	void unload() override;
	bool load(u64 size, const u8* mem) override;
	const OutputMemoryStream& getInstructions() const { return m_instructions; }
	u32 getEmitOffset() const { return m_emit_offset; }
	u32 getOutputOffset() const { return m_output_offset; }
	u32 getChannelsCount() const { return m_channels_count; }
	u32 getRegistersCount() const { return m_registers_count; }
	u32 getOutputsCount() const { return m_outputs_count; }
	Material* getMaterial() const { return m_material; }
	void setMaterial(const Path& path);
	void overrideData(OutputMemoryStream&& instructions,
		u32 emit_offset,
		u32 output_offset,
		u32 channels_count,
		u32 registers_count,
		u32 outputs_count,
		u32 init_emit_count,
		float emit_rate
	);
	const gpu::VertexDecl& getVertexDecl() const { return m_vertex_decl; }
	
	u32 getInitEmitCount() const { return m_init_emit_count; }
	float getEmitPerSecond() const { return m_emit_per_second; }

private:
	OutputMemoryStream m_instructions;
	u32 m_emit_offset;
	u32 m_output_offset;
	u32 m_channels_count;
	u32 m_registers_count;
	u32 m_outputs_count;
	Material* m_material;
	gpu::VertexDecl m_vertex_decl;
	u32 m_init_emit_count = 0;
	float m_emit_per_second = 100;
};


struct ResourceManagerHub;


struct LUMIX_RENDERER_API ParticleEmitter
{
public:
	ParticleEmitter(EntityPtr entity, struct World& world, IAllocator& allocator);
	ParticleEmitter(ParticleEmitter&& rhs);
	~ParticleEmitter();

	void serialize(OutputMemoryStream& blob) const;
	void deserialize(InputMemoryStream& blob, bool has_autodestroy, bool emit_rate_removed, ResourceManagerHub& manager);
	bool update(float dt, struct PageAllocator& allocator);
	void emit();
	void fillInstanceData(float* data) const;
	u32 getParticlesDataSizeBytes() const;
	ParticleEmitterResource* getResource() const { return m_resource; }
	void setResource(ParticleEmitterResource* res);
	u32 getParticlesCount() const { return m_particles_count; }
	float* getChannelData(u32 idx) const { return m_channels[idx].data; }
	void reset() { m_particles_count = 0; m_total_time = 0; }

	EntityPtr m_entity;
	u32 m_particles_count = 0;
	bool m_autodestroy = false;
	float m_constants[16];
	float m_total_time = 0;

private:
	struct Channel
	{
		alignas(16) float* data = nullptr;
		u32 name = 0;
	};

	void operator =(ParticleEmitter&& rhs) = delete;
	void onResourceChanged(Resource::State old_state, Resource::State new_state, Resource&);

	IAllocator& m_allocator;
	World& m_world;
	Channel m_channels[16];
	u32 m_capacity = 0;
	float m_emit_timer = 0;
	ParticleEmitterResource* m_resource = nullptr;
};


} // namespace Lumix