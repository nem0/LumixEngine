#include "particle_system.h"
#include "engine/crc32.h"
#include "engine/crt.h"
#include "engine/atomic.h"
#include "engine/debug.h"
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
		logError("Invalid file ", getPath());
		return false;
	}
	if (header.version != 0) {
		logError("Unsupported version ", getPath());
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
	blob.write(m_emit_rate);
	blob.writeString(m_resource ? m_resource->getPath().c_str() : "");
	blob.writeString(m_material ? m_material->getPath().c_str() : "");
}


void ParticleEmitter::deserialize(InputMemoryStream& blob, ResourceManagerHub& manager)
{
	blob.read(m_entity);
	blob.read(m_emit_rate);
	const char* path = blob.readString();
	auto* res = manager.load<ParticleEmitterResource>(Path(path));
	setResource(res);

	const char* matpath = blob.readString();
	auto* mat = manager.load<Material>(Path(matpath));
	setMaterial(mat);
}

static float4* getStream(const ParticleEmitter& emitter
	, DataStream stream
	, u32 offset
	, float4* register_mem)
{
	switch (stream.type) {
		case DataStream::CHANNEL: return (float4*)emitter.getChannelData(stream.index) + offset; //-V1032
		case DataStream::REGISTER: return register_mem + 256 * stream.index;
		default: ASSERT(false); return nullptr;
	}
}

struct LiteralGetter {
	float4* get(const ParticleEmitter& emitter, DataStream stream, i32, i32, float4*) {
		value = f4Splat(stream.value);
		return &value;
	}

	static void step(float4*&) {}
	float4 value;
};

struct ChannelGetter {
	float4* get(const ParticleEmitter& emitter, DataStream stream, i32, i32, float4*) {
		return (float4*)emitter.getChannelData(stream.index);
	}
	static void step(float4*& val) { ++val; }
};

struct ConstGetter {
	float4* get(const ParticleEmitter& emitter, DataStream stream, i32, i32, float4*) {
		value = f4Splat(emitter.m_constants[stream.index]);
		return &value;
	}
	static void step(float4*& val) {}

	float4 value;
};

struct BinaryHelper {
	template <auto F, typename... T>
	void run(const Instruction& instruction, T&... args) {
		const DataStream* stream = &instruction.op0 + sizeof...(args);
		switch(stream->type) {
			case DataStream::CHANNEL: { ChannelGetter tmp; run<F>(instruction, args..., tmp); break; }
			case DataStream::LITERAL: { LiteralGetter tmp; run<F>(instruction, args..., tmp); break; }
			case DataStream::CONST: { ConstGetter tmp; run<F>(instruction, args..., tmp); break; }
			default: ASSERT(false);
		}
	}

	template <auto F, typename T0, typename T1>
	void run(const Instruction& instruction, T0& t0, T1& t1) {
		float4* result = getStream(*emitter, instruction.dst, fromf4, reg_mem);

		float4* arg0 = t0.get(*emitter, instruction.op0, fromf4, stepf4, reg_mem);
		float4* arg1 = t1.get(*emitter, instruction.op1, fromf4, stepf4, reg_mem);
		
		const float4* const end = result + stepf4;

		for (; result != end; ++result, T0::step(arg0), T1::step(arg1)) {
			*result = F(*arg0, *arg1);
		}
	}

	ParticleEmitter* emitter;
	i32 fromf4;
	i32 stepf4;
	float4* reg_mem;
};

struct TernaryHelper {
	static float4 madd(float4 a, float4 b, float4 c) {
		return f4Add(f4Mul(a, b), c);
	}

	template <auto F, typename... T>
	void run(const Instruction& instruction, T&... args) {
		const DataStream* stream = &instruction.op0 + sizeof...(args);
		switch(stream->type) {
			case DataStream::CHANNEL: { ChannelGetter tmp; run<F>(instruction, args..., tmp); break; }
			case DataStream::LITERAL: { LiteralGetter tmp; run<F>(instruction, args..., tmp); break; }
			case DataStream::CONST: { ConstGetter tmp; run<F>(instruction, args..., tmp); break; }
			default: ASSERT(false);
		}
	}

	template <auto F, typename T0, typename T1, typename T2>
	void run(const Instruction& instruction, T0& t0, T1& t1, T2& t2) {
		float4* result = getStream(*emitter, instruction.dst, fromf4, reg_mem);

		float4* arg0 = t0.get(*emitter, instruction.op0, fromf4, stepf4, reg_mem);
		float4* arg1 = t1.get(*emitter, instruction.op1, fromf4, stepf4, reg_mem);
		float4* arg2 = t2.get(*emitter, instruction.op2, fromf4, stepf4, reg_mem);
		
		const float4* const end = result + stepf4;

		for (; result != end; ++result, T0::step(arg0), T1::step(arg1), T2::step(arg2)) {
			*result = F(*arg0, *arg1, *arg2);
		}
	}

	ParticleEmitter* emitter;
	i32 fromf4;
	i32 stepf4;
	float4* reg_mem;
};


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

	Profiler::pushInt("particle count", m_particles_count);
	if (m_particles_count == 0) return;

	m_emit_buffer.clear();
	m_constants[0] = dt;
	// TODO
	m_instances_count = m_particles_count;
	Array<u32> kill_list(m_allocator);
	kill_list.resize(4096);
	volatile i32 kill_counter = 0;

	volatile i32 counter = 0;
	JobSystem::runOnWorkers([&](){
		PROFILE_FUNCTION();
		Array<float4> reg_mem(m_allocator);
		reg_mem.resize(m_resource->getRegistersCount() * 256);
		for (;;) {
			const i32 from = atomicAdd(&counter, 1024);
			if (from >= (i32)m_particles_count) return;

			const i32 fromf4 = from / 4;
			const i32 stepf4 = minimum(1024, m_particles_count - from + 3) / 4;
			const Instruction* instruction = m_resource->getInstructions().begin();
			while (instruction->type != InstructionType::END) {
				switch ((InstructionType)instruction->type) {
					case InstructionType::GT: {
						const float4* arg0 = getStream(*this, instruction->dst, fromf4, reg_mem.begin());
						ASSERT(instruction->op0.type == DataStream::LITERAL);
						const float4 arg1 = f4Splat(instruction->op0.value);
						const float4* end = arg0 + stepf4;
					
						++instruction;
						for (const float4* beg = arg0; arg0 != end; ++arg0) {
							const float4 tmp = f4CmpGT(*arg0, arg1);
							const int m = f4MoveMask(tmp);
							for (int i = 0; i < 4; ++i) {
								if ((m & (1 << i))) {
									switch(instruction->type) {
										case InstructionType::KILL: {
											const i32 kill_idx = atomicIncrement(&kill_counter) - 1;
											if (kill_idx < kill_list.size()) {
												kill_list[kill_idx] = u32(from + (arg0 - beg) * 4 + i);
											}
											else {
												//ASSERT(false);
											}
											break;
										}
										default: ASSERT(false); break;
									}
								}
							}
						}
						break;
					}	
					case InstructionType::MUL: {
						BinaryHelper helper;
						helper.emitter = this;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						helper.run<f4Mul>(*instruction);
					}
					case InstructionType::MULTIPLY_ADD: {
						TernaryHelper helper;
						helper.emitter = this;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						helper.run<TernaryHelper::madd>(*instruction);
						break;
					}
					case InstructionType::MOV: {
						float4* result = getStream(*this, instruction->dst, fromf4, reg_mem.begin());
						const float4* const end = result + stepf4;
				
						if(instruction->op0.type == DataStream::CONST) {
							ASSERT(instruction->op0.index == 0);
							const float4 src = f4Splat(dt);
							for (; result != end; ++result) {
								*result = src;
							}
						}
						else {
							const float4* src = getStream(*this, instruction->op0, fromf4, reg_mem.begin());

							for (; result != end; ++result, ++src) {
								*result = *src;
							}
						}

						break;
					}
					case InstructionType::ADD: {
						BinaryHelper helper;
						helper.emitter = this;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						helper.run<f4Add>(*instruction);
						break;
					}
			
					case InstructionType::COS: {
						const float* arg = (float*)getStream(*this, instruction->op0, fromf4, reg_mem.begin());
						float* result = (float*)getStream(*this, instruction->dst, fromf4, reg_mem.begin());
						const float* const end = result + stepf4 * 4;

						for (; result != end; ++result, ++arg) {
							*result = cosf(*arg);
						}
						break;
					}
					case InstructionType::SIN: {
						const float* arg = (float*)getStream(*this, instruction->op0, fromf4, reg_mem.begin());
						float* result = (float*)getStream(*this, instruction->dst, fromf4, reg_mem.begin());
						const float* const end = result + stepf4 * 4;

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
		}
	});

	if (kill_counter > 0) {
		kill_counter = minimum(kill_counter, kill_list.size());
		qsort(kill_list.begin(), kill_counter, sizeof(u32), [](const void* a, const void* b) -> int {
			const u32 i = *(u32*)a;
			const u32 j = *(u32*)b;
			if (i < j) return -1;
			if (i > j) return 1;
			return 0;
		});
		const i32 channels_count = m_resource->getChannelsCount();
		for (i32 j = kill_counter - 1; j >= 0; --j) {
			const u32 last = m_particles_count - 1;
			const u32 particle_index = kill_list[j];
			for (i32 i = 0; i < channels_count; ++i) {
				float* data = m_channels[i].data;
				data[particle_index] = data[last];
			}
			--m_particles_count;
		}
	}
}


int ParticleEmitter::getInstanceDataSizeBytes() const
{
	return m_resource ? ((m_particles_count + 3) & ~3) * m_resource->getOutputsCount() * sizeof(float) : 0;
}


void ParticleEmitter::fillInstanceData(float* data) {
	if (m_particles_count == 0) return;

	volatile i32 counter = 0;
	JobSystem::runOnWorkers([&](){
		PROFILE_FUNCTION();
		Array<float4> reg_mem(m_allocator);
		reg_mem.resize(m_resource->getRegistersCount() * 256);
		for (;;) {
			const i32 from = atomicAdd(&counter, 1024);
			if (from >= (i32)m_particles_count) return;
			const i32 fromf4 = from / 4;
			const i32 stepf4 = minimum(1024, m_particles_count - from + 3) / 4;

			const Instruction* instruction = m_resource->getInstructions().begin() + m_resource->getOutputOffset();
			while (instruction->type != InstructionType::END) {
				switch ((InstructionType)instruction->type) {
					case InstructionType::SIN: {
						const float* arg = (float*)getStream(*this, instruction->op0, fromf4, reg_mem.begin());
						float* result = (float*)getStream(*this, instruction->dst, fromf4, reg_mem.begin());
						const float* const end = result + stepf4 * 4;

						for (; result != end; ++result, ++arg) {
							*result = sinf(*arg);
						}
						break;
					}
					case InstructionType::COS: {
						const float* arg = (float*)getStream(*this, instruction->op0, fromf4, reg_mem.begin());
						float* result = (float*)getStream(*this, instruction->dst, fromf4, reg_mem.begin());
						const float* const end = result + stepf4 * 4;

						for (; result != end; ++result, ++arg) {
							*result = cosf(*arg);
						}
						break;
					}
					case InstructionType::MUL: {
						float4* result = getStream(*this, instruction->dst, fromf4, reg_mem.begin());
						const float4* arg0 = getStream(*this, instruction->op0, fromf4, reg_mem.begin());
						const float4* const end = result + stepf4;
						if (instruction->op1.type == DataStream::LITERAL) {
							const float4 arg1 = f4Splat(instruction->op1.value);

							for (; result != end; ++result, ++arg0) {
								*result = f4Mul(*arg0, arg1);
							}
						}
						else {
							const float4* arg1 = getStream(*this, instruction->op1, fromf4, reg_mem.begin());

							for (; result != end; ++result, ++arg0, ++arg1) {
								*result = f4Mul(*arg0, *arg1);
							}
						}

						break;
					}
					case InstructionType::ADD: {
						if (instruction->dst.type == DataStream::OUT) {
							const int stride = m_resource->getOutputsCount();
							const float* arg0 = (float*)getStream(*this, instruction->op0, fromf4, reg_mem.begin());
							const float* arg1 = (float*)getStream(*this, instruction->op1, fromf4, reg_mem.begin());
							i32 output_idx = instruction->dst.index;
							float* dst = data + output_idx + fromf4 * 4 * stride;
							for (i32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
								dst[j] = arg0[i] + arg1[i];
							}
							break;
						}
						float4* result = getStream(*this, instruction->dst, fromf4, reg_mem.begin());
						const float4* arg0 = getStream(*this, instruction->op0, fromf4, reg_mem.begin());
						const float4* const end = result + stepf4;
						if (instruction->op1.type == DataStream::LITERAL) {
							const float4 arg1 = f4Splat(instruction->op1.value);

							for (; result != end; ++result, ++arg0) {
								*result = f4Add(*arg0, arg1);
							}
						}
						else {
							const float4* arg1 = getStream(*this, instruction->op1, fromf4, reg_mem.begin());

							for (; result != end; ++result, ++arg0, ++arg1) {
								*result = f4Add(*arg0, *arg1);
							}
						}

						break;
					}
					case InstructionType::MOV: {
						const int stride = m_resource->getOutputsCount();
						const float* arg = (float*)getStream(*this, instruction->op0, fromf4, reg_mem.begin());
						ASSERT(instruction->dst.type == DataStream::OUT);
						i32 output_idx = instruction->dst.index;
						float* dst = data + output_idx + fromf4 * 4 * stride;
						for (i32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
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
		}
	});
}


} // namespace Lumix