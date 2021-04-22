#pragma once


#include "engine/lumix.h"
#include "engine/array.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"


namespace Lumix
{


struct DVec3;
struct Material;
struct Renderer;


struct ParticleEmitterResource final : Resource
{
	struct Header {
		static constexpr u32 MAGIC = '_LPA';
		const u32 magic = MAGIC;
		u32 version = 0;
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
		FREE0,
		SUB,
		FREE1,
		MUL,
		MULTIPLY_ADD,
		LT,
		MOV,
		RAND,
		KILL,
		EMIT,
		GT,
		MIX,
		GRADIENT,
		DIV
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
		u32 outputs_count
	);

private:
	OutputMemoryStream m_instructions;
	u32 m_emit_offset;
	u32 m_output_offset;
	u32 m_channels_count;
	u32 m_registers_count;
	u32 m_outputs_count;
	Material* m_material;
};


struct ResourceManagerHub;


struct LUMIX_RENDERER_API ParticleEmitter
{
public:
	ParticleEmitter(EntityPtr entity, IAllocator& allocator);
	ParticleEmitter(ParticleEmitter&& rhs);
	~ParticleEmitter();

	void serialize(OutputMemoryStream& blob) const;
	void deserialize(InputMemoryStream& blob, bool has_autodestroy, ResourceManagerHub& manager);
	bool update(float dt, struct PageAllocator& allocator);
	void emit(const float* args);
	void fillInstanceData(float* data) const;
	u32 getParticlesDataSizeBytes() const;
	ParticleEmitterResource* getResource() const { return m_resource; }
	void setResource(ParticleEmitterResource* res);
	u32 getParticlesCount() const { return m_particles_count; }
	float* getChannelData(u32 idx) const { return m_channels[idx].data; }
	void reset() { m_particles_count = 0; }

	EntityPtr m_entity;
	u32 m_emit_rate = 10;
	u32 m_particles_count = 0;
	bool m_autodestroy = false;
	float m_constants[16];

private:
	struct Channel
	{
		alignas(16) float* data = nullptr;
		u32 name = 0;
	};

	void operator =(ParticleEmitter&& rhs) = delete;
	float readSingleValue(InputMemoryStream& blob) const;
	void onResourceChanged(Resource::State old_state, Resource::State new_state, Resource&);

	IAllocator& m_allocator;
	OutputMemoryStream m_emit_buffer;
	Channel m_channels[16];
	u32 m_capacity = 0;
	float m_emit_timer = 0;
	ParticleEmitterResource* m_resource = nullptr;
};


} // namespace Lumix