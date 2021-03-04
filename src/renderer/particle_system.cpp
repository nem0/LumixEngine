#include "particle_system.h"
#include "engine/crc32.h"
#include "engine/crt.h"
#include "engine/atomic.h"
#include "engine/debug.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/profiler.h"
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
using InstructionType = ParticleEmitterResource::InstructionType;

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
	if (m_material) {
		Material* tmp = m_material;
		m_material = nullptr;
		removeDependency(*tmp);
		tmp->decRefCount();
	}
	m_instructions.clear();
}


void ParticleEmitterResource::overrideData(OutputMemoryStream&& instructions,
	int emit_offset,
	int output_offset,
	int channels_count,
	int registers_count,
	int outputs_count
)
{
	++m_empty_dep_count;
	checkState();
	
	m_instructions = static_cast<OutputMemoryStream&&>(instructions);
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
	const u32 isize = blob.read<u32>();
	m_instructions.resize(isize);
	blob.read(m_instructions.getMutableData(), m_instructions.size());
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
	for (const Channel& c : m_channels) {
		m_allocator.deallocate_aligned(c.data);
	}
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

	const OutputMemoryStream& instructions = m_resource->getInstructions();

	InputMemoryStream ip(instructions);
	ip.skip(m_resource->getEmitOffset());
	for (;;) {
		switch (ip.read<InstructionType>()) {
			case InstructionType::END:
				++m_particles_count;
				return;
			case InstructionType::MOV: {
				DataStream dst = ip.read<DataStream>();
				DataStream op0 = ip.read<DataStream>();
				m_channels[dst.index].data[m_particles_count] = op0.value;
				break;
			}
			case InstructionType::RAND: {
				DataStream dst = ip.read<DataStream>();
				float from = ip.read<float>();
				float to = ip.read<float>();
				
				m_channels[dst.index].data[m_particles_count] = randFloat(from, to);
				break;
			}
			default:
				ASSERT(false);
				break;
		}
	}
}


void ParticleEmitter::serialize(OutputMemoryStream& blob)
{
	blob.write(m_entity);
	blob.write(m_emit_rate);
	blob.writeString(m_resource ? m_resource->getPath().c_str() : "");
}


void ParticleEmitter::deserialize(InputMemoryStream& blob, ResourceManagerHub& manager)
{
	blob.read(m_entity);
	blob.read(m_emit_rate);
	const char* path = blob.readString();
	auto* res = manager.load<ParticleEmitterResource>(Path(path));
	setResource(res);
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
	LiteralGetter(DataStream stream) : stream(stream) {}

	float4* get(const ParticleEmitter& emitter, i32, i32, float4*) {
		value = f4Splat(stream.value);
		return &value;
	}

	static void step(float4*&) {}
	float4 value;
	DataStream stream;
};

struct ChannelGetter {
	ChannelGetter(DataStream stream) : stream(stream) {}
	float4* get(const ParticleEmitter& emitter, i32, i32, float4*) {
		return (float4*)emitter.getChannelData(stream.index);
	}
	static void step(float4*& val) { ++val; }
	DataStream stream;
};

struct ConstGetter {
	ConstGetter(DataStream stream) : stream(stream) {}

	float4* get(const ParticleEmitter& emitter, i32, i32, float4*) {
		value = f4Splat(emitter.m_constants[stream.index]);
		return &value;
	}
	static void step(float4*& val) {}

	float4 value;
	DataStream stream;
};

struct RegisterGetter {
	RegisterGetter(DataStream stream) : stream(stream) {}

	float4* get(const ParticleEmitter& emitter, i32, i32 stepf4, float4* reg_mem) {
		return reg_mem + 256 * stream.index;
	}

	static void step(float4*& val) { ++val; }
	DataStream stream;
};

struct BinaryHelper {
	template <auto F, typename... T>
	void run(DataStream dst, InputMemoryStream& ip, T&... args) {
		const DataStream stream = ip.read<DataStream>();
		switch(stream.type) {
			case DataStream::CHANNEL: { ChannelGetter tmp(stream); run<F>(dst, ip, args..., tmp); break; }
			case DataStream::LITERAL: { LiteralGetter tmp(stream); run<F>(dst, ip, args..., tmp); break; }
			case DataStream::CONST: { ConstGetter tmp(stream); run<F>(dst, ip, args..., tmp); break; }
			case DataStream::REGISTER: { RegisterGetter tmp(stream); run<F>(dst, ip, args..., tmp); break; }
			default: ASSERT(false);
		}
	}

	template <auto F, typename T0, typename T1>
	void run(DataStream dst, InputMemoryStream& ip, T0& t0, T1& t1) {

		float4* arg0 = t0.get(*emitter, fromf4, stepf4, reg_mem);
		float4* arg1 = t1.get(*emitter, fromf4, stepf4, reg_mem);
		
		if (dst.type == DataStream::OUT) {
			const u32 stride = emitter->getResource()->getOutputsCount();
			for (i32 i = 0; i < stepf4; ++i, T0::step(arg0), T1::step(arg1)) {
				float4 tmp = F(*arg0, *arg1);
				u32 idx = dst.index + (fromf4 + i) * 4 * stride;
				out_mem[idx] = f4GetX(tmp);
				idx += stride;
				out_mem[idx] = f4GetY(tmp);
				idx += stride;
				out_mem[idx] = f4GetZ(tmp);
				idx += stride;
				out_mem[idx] = f4GetW(tmp);
			}
		}
		else {
			float4* result = getStream(*emitter, dst, fromf4, reg_mem);
			const float4* const end = result + stepf4;

			for (; result != end; ++result, T0::step(arg0), T1::step(arg1)) {
				*result = F(*arg0, *arg1);
			}
		}
	}

	ParticleEmitter* emitter;
	i32 fromf4;
	i32 stepf4;
	float4* reg_mem;
	float* out_mem = nullptr;
};

struct TernaryHelper {
	static float4 madd(float4 a, float4 b, float4 c) {
		return f4Add(f4Mul(a, b), c);
	}

	static float4 mix(float4 a, float4 b, float4 c) {
		float4 invc = f4Sub(f4Splat(1.f), c);
		return f4Add(f4Mul(b, c), f4Mul(a, invc));
	}

	template <auto F, typename... T>
	void run(DataStream dst, InputMemoryStream& ip, T&... args) {
		const DataStream stream = ip.read<DataStream>();
		switch(stream.type) {
			case DataStream::CHANNEL: { ChannelGetter tmp(stream); run<F>(dst, ip, args..., tmp); break; }
			case DataStream::LITERAL: { LiteralGetter tmp(stream); run<F>(dst, ip, args..., tmp); break; }
			case DataStream::CONST: { ConstGetter tmp(stream); run<F>(dst, ip, args..., tmp); break; }
			case DataStream::REGISTER: { RegisterGetter tmp(stream); run<F>(dst, ip, args..., tmp); break; }
			default: ASSERT(false);
		}
	}

	template <auto F, typename T0, typename T1, typename T2>
	void run(DataStream dst, InputMemoryStream& ip, T0& t0, T1& t1, T2& t2) {
		float4* arg0 = t0.get(*emitter, fromf4, stepf4, reg_mem);
		float4* arg1 = t1.get(*emitter, fromf4, stepf4, reg_mem);
		float4* arg2 = t2.get(*emitter, fromf4, stepf4, reg_mem);

		if (dst.type == DataStream::OUT) {
			const u32 stride = emitter->getResource()->getOutputsCount();
			for (i32 i = 0; i < stepf4; ++i, T0::step(arg0), T1::step(arg1), T2::step(arg2)) {
				float4 tmp = F(*arg0, *arg1, *arg2);
				u32 idx = dst.index + (fromf4 + i) * 4 * stride;
				out_mem[idx] = f4GetX(tmp);
				idx += stride;
				out_mem[idx] = f4GetY(tmp);
				idx += stride;
				out_mem[idx] = f4GetZ(tmp);
				idx += stride;
				out_mem[idx] = f4GetW(tmp);
			}
		}
		else {
			float4* result = getStream(*emitter, dst, fromf4, reg_mem);
			const float4* const end = result + stepf4;

			for (; result != end; ++result, T0::step(arg0), T1::step(arg1), T2::step(arg2)) {
				*result = F(*arg0, *arg1, *arg2);
			}
		}
	}

	ParticleEmitter* emitter;
	i32 fromf4;
	i32 stepf4;
	float4* reg_mem;
	float* out_mem = nullptr;
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

	profiler::pushInt("particle count", m_particles_count);
	if (m_particles_count == 0) return;

	m_emit_buffer.clear();
	m_constants[0] = dt;
	// TODO
	m_instances_count = m_particles_count;
	Array<u32> kill_list(m_allocator);
	kill_list.resize(4096);
	volatile i32 kill_counter = 0;

	volatile i32 counter = 0;
	jobs::runOnWorkers([&](){
		PROFILE_FUNCTION();
		Array<float4> reg_mem(m_allocator);
		reg_mem.resize(m_resource->getRegistersCount() * 256);
		for (;;) {
			const i32 from = atomicAdd(&counter, 1024);
			if (from >= (i32)m_particles_count) return;

			const i32 fromf4 = from / 4;
			const i32 stepf4 = minimum(1024, m_particles_count - from + 3) / 4;
			InputMemoryStream ip = InputMemoryStream(m_resource->getInstructions());
			InstructionType itype = ip.read<InstructionType>();

			while (itype != InstructionType::END) {
				switch (itype) {
					case InstructionType::GT: {
						DataStream dst = ip.read<DataStream>();
						DataStream op0 = ip.read<DataStream>();
						const float4* arg0 = getStream(*this, dst, fromf4, reg_mem.begin());
						ASSERT(op0.type == DataStream::LITERAL);
						const float4 arg1 = f4Splat(op0.value);
						const float4* end = arg0 + stepf4;
					
						const InstructionType inner_type = ip.read<InstructionType>();
						for (const float4* beg = arg0; arg0 != end; ++arg0) {
							const float4 tmp = f4CmpGT(*arg0, arg1);
							const int m = f4MoveMask(tmp);
							for (int i = 0; i < 4; ++i) {
								if ((m & (1 << i))) {
									switch(inner_type) {
										case InstructionType::KILL: {
											const u32 idx = u32(from + (arg0 - beg) * 4 + i);
											if (idx < m_particles_count) {
												const i32 kill_idx = atomicIncrement(&kill_counter) - 1;
												if (kill_idx < kill_list.size()) {
													kill_list[kill_idx] = idx;
												}
												else {
													ASSERT(false);
												}
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
						const DataStream dst = ip.read<DataStream>();
						helper.run<f4Mul>(dst, ip);
					}
					case InstructionType::MULTIPLY_ADD: {
						TernaryHelper helper;
						helper.emitter = this;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						const DataStream dst = ip.read<DataStream>();
						helper.run<TernaryHelper::madd>(dst, ip);
						break;
					}
					case InstructionType::ADD: {
						BinaryHelper helper;
						helper.emitter = this;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						const DataStream dst = ip.read<DataStream>();
						helper.run<f4Add>(dst, ip);
						break;
					}
					case InstructionType::MOV: {
						const DataStream dst = ip.read<DataStream>();
						const DataStream op0 = ip.read<DataStream>();
						float4* result = getStream(*this, dst, fromf4, reg_mem.begin());
						const float4* const end = result + stepf4;
				
						if (op0.type == DataStream::CONST) {
							ASSERT(op0.index == 0);
							const float4 src = f4Splat(dt);
							for (; result != end; ++result) {
								*result = src;
							}
						}
						else {
							const float4* src = getStream(*this, op0, fromf4, reg_mem.begin());

							for (; result != end; ++result, ++src) {
								*result = *src;
							}
						}

						break;
					}
					case InstructionType::COS: {
						const DataStream dst = ip.read<DataStream>();
						const DataStream op0 = ip.read<DataStream>();
						const float* arg = (float*)getStream(*this, op0, fromf4, reg_mem.begin());
						float* result = (float*)getStream(*this, dst, fromf4, reg_mem.begin());
						const float* const end = result + stepf4 * 4;

						for (; result != end; ++result, ++arg) {
							*result = cosf(*arg);
						}
						break;
					}
					case InstructionType::SIN: {
						const DataStream dst = ip.read<DataStream>();
						const DataStream op0 = ip.read<DataStream>();
						const float* arg = (float*)getStream(*this, op0, fromf4, reg_mem.begin());
						float* result = (float*)getStream(*this, dst, fromf4, reg_mem.begin());
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
				itype = ip.read<InstructionType>();
			}
		}
	});

	if (kill_counter > 0) {
		ASSERT(kill_counter <= (i32)m_particles_count);
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
	jobs::runOnWorkers([&](){
		PROFILE_FUNCTION();
		Array<float4> reg_mem(m_allocator);
		reg_mem.resize(m_resource->getRegistersCount() * 256);
		for (;;) {
			const i32 from = atomicAdd(&counter, 1024);
			if (from >= (i32)m_particles_count) return;
			const i32 fromf4 = from / 4;
			const i32 stepf4 = minimum(1024, m_particles_count - from + 3) / 4;

			InputMemoryStream ip(m_resource->getInstructions());
			ip.skip(m_resource->getOutputOffset());

			InstructionType itype = ip.read<InstructionType>();
			while (itype != InstructionType::END) {
				switch (itype) {
					case InstructionType::SIN: {
						DataStream dst = ip.read<DataStream>();
						DataStream op0 = ip.read<DataStream>();
						const float* arg = (float*)getStream(*this, op0, fromf4, reg_mem.begin());
						
						if (dst.type == DataStream::OUT) {
							i32 output_idx = dst.index;
							const int stride = m_resource->getOutputsCount();
							float* dst = data + output_idx + fromf4 * 4 * stride;
							for (i32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
								dst[j] = sinf(arg[i]);
							}
						}
						else {
							float* result = (float*)getStream(*this, dst, fromf4, reg_mem.begin());
							const float* const end = result + stepf4 * 4;

							for (; result != end; ++result, ++arg) {
								*result = sinf(*arg);
							}
						}
						break;
					}
					case InstructionType::COS: {
						DataStream dst = ip.read<DataStream>();
						DataStream op0 = ip.read<DataStream>();
						const float* arg = (float*)getStream(*this, op0, fromf4, reg_mem.begin());
						if (dst.type == DataStream::OUT) {
							i32 output_idx = dst.index;
							const int stride = m_resource->getOutputsCount();
							float* dst = data + output_idx + fromf4 * 4 * stride;
							for (i32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
								dst[j] = cosf(arg[i]);
							}
						}
						else {
							float* result = (float*)getStream(*this, dst, fromf4, reg_mem.begin());
							const float* const end = result + stepf4 * 4;

							for (; result != end; ++result, ++arg) {
								*result = cosf(*arg);
							}
						}
						break;
					}
					case InstructionType::MULTIPLY_ADD: {
						TernaryHelper helper;
						helper.emitter = this;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						helper.out_mem = data;
						DataStream dst = ip.read<DataStream>();
						helper.run<TernaryHelper::madd>(dst, ip);
						break;
					}
					case InstructionType::MIX: {
						TernaryHelper helper;
						helper.emitter = this;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						helper.out_mem = data;
						DataStream dst = ip.read<DataStream>();
						helper.run<TernaryHelper::mix>(dst, ip);
						break;
					}
					case InstructionType::MUL: {
						BinaryHelper helper;
						helper.emitter = this;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						helper.out_mem = data;
						DataStream dst = ip.read<DataStream>();
						helper.run<f4Mul>(dst, ip);
						break;
					}
					case InstructionType::ADD: {
						BinaryHelper helper;
						helper.emitter = this;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						helper.out_mem = data;
						DataStream dst = ip.read<DataStream>();
						helper.run<f4Add>(dst, ip);
						break;
					}
					case InstructionType::GRADIENT: {
						DataStream dst = ip.read<DataStream>();
						DataStream op0 = ip.read<DataStream>();
						u32 count = ip.read<u32>();
						float keys[8];
						float values[8];
						ip.read(keys, sizeof(keys[0]) * count);
						ip.read(values, sizeof(values[0]) * count);

						ASSERT(dst.type == DataStream::OUT);
						const i32 output_idx = dst.index;
						const i32 stride = m_resource->getOutputsCount();
						const float* arg = (float*)getStream(*this, op0, fromf4, reg_mem.begin());
						float* out = data + output_idx + fromf4 * 4 * stride;
						for (i32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
							if (arg[i] < keys[0]) {
								out[j] = values[0];
							}
							else if (arg[i] >= keys[count - 1]) {
								out[j] = values[count - 1];
							}
							else {
								for (u32 k = 1; k < count; ++k) {
									if (arg[i] < keys[k]) {
										const float t = (arg[i] - keys[k - 1]) / (keys[k] - keys[k - 1]);
										ASSERT(t >= 0 && t <= 1);
										out[j] = t * values[k] + (1 - t) * values[k - 1];
										break;
									}
								}
							}
						}
						break;
					}
					case InstructionType::MOV: {
						const int stride = m_resource->getOutputsCount();
						DataStream dst = ip.read<DataStream>();
						DataStream op0 = ip.read<DataStream>();

						if (op0.type == DataStream::LITERAL) {
							const float arg = op0.value;
							ASSERT(dst.type == DataStream::OUT);
							i32 output_idx = dst.index;
							float* res = data + output_idx + fromf4 * 4 * stride;
							for (i32 i = 0; i < stepf4 * 4; ++i) {
								res[i * stride] = arg;
							}
						}
						else {
							const float* arg = (float*)getStream(*this, op0, fromf4, reg_mem.begin());
							ASSERT(dst.type == DataStream::OUT);
							i32 output_idx = dst.index;
							float* res = data + output_idx + fromf4 * 4 * stride;
							for (i32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
								res[j] = arg[i];
							}
						}
						break;
					}
					default:
						ASSERT(false);
						break;
				}
				itype = ip.read<InstructionType>();
			}
		}
	});
}


} // namespace Lumix