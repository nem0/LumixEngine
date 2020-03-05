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
public:
	static const ResourceType TYPE;

	ParticleEmitterResource(const Path& path, ResourceManager& manager, Renderer& renderer, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }
	void unload() override;
	bool load(u64 size, const u8* mem) override;
	const OutputMemoryStream& getBytecode() const { return m_bytecode; }
	int getEmitByteOffset() const { return m_emit_byte_offset; }
	int getOutputByteOffset() const { return m_output_byte_offset; }
	int getChannelsCount() const { return m_channels_count; }
	int getRegistersCount() const { return m_registers_count; }
	int getOutputsCount() const { return m_outputs_count; }
	Material* getMaterial() const { return m_material; }
	void setMaterial(const Path& path);

private:
	OutputMemoryStream m_bytecode;
	int m_emit_byte_offset;
	int m_output_byte_offset;
	int m_channels_count;
	int m_registers_count;
	int m_outputs_count;
	Material* m_material;
};



struct Material;
struct ResourceManagerHub;


struct LUMIX_RENDERER_API ParticleEmitter
{
public:
	ParticleEmitter(EntityPtr entity, IAllocator& allocator);
	~ParticleEmitter();

	void serialize(OutputMemoryStream& blob);
	void deserialize(InputMemoryStream& blob, ResourceManagerHub& manager);
	void update(float dt);
	void emit(const float* args);
	void fillInstanceData(const DVec3& cam_pos, float* data);
	int getInstanceDataSizeBytes() const;
	ParticleEmitterResource* getResource() const { return m_resource; }
	void setResource(ParticleEmitterResource* res);
	int getInstancesCount() const { return m_instances_count; }
	float* getChannelData(int idx) const { return m_channels[idx].data; }
	
	EntityPtr m_entity;

private:
	struct Channel
	{
		alignas(16) float* data = nullptr;
		u32 name = 0;
	};

	struct Constant
	{
		u32 name = 0;
		float value = 0;
	};

	void execute(InputMemoryStream& blob, int particle_index);
	void kill(int particle_index);
	float readSingleValue(InputMemoryStream& blob) const;

	IAllocator& m_allocator;
	OutputMemoryStream m_emit_buffer;
	Constant m_constants[16];
	Channel m_channels[16];
	int m_capacity = 0;
	int m_particles_count = 0;
	int m_instances_count = 0;
	ParticleEmitterResource* m_resource = nullptr;
};


} // namespace Lumix