#pragma once


#include "engine/lumix.h"
#include "engine/array.h"
#include "engine/blob.h"
#include "engine/resource.h"
#include "engine/resource_manager_base.h"


namespace Lumix
{


class ParticleEmitterResourceManager final : public ResourceManagerBase
{
public:
	ParticleEmitterResourceManager(IAllocator& allocator) 
		: ResourceManagerBase(allocator)
		, m_allocator(allocator) {}

	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;
private:
	IAllocator& m_allocator;
};


class ParticleEmitterResource final : public Resource
{
public:
	static const ResourceType TYPE;

	ParticleEmitterResource(const Path& path, ResourceManagerBase& manager, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }
	void unload() override;
	bool load(FS::IFile& file) override;
	const OutputBlob& getBytecode() const { return m_bytecode; }
	int getEmitByteOffset() const { return m_emit_byte_offset; }
	int getChannelsCount() const { return m_channels_count; }
	int getRegistersCount() const { return m_registers_count; }
	int getOutputsCount() const { return m_outputs_count; }
	float getLiteralValue(int idx) const { ASSERT(idx < lengthOf(m_literals)); return m_literals[idx]; }
private:
	OutputBlob m_bytecode;
	float m_literals[16];
	int m_emit_byte_offset;
	int m_channels_count;
	int m_registers_count;
	int m_outputs_count;
};



class Material;
class ResourceManager;


class LUMIX_RENDERER_API ParticleEmitter
{
public:
	ParticleEmitter(EntityPtr entity, IAllocator& allocator);
	~ParticleEmitter();

	void serialize(OutputBlob& blob);
	void deserialize(InputBlob& blob, ResourceManager& manager);
	void update(float dt);
	void emit(const float* args);
	const float* getInstanceData() const { return m_instance_data.begin(); }
	int getInstanceDataSize() const { return m_instance_data.size(); }
	ParticleEmitterResource* getResource() const { return m_resource; }
	void setResource(ParticleEmitterResource* res);
	float* getChannelData(int idx) const { return m_channels[idx].data; }
	
	EntityPtr m_entity;

private:
	struct Channel
	{
		float* data = nullptr;
		u32 name = 0;
	};

	struct Constant
	{
		u32 name = 0;
		float value = 0;
	};

	void execute(InputBlob& blob, int particle_index);
	void kill(int particle_index);

	IAllocator& m_allocator;
	OutputBlob m_emit_buffer;
	Constant m_constants[16];
	int m_constants_count = 0;
	Channel m_channels[16];
	int m_capacity = 0;
	int m_outputs_per_particle = 0;
	int m_particles_count = 0;
	ParticleEmitterResource* m_resource = nullptr;
	Array<float> m_instance_data;
};


} // namespace Lumix