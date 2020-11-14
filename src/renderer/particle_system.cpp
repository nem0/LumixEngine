#include "particle_system.h"
#include "engine/crc32.h"
#include "engine/crt.h"
#include "engine/atomic.h"
#include "engine/job_system.h"
#include "engine/lua_wrapper.h"
#include "engine/math.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/simd.h"
#include "engine/stream.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "renderer/material.h"
#include "renderer/render_scene.h"
#include "engine/universe.h"


namespace Lumix
{

using DataStream = ParticleEmitterResource::DataStream;
using InstructionType = ParticleEmitterResource::Instruction::Type;
using Instruction = ParticleEmitterResource::Instruction;

const ResourceType ParticleEmitterResource::TYPE = ResourceType("particle_emitter");


ParticleEmitterResource::ParticleEmitterResource(const Path& path
	, ResourceManager& manager
	, Renderer& renderer
	, IAllocator& allocator)
	: Resource(path, manager, allocator)
	, m_instructions(allocator)
	, m_material(nullptr)
{
}


void ParticleEmitterResource::unload()
{
	m_instructions.clear();
}


void ParticleEmitterResource::overrideData(Array<Instruction>&& instructions,
	int emit_offset,
	int output_offset,
	int channels_count,
	int registers_count,
	int outputs_count
)
{
	++m_empty_dep_count;
	checkState();
	
	m_instructions = static_cast<Array<Instruction>&&>(instructions);
	m_emit_offset = emit_offset;
	m_output_offset = output_offset;
	m_channels_count = channels_count;
	m_registers_count = registers_count;
	m_outputs_count = outputs_count;
	
	--m_empty_dep_count;
	checkState();
}

void ParticleEmitterResource::setMaterial(const Path& path)
{
	Material* material = m_resource_manager.getOwner().load<Material>(path);
	if (m_material) { //-V1051
		Material* tmp = m_material;
		m_material = nullptr;
		removeDependency(*tmp);
		tmp->decRefCount();
	}
	m_material = material;
	if (m_material) {
		addDependency(*m_material);
	}
}

bool ParticleEmitterResource::load(u64 size, const u8* mem) {
	Header header;
	InputMemoryStream blob(mem, size);
	blob.read(header);
	if (header.magic != Header::MAGIC) {
		logError("Renderer") << "Invalid file " << getPath();
		return false;
	}
	if (header.version != 0) {
		logError("Renderer") << "Unsupported version " << getPath();
		return false;
	}

	setMaterial(Path(blob.readString()));
	const i32 count = blob.read<i32>();
	m_instructions.resize(count);
	blob.read(m_instructions.begin(), m_instructions.byte_size());
	blob.read(m_emit_offset);
	blob.read(m_output_offset);
	blob.read(m_channels_count);
	blob.read(m_registers_count);
	blob.read(m_outputs_count);

	return true;
}


ParticleEmitter::ParticleEmitter(EntityPtr entity, IAllocator& allocator)
	: m_allocator(allocator)
	, m_entity(entity)
	, m_emit_buffer(allocator)
{
}


ParticleEmitter::~ParticleEmitter()
{
	setResource(nullptr);
	setMaterial(nullptr);
	for (const Channel& c : m_channels) {
		m_allocator.deallocate_aligned(c.data);
	}
}

void ParticleEmitter::setMaterial(Material* material) {
	if (m_material) m_material->decRefCount();
	m_material = material;
}

void ParticleEmitter::setResource(ParticleEmitterResource* res)
{
	if (m_resource) {
		m_resource->decRefCount();
	}
	m_resource = res;
}


float ParticleEmitter::readSingleValue(InputMemoryStream& blob) const
{
	const auto type = blob.read<DataStream::Type>();
	switch(type) {
		case DataStream::LITERAL:
			return blob.read<float>();
			break;
		case DataStream::CHANNEL: {
			const u8 idx = blob.read<u8>();
			return m_channels[idx].data[m_particles_count];
		}
		default:
			ASSERT(false);
			return 0;
	}
}


void ParticleEmitter::emit(const float* args)
{
	if (m_particles_count == m_capacity) {
		const int channels_count = m_resource->getChannelsCount();
		int new_capacity = maximum(16, m_capacity << 1);
		for (int i = 0; i < channels_count; ++i)
		{
			m_channels[i].data = (float*)m_allocator.reallocate_aligned(m_channels[i].data, new_capacity * sizeof(float), 16);
		}
		m_capacity = new_capacity;
	}

	const Array<Instruction>& instructions = m_resource->getInstructions();

	const Instruction* instruction = instructions.begin() + m_resource->getEmitOffset();
	for (;;) {
		switch((InstructionType)instruction->type) {
			case InstructionType::END:
				++m_particles_count;
				return;
			case InstructionType::MOV: {
				ASSERT(instruction->dst.type == DataStream::CHANNEL);
				ASSERT(instruction->op0.type == DataStream::LITERAL);
				m_channels[instruction->dst.index].data[m_particles_count] = instruction->op0.value;
				break;
			}
			case InstructionType::RAND: {
				ASSERT(instruction->dst.type == DataStream::CHANNEL);
				ASSERT(instruction->op0.type == DataStream::LITERAL);
				ASSERT(instruction->op1.type == DataStream::LITERAL);
				
				const float from = instruction->op0.value;
				const float to = instruction->op1.value;
				
				m_channels[instruction->dst.index].data[m_particles_count] = randFloat(from, to);
				break;
			}
			default:
				ASSERT(false);
				break;
		}
		++instruction;
	}
}


void ParticleEmitter::serialize(OutputMemoryStream& blob)
{
	blob.write(m_entity);
	blob.writeString(m_resource ? m_resource->getPath().c_str() : "");
	blob.writeString(m_material ? m_material->getPath().c_str() : "");
}


void ParticleEmitter::deserialize(InputMemoryStream& blob, ResourceManagerHub& manager)
{
	blob.read(m_entity);
	const char* path = blob.readString();
	auto* res = manager.load<ParticleEmitterResource>(Path(path));
	setResource(res);

	const char* matpath = blob.readString();
	auto* mat = manager.load<Material>(Path(matpath));
	setMaterial(mat);
}


void ParticleEmitter::kill(int particle_index)
{
	if (particle_index >= m_particles_count) return;
	const int channels_count = m_resource->getChannelsCount();
	int last = m_particles_count - 1;
	for (int i = 0; i < channels_count; ++i) {
		float* data = m_channels[i].data;
		data[particle_index] = data[last];
	}
	--m_particles_count;
}


void ParticleEmitter::execute(InputMemoryStream& blob, int particle_index)
{
	if (particle_index >= m_particles_count) return;
	for (;;)
	{
		u8 instruction = blob.read<u8>();
		/*u8 flag = */blob.read<u8>();

		switch ((InstructionType)instruction)
		{
			case InstructionType::END:
				return;
			case InstructionType::KILL:
				kill(particle_index);
				ASSERT(blob.read<u8>() == (u8)InstructionType::END);
				return;
			case InstructionType::EMIT:
			{
				float args[16];
				u8 arg_count = blob.read<u8>();
				m_emit_buffer.write(arg_count);
				ASSERT(arg_count < lengthOf(args));
				for (int i = 0; i < arg_count; ++i)
				{
					u8 ch = blob.read<u8>();
					m_emit_buffer.write(m_channels[ch].data[particle_index]);
				}
				break;
			}
			default:
				ASSERT(false);
				break;
		}
	}
}


static float4* getStream(const ParticleEmitter& emitter
	, DataStream stream
	, int particles_count
	, float4* register_mem)
{
	switch (stream.type) {
		case DataStream::CHANNEL: return (float4*)emitter.getChannelData(stream.index); //-V1032
		case DataStream::REGISTER: return register_mem + particles_count * stream.index;
		default: ASSERT(false); return nullptr;
	}
}


void ParticleEmitter::update(float dt)
{
	if (!m_resource || !m_resource->isReady()) return;
	
	if (m_emit_rate > 0) {
		m_emit_timer += dt;
		const float d = 1.f / m_emit_rate;
		while(m_emit_timer > 0) {
			emit(nullptr);
			m_emit_timer -= d;
		}
	}

	PROFILE_FUNCTION();
	Profiler::pushInt("particle count", m_particles_count);
	if (m_particles_count == 0) return;

	m_emit_buffer.clear();
	m_constants[0].value = dt;
	const Instruction* instruction = m_resource->getInstructions().begin();
	// TODO
	Array<float4> reg_mem(m_allocator);
	reg_mem.resize(m_resource->getRegistersCount() * ((m_particles_count + 3) >> 2));
	m_instances_count = m_particles_count;

	for (;;) {
		switch ((InstructionType)instruction->type) {
			case InstructionType::END:
				goto end;
			case InstructionType::MUL: {
				const float4* arg0 = getStream(*this, instruction->op0, m_particles_count, reg_mem.begin());
				float4* result = getStream(*this, instruction->dst, m_particles_count, reg_mem.begin());
				const float4* const end = result + ((m_particles_count + 3) >> 2);

				if (instruction->op1.type == DataStream::LITERAL) {
					const float4 arg1 = f4Splat(instruction->op1.value);

					for (; result != end; ++result, ++arg0) {
						*result = f4Mul(*arg0, arg1);
					}
				}
				else {
					const float4* arg1 = getStream(*this, instruction->op1, m_particles_count, reg_mem.begin());
					for (; result != end; ++result, ++arg0, ++arg1) {
						*result = f4Mul(*arg0, *arg1);
					}
				}

				break;
			}
			case InstructionType::MOV: {
				float4* result = getStream(*this, instruction->dst, m_particles_count, reg_mem.begin());
				const float4* const end = result + ((m_particles_count + 3) >> 2);
				
				if(instruction->op0.type == DataStream::CONST) {
					ASSERT(instruction->op0.index == 0);
					const float4 src = f4Splat(dt);
					for (; result != end; ++result) {
						*result = src;
					}
				}
				else {
					const float4* src = getStream(*this, instruction->op0, m_particles_count, reg_mem.begin());

					for (; result != end; ++result, ++src) {
						*result = *src;
					}
				}

				break;
			}
			case InstructionType::ADD: {
				// TODO
				float4* result = getStream(*this, instruction->dst, m_particles_count, reg_mem.begin());
				const float4* const end = result + ((m_particles_count + 3) >> 2);
				const float4* arg0 = getStream(*this, instruction->op0, m_particles_count, reg_mem.begin());

				if (instruction->op1.type == DataStream::CONST) { 
					ASSERT(instruction->op1.index == 0);
					const float4 arg1 = f4Splat(dt);

					for (; result != end; ++result, ++arg0) {
						*result = f4Add(*arg0, arg1);
					}
				}
				else {
					const float4* arg1 = getStream(*this, instruction->op1, m_particles_count, reg_mem.begin());

					for (; result != end; ++result, ++arg0, ++arg1) {
						*result = f4Add(*arg0, *arg1);
					}
				}

				break;
			}
			case InstructionType::COS: {
				const float* arg = (float*)getStream(*this, instruction->op0, m_particles_count, reg_mem.begin());
				float* result = (float*)getStream(*this, instruction->dst, m_particles_count, reg_mem.begin());
				const float* const end = result + ((m_particles_count + 3) & ~3);

				for (; result != end; ++result, ++arg) {
					*result = cosf(*arg);
				}
				break;
			}
			case InstructionType::SIN: {
				const float* arg = (float*)getStream(*this, instruction->op0, m_particles_count, reg_mem.begin());
				float* result = (float*)getStream(*this, instruction->dst, m_particles_count, reg_mem.begin());
				const float* const end = result + ((m_particles_count + 3) & ~3);

				for (; result != end; ++result, ++arg) {
					*result = sinf(*arg);
				}
				break;
			}
			default:
				ASSERT(false);
				break;
		}
		++instruction;
	}

	end:
		InputMemoryStream emit_buffer(m_emit_buffer);
		while (emit_buffer.getPosition() < emit_buffer.size())
		{
			u8 count = emit_buffer.read<u8>();
			float args[16];
			ASSERT(count <= lengthOf(args));
			emit_buffer.read(args, sizeof(args[0]) * count);
			emit(args);
		}
}


int ParticleEmitter::getInstanceDataSizeBytes() const
{
	return m_resource ? ((m_particles_count + 3) & ~3) * m_resource->getOutputsCount() * sizeof(float) : 0;
}


void ParticleEmitter::fillInstanceData(const DVec3& cam_pos, float* data)
{
	PROFILE_FUNCTION();

	// TODO
	m_constants[1].value = (float)cam_pos.x;
	m_constants[2].value = (float)cam_pos.y;
	m_constants[3].value = (float)cam_pos.z;
	// TODO
	Array<float4> reg_mem(m_allocator);
	reg_mem.resize(m_resource->getRegistersCount() * ((m_particles_count + 3) >> 2));

	auto sim = [&](u32 offset, u32 count){
		const Instruction* instruction = m_resource->getInstructions().begin() + m_resource->getOutputOffset();
		PROFILE_BLOCK("particle simulation");
		for (;;) {
			switch ((InstructionType)instruction->type) {
				case InstructionType::END:
					return;
				case InstructionType::SIN: {
					const float* arg = (float*)getStream(*this, instruction->op0, m_particles_count, reg_mem.begin()) + offset;
					float* result = (float*)getStream(*this, instruction->dst, m_particles_count, reg_mem.begin()) + offset;
					const float* const end = result + count;

					for (; result != end; ++result, ++arg) {
						*result = sinf(*arg);
					}
					break;
				}
				case InstructionType::COS: {
					const float* arg = (float*)getStream(*this, instruction->op0, m_particles_count, reg_mem.begin()) + offset;
					float* result = (float*)getStream(*this, instruction->dst, m_particles_count, reg_mem.begin()) + offset;
					const float* const end = result + count;

					for (; result != end; ++result, ++arg) {
						*result = cosf(*arg);
					}
					break;
				}
				case InstructionType::MUL: {
					float4* result = getStream(*this, instruction->dst, m_particles_count, reg_mem.begin()) + (offset >> 2);
					const float4* arg0 = getStream(*this, instruction->op0, m_particles_count, reg_mem.begin()) + (offset >> 2);
					const float4* const end = result + (count >> 2);
					if (instruction->op1.type == DataStream::LITERAL) {
						const float4 arg1 = f4Splat(instruction->op1.value);

						for (; result != end; ++result, ++arg0) {
							*result = f4Mul(*arg0, arg1);
						}
					}
					else {
						const float4* arg1 = getStream(*this, instruction->op1, m_particles_count, reg_mem.begin()) + (offset >> 2);

						for (; result != end; ++result, ++arg0, ++arg1) {
							*result = f4Mul(*arg0, *arg1);
						}
					}

					break;
				}
				case InstructionType::MOV: {
					const int stride = m_resource->getOutputsCount();
					const float* arg = (float*)getStream(*this, instruction->op0, m_particles_count, reg_mem.begin()) + offset;
					ASSERT(instruction->dst.type == DataStream::OUT);
					i32 output_idx = instruction->dst.index;
					float* dst = data + output_idx + offset * stride;
					++output_idx;
					for (u32 i = 0, j = 0; i < count; ++i, j += stride) {
						dst[j] =  arg[i];
					}
					break;
				}
				default:
					ASSERT(false);
					break;
			}
			++instruction;
		}
	};
	JobSystem::forEach(m_particles_count, 16 * 1024, [&](i32 from, i32 to){
		sim((u32)from, (to + 3) & ~3);
	});
}


} // namespace Lumix