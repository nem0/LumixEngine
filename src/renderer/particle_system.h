#pragma once


#include "engine/lumix.h"
#include "engine/array.h"
#include "engine/blob.h"
#include "engine/vec.h"


namespace bgfx
{
	struct InstanceDataBuffer;
}


namespace Lumix
{


struct IAllocator;
class InputBlob;
class Material;
class OutputBlob;
class RenderScene;
class ResourceManager;
class Universe;
class WorldEditor;


struct IntInterval
{
	int from;
	int to;

	IntInterval();
	int getRandom() const;
};


struct Interval
{
	float from;
	float to;


	Interval();
	float getRandom() const;

	void check();
	void checkZero();

	void operator=(const Vec2& value)
	{
		from = value.x;
		to = value.y;
	}

	operator Vec2()
	{
		return Vec2(from, to);
	}
};


class LUMIX_RENDERER_API ScriptedParticleEmitter
{
public:
	ScriptedParticleEmitter(EntityPtr entity, IAllocator& allocator);
	~ScriptedParticleEmitter();

	void serialize(OutputBlob& blob);
	void deserialize(InputBlob& blob, ResourceManager& manager);
	void compile(const char* code);
	void update(float dt);
	void emit(const float* args);
	bgfx::InstanceDataBuffer generateInstanceBuffer() const;
	Material* getMaterial() const { return m_material; }
	void setMaterial(Material* material);
	int getChannel(const char* name) const;
	int getConstant(const char* name) const;
	
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

	void parseInstruction(const char* instruction, struct ParseContext& ctx);
	void execute(InputBlob& blob, int particle_index);
	void kill(int particle_index);

	IAllocator& m_allocator;
	Array<u8> m_bytecode;
	OutputBlob m_emit_buffer;
	int m_output_bytecode_offset;
	int m_emit_bytecode_offset;
	Constant m_constants[16];
	int m_constants_count = 0;
	Channel m_channels[16];
	int m_channels_count = 0;
	int m_capacity = 0;
	int m_outputs_per_particle = 0;
	int m_particles_count = 0;
	Material* m_material = nullptr;
};


} // namespace Lumix