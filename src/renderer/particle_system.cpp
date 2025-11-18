#include "particle_system.h"
#include "core/crt.h"
#include "core/atomic.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/math.h"
#include "core/metaprogramming.h"
#include "core/page_allocator.h"
#include "core/profiler.h"
#include "core/simd.h"
#include "core/stack_array.h"
#include "core/stream.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "engine/component_types.h"
#include "engine/core.h"
#include "engine/resource_manager.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_module.h"
#include "engine/world.h"

namespace Lumix
{

using DataStream = ParticleSystemResource::DataStream;
using InstructionType = ParticleSystemResource::InstructionType;

const ResourceType ParticleSystemResource::TYPE = ResourceType("particle_emitter");

ParticleSystemResource::Emitter::~Emitter() {
	if (material) material->decRefCount();
	if (model) model->decRefCount();
}

ParticleSystemResource::Emitter::Emitter(ParticleSystemResource& resource)
	: resource(resource)
	, instructions(resource.m_allocator)
	, vertex_decl(gpu::PrimitiveType::TRIANGLE_STRIP)
{
}

ParticleSystemResource::ParticleSystemResource(const Path& path
	, ResourceManager& manager
	, Renderer& renderer
	, IAllocator& allocator
)
	: Resource(path, manager, allocator)
	, m_allocator(allocator)
	, m_emitters(allocator)
	, m_globals(allocator)
{
}


void ParticleSystemResource::unload()
{
	for (Emitter& emitter : m_emitters) {
		if (emitter.model) {
			Model* tmp = emitter.model;
			emitter.model = nullptr;
			removeDependency(*tmp);
			tmp->decRefCount();
		}
		if (emitter.material) {
			Material* tmp = emitter.material;
			emitter.material = nullptr;
			removeDependency(*tmp);
			tmp->decRefCount();
		}
		emitter.instructions.clear();
	}
	m_emitters.clear();
	m_globals.clear();
}


void ParticleSystemResource::overrideData(u32 emitter_idx
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
)
{
	++m_empty_dep_count;
	checkState();
	
	Emitter& emitter = m_emitters[emitter_idx];

	emitter.instructions = static_cast<OutputMemoryStream&&>(instructions);
	emitter.emit_offset = emit_offset;
	emitter.output_offset = output_offset;
	emitter.channels_count = channels_count;
	emitter.emit_registers_count = registers_count;
	emitter.update_registers_count = registers_count;
	emitter.output_registers_count = registers_count;
	emitter.outputs_count = outputs_count;
	emitter.init_emit_count = init_emit_count;
	emitter.emit_per_second = emit_rate;
	emitter.emit_inputs_count = emit_inputs_count;
	emitter.setMaterial(material);
	
	--m_empty_dep_count;
	checkState();
}

void ParticleSystemResource::Emitter::setMaterial(const Path& path) {
	Material* new_material = resource.m_resource_manager.getOwner().load<Material>(path);
	if (material) {
		Material* tmp = material;
		material = nullptr;
		resource.removeDependency(*tmp);
		tmp->decRefCount();
	}
	material = new_material;
	if (material) {
		resource.addDependency(*material);
		setModel(Path());
	}
}

void ParticleSystemResource::Emitter::setModel(const Path& path) {
	Model* new_model = resource.m_resource_manager.getOwner().load<Model>(path);
	if (model) {
		Model* tmp = model;
		model = nullptr;
		resource.removeDependency(*tmp);
		tmp->decRefCount();
	}
	model = new_model;
	if (model) {
		resource.addDependency(*model);
		setMaterial(Path());
	}
}

bool ParticleSystemResource::load(Span<const u8> mem) {
	Header header;
	InputMemoryStream blob(mem);
	blob.read(header);
	if (header.magic != Header::MAGIC) {
		logError("Invalid file ", getPath());
		return false;
	}

	if (header.version <= Version::NOT_SUPPORTED_BEFORE) {
		logError("Version too old, please rebuild ", getPath());
		return false;		
	}

	if (header.version > Version::LAST) {
		logError("Unsupported version ", getPath());
		return false;
	}

	blob.read(m_flags);

	u32 emitter_count = emitter_count = blob.read<u32>();

	for (u32 i = 0; i < emitter_count; ++i) {
		Emitter& emitter = m_emitters.emplace(*this);
		blob.read(emitter.vertex_decl);

		emitter.setMaterial(Path(blob.readString()));
		emitter.setModel(Path(blob.readString()));
		const u32 isize = blob.read<u32>();
		emitter.instructions.resize(isize);
		blob.read(emitter.instructions.getMutableData(), emitter.instructions.size());
		blob.read(emitter.emit_offset);
		blob.read(emitter.output_offset);
		blob.read(emitter.channels_count);
		blob.read(emitter.update_registers_count); 
		blob.read(emitter.emit_registers_count);
		blob.read(emitter.output_registers_count);
		blob.read(emitter.update_instructions_count); 
		blob.read(emitter.emit_instructions_count);
		blob.read(emitter.output_instructions_count);
		blob.read(emitter.outputs_count);
		blob.read(emitter.init_emit_count);
		blob.read(emitter.emit_per_second);
		blob.read(emitter.emit_inputs_count);

		blob.read(emitter.max_ribbons);
		blob.read(emitter.max_ribbon_length);
		emitter.max_ribbon_length = (emitter.max_ribbon_length + 3) & ~3;
		blob.read(emitter.init_ribbons_count);
		emitter.emit_on_move = blob.read<bool>();
	}
	u32 num_globals = blob.read<u32>();
	m_globals.reserve(num_globals);
	u32 offset = 0;
	for (u32 i = 0; i < num_globals; ++i) {
		Global& p = m_globals.emplace(m_allocator);
		p.name = blob.readString();
		p.num_floats = blob.read<u32>();
		p.offset = offset;
		offset += p.num_floats;
	}
	return true;
}

ParticleSystem::ParticleSystem(EntityPtr entity, World& world, IAllocator& allocator)
	: m_allocator(allocator)
	, m_world(world)
	, m_entity(entity)
	, m_emitters(allocator)
	, m_globals(allocator)
{}

ParticleSystem::Emitter::Emitter(ParticleSystem::Emitter&& rhs)
	: system(rhs.system)
	, resource_emitter(rhs.resource_emitter)
	, ribbons(static_cast<Array<Ribbon>&&>(rhs.ribbons))
{
	memcpy(channels, rhs.channels, sizeof(channels));
	memset(rhs.channels, 0, sizeof(rhs.channels));
}

ParticleSystem::ParticleSystem(ParticleSystem&& rhs)
	: m_allocator(rhs.m_allocator)
	, m_world(rhs.m_world)
	, m_emitters(rhs.m_emitters.move())
	, m_resource(rhs.m_resource)
	, m_entity(rhs.m_entity)
	, m_autodestroy(rhs.m_autodestroy)
	, m_total_time(rhs.m_total_time)
	, m_prev_frame_transform(rhs.m_prev_frame_transform)
	, m_last_update_stats(rhs.m_last_update_stats)
	, m_globals(rhs.m_globals.move())
{
	memcpy(m_system_values, rhs.m_system_values, sizeof(m_system_values));
	
	if (rhs.m_resource) {
		rhs.m_resource->getObserverCb().unbind<&ParticleSystem::onResourceChanged>(&rhs);
	}
	rhs.m_resource = nullptr;
	if (m_resource) {
		m_resource->onLoaded<&ParticleSystem::onResourceChanged>(this);
	}
}

ParticleSystem::~ParticleSystem()
{
	setResource(nullptr);
	for (Emitter& emitter : m_emitters) {
		for (const Channel& c : emitter.channels) {
			m_allocator.deallocate(c.data);
		}
	}
}

void ParticleSystem::reset() {
	m_total_time = 0;
	for (Emitter& emitter : m_emitters) {
		emitter.particles_count = 0;
		emitter.emit_index = 0;
		emitter.emit_timer = 0;
	}
}

void ParticleSystem::onResourceChanged(Resource::State old_state, Resource::State new_state, Resource&) {
	if (new_state == Resource::State::READY) {
		for (Emitter& emitter : m_emitters) {
			for (Channel& c : emitter.channels) {
				m_allocator.deallocate(c.data);
			}
		}
		u32 nun_float_globals = 0;
		for (const auto& p : m_resource->getGlobals()) {
			nun_float_globals += p.num_floats;
		}
		m_globals.resize(nun_float_globals);
		if (nun_float_globals > 0) {
			memset(m_globals.begin(), 0, m_globals.byte_size());
		}
		m_emitters.clear();
		for (u32 i = 0, c = m_resource->getEmitters().size(); i < c; ++i) {
			m_emitters.emplace(*this, m_resource->getEmitters()[i]);
		}
	}

	m_total_time = 0;
	for (Emitter& emitter : m_emitters) {
		emitter.emit_timer = 0;
		emitter.particles_count = 0;
		emitter.capacity = 0;
		for (Channel& c : emitter.channels) {
			m_allocator.deallocate(c.data);
			c.data = nullptr;
		}
	}
}

void ParticleSystem::setResource(ParticleSystemResource* res)
{
	if (m_resource) {
		m_resource->getObserverCb().unbind<&ParticleSystem::onResourceChanged>(this);
		m_resource->decRefCount();
	}
	m_resource = res;
	if (m_resource) {
		m_resource->onLoaded<&ParticleSystem::onResourceChanged>(this);
	}
}

namespace {
	float hash(u32 n) {
		// integer hash copied from Hugo Elias
		n = (n << 13U) ^ n;
		n = n * (n * n * 15731U + 789221U) + 1376312589U;
		return float(n & u32(0x0ffFFffFU)) / float(0x0ffFFffF);
	}

	// https://www.shadertoy.com/view/3sd3Rs
	float gnoise(float p) {
		u32 i = u32(floor(p));
		float f = p - i;
		float u = f * f * (3.f - 2.f * f);

		float g0 = hash(i + 0u) * 2.f - 1.f;
		float g1 = hash(i + 1u) * 2.f - 1.f;
		return 2.4f * lerp(g0 * (f - 0.f), g1 * (f - 1.f), u);
	}
}
struct ParticleSystem::RunningContext {
	RunningContext(const Emitter& emitter, IAllocator& allocator)
		: registers(allocator)
		, emitter(emitter)
		, instructions(nullptr, 0)
	{}
	StackArray<float, 16> registers;
	float* output_memory;
	InputMemoryStream instructions;
	const Emitter& emitter;
	jobs::Mutex* emit_mutex = nullptr;
	OutputPagedStream* emit_stream = nullptr;
	u32 ribbon_index;
	u32 particle_idx;
};

void ParticleSystem::ensureCapacity(Emitter& emitter, u32 num_new_particles) {
	u32 new_capacity = num_new_particles + emitter.particles_count;
	if (new_capacity > emitter.capacity) {
		u32 num_channels = emitter.resource_emitter.channels_count;
		if (num_channels > lengthOf(emitter.channels)) {
			logError("Too many emitter channels");
			num_channels = lengthOf(emitter.channels);
		}
		new_capacity = maximum(16, new_capacity, emitter.capacity * 3 / 2);
		new_capacity = (new_capacity + 3) & ~u32(3);
		for (u32 i = 0; i < num_channels; ++i) {
			emitter.channels[i].data = (float*)m_allocator.reallocate(emitter.channels[i].data, new_capacity * sizeof(float), emitter.capacity * sizeof(float), 16);
		}
		emitter.capacity = new_capacity;
	}
}

void ParticleSystem::emitRibbonPoints(u32 emitter_idx, u32 ribbon_idx, Span<const float> emit_data, u32 count, float time_step) {
	Emitter& emitter = m_emitters[emitter_idx];
	const ParticleSystemResource::Emitter& res_emitter = m_resource->getEmitters()[emitter_idx];

	const float c1 = m_system_values[(u8)ParticleSystemValues::TOTAL_TIME];
	Ribbon& ribbon = emitter.ribbons[ribbon_idx];

	RunningContext ctx(emitter, m_allocator);
	ctx.registers.resize(res_emitter.emit_registers_count + emit_data.length());

	const u32 max_len = emitter.resource_emitter.max_ribbon_length;
	m_system_values[(u8)ParticleSystemValues::RIBBON_INDEX] = (float)ribbon_idx;
	for (u32 i = 0; i < count; ++i) {
		m_system_values[(u8)ParticleSystemValues::EMIT_INDEX] = (float)ribbon.emit_index;
		if (emit_data.length() > 0) {
			memcpy(ctx.registers.begin(), emit_data.begin(), emit_data.length() * sizeof(emit_data[0]));
		}
		
		if (ribbon.length < max_len) {
			++ribbon.length;
			++emitter.particles_count;
		}
		else {
			++ribbon.offset;
		}

		ctx.particle_idx = ((ribbon.offset + ribbon.length - 1) % max_len) + ribbon_idx * max_len;
		ctx.instructions.set(res_emitter.instructions.data() + res_emitter.emit_offset, res_emitter.instructions.size() - res_emitter.emit_offset);
		run(ctx);

		++ribbon.emit_index;
		m_system_values[(u8)ParticleSystemValues::TOTAL_TIME] += time_step;
	}
	m_last_update_stats.emitted.add(count);
	m_system_values[(u8)ParticleSystemValues::TOTAL_TIME] = c1;
}

void ParticleSystem::emit(u32 emitter_idx, Span<const float> emit_data, u32 count, float time_step) {
	Emitter& emitter = m_emitters[emitter_idx];
	const ParticleSystemResource::Emitter& res_emitter = m_resource->getEmitters()[emitter_idx];
	ensureCapacity(emitter, count);

	RunningContext ctx(emitter, m_allocator);
	ctx.registers.resize(res_emitter.emit_registers_count + emit_data.length());

	const float c1 = m_system_values[(u8)ParticleSystemValues::TOTAL_TIME];
	m_system_values[(u8)ParticleSystemValues::RIBBON_INDEX] = 0;
	for (u32 i = 0; i < count; ++i) {
		m_system_values[(u8)ParticleSystemValues::EMIT_INDEX] = (float)emitter.emit_index; // TODO
		if (emit_data.length() > 0) {
			memcpy(ctx.registers.begin(), emit_data.begin(), emit_data.length() * sizeof(emit_data[0]));
		}
		ctx.particle_idx = emitter.particles_count;
		ctx.instructions.set(res_emitter.instructions.data() + res_emitter.emit_offset, res_emitter.instructions.size() - res_emitter.emit_offset);
		run(ctx);
		
		++emitter.particles_count;
		++emitter.emit_index;
		m_system_values[(u8)ParticleSystemValues::TOTAL_TIME] += time_step;
	}
	m_last_update_stats.emitted.add(count);
	m_system_values[(u8)ParticleSystemValues::TOTAL_TIME] = c1;
}


void ParticleSystem::serialize(OutputMemoryStream& blob) const
{
	blob.write(m_entity);
	blob.write(m_autodestroy);
	blob.writeString(m_resource ? m_resource->getPath().c_str() : "");
}


void ParticleSystem::deserialize(InputMemoryStream& blob, bool has_autodestroy, bool emit_rate_removed, ResourceManagerHub& manager)
{
	blob.read(m_entity);
	if (!emit_rate_removed) {
		u32 emit_rate;
		blob.read(emit_rate);
	}
	m_autodestroy = false;
	if (has_autodestroy) blob.read(m_autodestroy);
	const char* path = blob.readString();
	auto* res = manager.load<ParticleSystemResource>(Path(path));
	setResource(res);
}

static float4* getStream(const ParticleSystem::Emitter& emitter
	, DataStream stream
	, u32 offset
	, float4** register_mem)
{
	switch (stream.type) {
		case DataStream::CHANNEL: return (float4*)emitter.channels[stream.index].data + offset;
		case DataStream::REGISTER: return register_mem[stream.index];
		default: ASSERT(false); return nullptr;
	}
}

struct Stream {
	float4* data;
	u32 step;
};

struct ProcessHelper {
	ProcessHelper(const ParticleSystem::Emitter& emitter, i32 fromf4, i32 stepf4, float4** reg_mem)
		: emitter(emitter)
		, fromf4(fromf4)
		, stepf4(stepf4)
		, reg_mem(reg_mem)
	{}

	static float4 madd(float4 a, float4 b, float4 c) {
		return f4Add(f4Mul(a, b), c);
	}

	static float4 min(float4 a, float4 b) {
		return f4Min(a, b);
	}

	static float4 max(float4 a, float4 b) {
		return f4Max(a, b);
	}

	static float4 mix(float4 a, float4 b, float4 c) {
		return f4Add(a, f4Mul(f4Sub(b, a), c));
	}

	template <auto F>
	void run1(InputMemoryStream& ip) {
		const DataStream dst = ip.read<DataStream>();
		DataStream op0 = ip.read<DataStream>();
		const float* arg0 = (float*)getStream(emitter, op0, fromf4, reg_mem);
		
		if constexpr (ArgsCount<decltype(F)>::value == 1) {
			if (dst.type == DataStream::OUT) {
				i32 output_idx = dst.index;
				const u32 stride = emitter.resource_emitter.outputs_count;
				float* result = out_mem + output_idx + fromf4 * 4 * stride;
				for (i32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
					result[j] = F(arg0[i]);
				}
			}
			else {
				float* result = (float*)getStream(emitter, dst, fromf4, reg_mem);
				const float* const end = result + stepf4 * 4;

				for (; result != end; ++result, ++arg0) {
					*result = F(*arg0);
				}
			}
		}
		else {
			DataStream op1 = ip.read<DataStream>();
			const float* arg1 = (float*)getStream(emitter, op1, fromf4, reg_mem);

			if (dst.type == DataStream::OUT) {
				i32 output_idx = dst.index;
				const u32 stride = emitter.resource_emitter.outputs_count;
				float* result = out_mem + output_idx + fromf4 * 4 * stride;
				for (i32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
					result[j] = F(arg0[i], arg1[i]);
				}
			}
			else {
				float* result = (float*)getStream(emitter, dst, fromf4, reg_mem);
				const float* const end = result + stepf4 * 4;

				for (; result != end; ++result, ++arg0, ++arg1) {
					*result = F(*arg0, *arg1);
				}
			}
		}
	}

	const ParticleSystem::Emitter& emitter;
	const i32 fromf4;
	const i32 stepf4;
	float4** reg_mem;
	float* out_mem = nullptr;

	LUMIX_FORCE_INLINE void readArgs(InputMemoryStream& ip, Stream* s, float4* literals, u32 num_args) {
		for (u32 i = 0; i < num_args; ++i) {
			const DataStream stream = ip.read<DataStream>();;
			switch (stream.type) {
				case DataStream::CHANNEL: {
					s[i].data = ((float4*)emitter.channels[stream.index].data) + fromf4;
					s[i].step = 1;
					break;
				}
				case DataStream::LITERAL: {
					literals[i] = f4Splat(stream.value);
					s[i].data = &literals[i];
					s[i].step = 0;
					break;
				}
				case DataStream::SYSTEM_VALUE: {
					literals[i] = f4Splat(emitter.system.m_system_values[stream.index]);
					s[i].data = &literals[i];
					s[i].step = 0;
					break;
				}
				case DataStream::GLOBAL: {
					literals[i] = f4Splat(emitter.system.m_globals[stream.index]);
					s[i].data = &literals[i];
					s[i].step = 0;
					break;
				}
				case DataStream::REGISTER: {
					s[i].data = reg_mem[stream.index];
					s[i].step = 1;
					break;
				}
				case DataStream::OUT:
				case DataStream::NONE:
				case DataStream::ERROR: ASSERT(false); break;
			}
		}
	}

	template <auto F>
	void run2(InputMemoryStream& ip) {
		const DataStream dst = ip.read<DataStream>();
		Stream s[2];
		float4 literals[2];

		readArgs(ip, s, literals, 2);

		float4* arg0 = s[0].data;
		float4* arg1 = s[1].data;
		
		if (dst.type == DataStream::OUT) {
			const u32 stride = emitter.resource_emitter.outputs_count;
			u32 idx = dst.index + fromf4 * 4 * stride;
			for (i32 i = 0; i < stepf4; ++i, arg0 += s[0].step, arg1 += s[1].step) {
				float4 tmp = F(*arg0, *arg1);
				out_mem[idx] = f4GetX(tmp);
				idx += stride;
				out_mem[idx] = f4GetY(tmp);
				idx += stride;
				out_mem[idx] = f4GetZ(tmp);
				idx += stride;
				out_mem[idx] = f4GetW(tmp);
				idx += stride;
			}
		}
		else {
			float4* result = getStream(emitter, dst, fromf4, reg_mem);
			const float4* const end = result + stepf4;

			for (; result != end; ++result, arg0 += s[0].step, arg1 += s[1].step) {
				*result = F(*arg0, *arg1);
			}
		}
	}

	template <auto F>
	void run3(InputMemoryStream& ip) {
		const DataStream dst = ip.read<DataStream>();
		Stream s[3];
		float4 literals[3];

		readArgs(ip, s, literals, 3);

		float4* arg0 = s[0].data;
		float4* arg1 = s[1].data;
		float4* arg2 = s[2].data;

		if (dst.type == DataStream::OUT) {
			const u32 stride = emitter.resource_emitter.outputs_count;
			for (i32 i = 0; i < stepf4; ++i, arg0 += s[0].step, arg1 += s[1].step, arg2 += s[2].step) {
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
			float4* result = getStream(emitter, dst, fromf4, reg_mem);
			const float4* const end = result + stepf4;

			for (; result != end; ++result, arg0 += s[0].step, arg1 += s[1].step, arg2 += s[2].step) {
				*result = F(*arg0, *arg1, *arg2);
			}
		}
	}
};


ParticleSystem::RunResult ParticleSystem::run(RunningContext& ctx) {
	StackArray<float, 16>& registers = ctx.registers;
	float* outputs = ctx.output_memory;
	const Emitter& emitter = ctx.emitter;
	InputMemoryStream& ip = ctx.instructions;
	const u32 particle_idx = ctx.particle_idx;

	auto getConstValue = [&](const DataStream& str) -> float {
		switch (str.type) {
			case DataStream::LITERAL: return str.value;
			case DataStream::SYSTEM_VALUE: return m_system_values[str.index];
			case DataStream::OUT: return outputs[str.index];
			case DataStream::REGISTER: return registers[str.index];
			case DataStream::CHANNEL: return emitter.channels[str.index].data[particle_idx];
			case DataStream::GLOBAL: return m_globals[str.index];
			case DataStream::ERROR:
			case DataStream::NONE: break;
		}
		ASSERT(false);
		return str.value;
	};

	auto setValue = [&](const DataStream& str, float value) {
		switch (str.type) {
			case DataStream::OUT: outputs[str.index] = value; return;
			case DataStream::REGISTER: registers[str.index] = value; return;
			case DataStream::CHANNEL: emitter.channels[str.index].data[particle_idx] = value; return;
			case DataStream::SYSTEM_VALUE:
			case DataStream::GLOBAL:
			case DataStream::LITERAL:
			case DataStream::ERROR:
			case DataStream::NONE: break;
		}
		ASSERT(false); 
	};

	RunResult result = RunResult::SURVIVED;
	for (;;) {
		const InstructionType it = ip.read<InstructionType>();
		switch (it) {
			case InstructionType::END:
				return result;
			case InstructionType::KILL: result = RunResult::KILLED; break;
			case InstructionType::MESH: {
				DataStream dst = ip.read<DataStream>();
				DataStream index = ip.read<DataStream>();
				const u8 subindex = ip.read<u8>();

				RenderModule* render_module = (RenderModule*)m_world.getModule(types::model_instance);
				if (!m_world.hasComponent(*m_entity, types::model_instance)) return result; // TODO error message
				
				Model* model = render_module->getModelInstanceModel(*m_entity);
				if (!model || !model->isReady()) return result;

				u32 mesh_idx = 0; // TODO random mesh
				if (mesh_idx >= (u32)model->getMeshCount()) return result;
				
				const Mesh& mesh = model->getMesh(mesh_idx);
				if (getConstValue(index) < 0) {
					setValue(index, (float)rand(0, mesh.vertices.size() - 1));
				}

				const u32 idx = u32(getConstValue(index) + 0.5f);
				if (model->getBones().size() > 0) {
					ModelInstance* mi = render_module->getModelInstance(*m_entity);
					if (!mi->pose) return result;

					const float v = mi->model->evalVertexPose(*mi->pose, mesh_idx, idx)[subindex];
					setValue(dst, v);
				}
				else {
					const float v = mesh.vertices[idx][subindex];
					setValue(dst, v);
				}
				break;
			}
			case InstructionType::SPLINE: {
				DataStream dst = ip.read<DataStream>();
				DataStream op0 = ip.read<DataStream>();
				const u8 subindex = ip.read<u8>();

				CoreModule* core_module = (CoreModule*)m_world.getModule(types::spline);
				if (!m_world.hasComponent(*m_entity, types::spline)) return result; // TODO error message
				Spline& spline = core_module->getSpline(*m_entity);

				float t = getConstValue(op0);

				t *= spline.points.size() - 2;
				u32 segment = clamp(u32(t), 0, spline.points.size() - 3);
				float rel_t = t - segment;
				float p0 = spline.points[segment + 0][subindex];
				float p1 = spline.points[segment + 1][subindex];
				float p2 = spline.points[segment + 2][subindex];
				p0 = (p1 + p0) * 0.5f;
				p2 = (p1 + p2) * 0.5f;

				setValue(dst, lerp(lerp(p0, p1, rel_t), lerp(p1, p2, rel_t), rel_t));
				break;
			}
			case InstructionType::MUL: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();

				setValue(dst, getConstValue(op0) * getConstValue(op1));
				break;
			}
			case InstructionType::ADD: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();

				setValue(dst, getConstValue(op0) + getConstValue(op1));
				break;
			}
			case InstructionType::MULTIPLY_ADD: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();
				const DataStream op2 = ip.read<DataStream>();

				setValue(dst, getConstValue(op0) * getConstValue(op1) + getConstValue(op2));
				break;
			}
			case InstructionType::MIX: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();
				const DataStream op2 = ip.read<DataStream>();

				setValue(dst, lerp(getConstValue(op0), getConstValue(op1), getConstValue(op2)));
				break;
			}
			case InstructionType::MOD: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();

				setValue(dst, fmodf(getConstValue(op0), getConstValue(op1)));
				break;
			}
			case InstructionType::DIV: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();

				setValue(dst, getConstValue(op0) / getConstValue(op1));
				break;
			}
			case InstructionType::SUB: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();

				setValue(dst, getConstValue(op0) - getConstValue(op1));
				break;
			}
			case InstructionType::AND: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();

				const bool res = getConstValue(op0) != 0 && getConstValue(op1) != 0;
				float v;
				memset(&v, res ? 0xffFFffFF : 0, sizeof(float));
				setValue(dst, v);
				break;
			}
			case InstructionType::OR: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();

				const bool res = getConstValue(op0) != 0 || getConstValue(op1) != 0;
				float v;
				memset(&v, res ? 0xffFFffFF : 0, sizeof(float));
				setValue(dst, v);
				break;
			}
			case InstructionType::MOV: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				setValue(dst, getConstValue(op0));
				break;
			}
			case InstructionType::SIN: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				setValue(dst, sinf(getConstValue(op0)));
				break;
			}
			case InstructionType::COS: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				setValue(dst, cosf(getConstValue(op0)));
				break;
			}
			case InstructionType::SQRT: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				setValue(dst, sqrtf(getConstValue(op0)));
				break;
			}
			case InstructionType::RAND: {
				DataStream dst = ip.read<DataStream>();
				float from = ip.read<float>();
				float to = ip.read<float>();
				
				setValue(dst, randFloat(from, to));
				break;
			}
			case InstructionType::NOISE: {
				DataStream dst = ip.read<DataStream>();
				DataStream op0 = ip.read<DataStream>();

				setValue(dst, gnoise(getConstValue(op0)));
				break;
			}
			case InstructionType::MAX: {
				DataStream dst = ip.read<DataStream>();
				DataStream op0 = ip.read<DataStream>();
				DataStream op1 = ip.read<DataStream>();

				setValue(dst, maximum(getConstValue(op0), getConstValue(op1)));
				break;
			}
			case InstructionType::MIN: {
				DataStream dst = ip.read<DataStream>();
				DataStream op0 = ip.read<DataStream>();
				DataStream op1 = ip.read<DataStream>();

				setValue(dst, minimum(getConstValue(op0), getConstValue(op1)));
				break;
			}
			case InstructionType::LT: {
				DataStream dst = ip.read<DataStream>();
				DataStream op0 = ip.read<DataStream>();
				DataStream op1 = ip.read<DataStream>();

				setValue(dst, getConstValue(op0) < getConstValue(op1));
				break;
			}
			case InstructionType::GT: {
				DataStream dst = ip.read<DataStream>();
				DataStream op0 = ip.read<DataStream>();
				DataStream op1 = ip.read<DataStream>();

				setValue(dst, getConstValue(op0) > getConstValue(op1));
				break;
			}
			case InstructionType::EMIT: {
				u32 emitter_idx = ip.read<u32>();
				bool is_ribbon = emitter.resource_emitter.max_ribbons > 0;

				StackArray<float, 16> emit_outputs(m_allocator);
				emit_outputs.resize(m_resource->getEmitters()[emitter_idx].emit_inputs_count);
				float* output_tmp = ctx.output_memory;
				ctx.output_memory = emit_outputs.begin();
				run(ctx);
				ctx.output_memory = output_tmp;

				if (ctx.emit_mutex) { // if we are just skipping, emit_mutex is null
					jobs::enter(ctx.emit_mutex);
					if (is_ribbon) {
						ctx.emit_stream->write(emitter_idx);
						ctx.emit_stream->write(emit_outputs.size());
						ctx.emit_stream->write(emit_outputs.begin(), emit_outputs.byte_size());
						ctx.emit_stream->write(ctx.ribbon_index);
					}
					else {
						ctx.emit_stream->write(emitter_idx);
						ctx.emit_stream->write(emit_outputs.size());
						ctx.emit_stream->write(emit_outputs.begin(), emit_outputs.byte_size());
					}
					jobs::exit(ctx.emit_mutex);
				}
				break;
			}
			case InstructionType::GRADIENT:
			case InstructionType::BLEND:
			case InstructionType::CMP: // TODO nested ifs
			case InstructionType::CMP_ELSE:
				ASSERT(false);
				break;
		}
	}
}

struct ParticleSystem::ChunkProcessorContext {
	ChunkProcessorContext(const Emitter& emitter, bool is_update, PageAllocator& page_allocator)
		: emitter(emitter)
		, page_allocator(page_allocator)
		, is_update(is_update)
	{
		num_registers = is_update ? emitter.resource_emitter.update_registers_count : emitter.resource_emitter.output_registers_count;
		ASSERT(num_registers <= lengthOf(registers));
		for (u32 i = 0; i < num_registers; ++i) {
			registers[i] = (float4*)page_allocator.allocate();
		}
	}

	~ChunkProcessorContext() {
		num_registers = is_update ? emitter.resource_emitter.update_registers_count : emitter.resource_emitter.output_registers_count;
		for (u32 i = 0; i < num_registers; ++i) {
			page_allocator.deallocate(registers[i]);
		}
	}

	const Emitter& emitter;
	PageAllocator& page_allocator;
	bool is_update;
	i32 from;
	i32 to;
	float4* registers[16] = {};
	u32 num_registers = 0;

	u32 instructions_offset = 0;
	u32* kill_counter = nullptr;
	jobs::Mutex* emit_mutex = nullptr;
	OutputPagedStream* emit_stream = nullptr;
	float time_delta = 0;
	float* output_memory = nullptr;
	u32 ribbon_index = 0;
};

void ParticleSystem::skipBlock(ParticleSystem::RunningContext& ctx, const InputMemoryStream& ip, InputMemoryStream& head, InputMemoryStream& tail) {
	ctx.instructions.set((const u8*)ip.getData() + ip.getPosition(), ip.remaining());
	ctx.particle_idx = 0;
	jobs::Mutex* mtx = ctx.emit_mutex;
	ctx.emit_mutex = nullptr;
	run(ctx);
	ctx.emit_mutex = mtx;

	u32 remaining = u32(ctx.instructions.remaining());
	head.set(ctx.instructions.getData(), ctx.instructions.size() - remaining);
	tail.set((const u8*)head.getData() + head.size(), remaining);
}

void ParticleSystem::processChunk(ChunkProcessorContext& ctx) {
	const Emitter& emitter = ctx.emitter;
	const i32 from = ctx.from;

	ParticleSystemResource::Emitter& res_emitter = emitter.resource_emitter;
	const i32 fromf4 = from / 4;
	const i32 stepf4 = ((ctx.to - from) + 3) / 4;
	InputMemoryStream ip = InputMemoryStream(res_emitter.instructions);
	ip.skip(ctx.instructions_offset);
	InstructionType itype = ip.read<InstructionType>();
	const u32 num_registers = ctx.num_registers;

	ProcessHelper op_helper(emitter, fromf4, stepf4, ctx.registers);
	op_helper.out_mem = ctx.output_memory;
	const u32 num_channels = res_emitter.channels_count;

	while (itype != InstructionType::END) {
		switch (itype) {
			case InstructionType::CMP_ELSE: {
				DataStream condition_stream = ip.read<DataStream>();
				const float4* cond = getStream(emitter, condition_stream, fromf4, ctx.registers);
				const float4* const end = cond + stepf4;
				StackArray<float, 16> tmp_outputs(m_allocator);
				tmp_outputs.resize(emitter.resource_emitter.outputs_count);
				RunningContext single_ctx(emitter, m_allocator);
				single_ctx.instructions = ip;
				single_ctx.output_memory = tmp_outputs.begin();
				single_ctx.registers.resize(num_registers);
				single_ctx.emit_mutex = ctx.emit_mutex;
				single_ctx.emit_stream = ctx.emit_stream;
				
				// TODO serialize block length so we can avoid this
				InputMemoryStream true_block_ip(nullptr, 0), false_block_ip(nullptr, 0);
				skipBlock(single_ctx, ip, true_block_ip, false_block_ip);
				skipBlock(single_ctx, false_block_ip, false_block_ip, ip);
				u32 kill_count = 0;
				u32 last = ctx.to - 1;

				for (const float4* beg = cond; cond != end; ++cond) {
					const int m = f4MoveMask(*cond);
					
					i32 particle_index = from + i32(cond - beg) * 4;
					for (i32 i = 0; i < 4; ++i) {
						single_ctx.particle_idx = particle_index;
						const bool is_true = (m & (1 << i)) && particle_index < ctx.to;
						single_ctx.instructions = is_true ? true_block_ip : false_block_ip;
						single_ctx.output_memory = ctx.output_memory + particle_index * emitter.resource_emitter.outputs_count;
						if (run(single_ctx) == RunResult::KILLED) {
							for (u32 ch = 0; ch < num_channels; ++ch) {
								float* data = emitter.channels[ch].data;
								data[particle_index] = data[last];
							}
							--last;
							++kill_count;
						}
						++particle_index;
					}
					if (kill_count > 0) {
						const u32 chunk_idx = from / 1024;
						ctx.kill_counter[chunk_idx] = kill_count;
					}
				}
				break;
			}
			case InstructionType::CMP: {
				DataStream condition_stream = ip.read<DataStream>();
				const float4* cond = getStream(emitter, condition_stream, fromf4, ctx.registers);
				const float4* const end = cond + stepf4;
				StackArray<float, 16> tmp_outputs(m_allocator);
				tmp_outputs.resize(emitter.resource_emitter.outputs_count);
				RunningContext single_ctx(emitter, m_allocator);
				single_ctx.instructions = ip;
				single_ctx.output_memory = tmp_outputs.begin();
				single_ctx.registers.resize(num_registers);
				single_ctx.emit_mutex = ctx.emit_mutex;
				single_ctx.emit_stream = ctx.emit_stream;
				
				InputMemoryStream true_block_ip(nullptr, 0);
				skipBlock(single_ctx, ip, true_block_ip, ip);
				u32 kill_count = 0;
				u32 last = ctx.to - 1;

				for (const float4* beg = cond; cond != end; ++cond) {
					const int m = f4MoveMask(*cond);
					if (!m) continue;
					
					i32 particle_index = from + i32(cond - beg) * 4;
					for (i32 i = 0; i < 4; ++i) {
						if ((m & (1 << i)) && particle_index < ctx.to) {
							single_ctx.instructions = true_block_ip;
							single_ctx.particle_idx = particle_index;
							single_ctx.output_memory = ctx.output_memory + particle_index * emitter.resource_emitter.outputs_count;
							if (run(single_ctx) == RunResult::KILLED) {
								for (u32 ch = 0; ch < num_channels; ++ch) {
									float* data = emitter.channels[ch].data;
									data[particle_index] = data[last];
								}
								--last;
								++kill_count;
							}
						}
						++particle_index;
					}
					if (kill_count > 0) {
						const u32 chunk_idx = from / 1024;
						ctx.kill_counter[chunk_idx] = kill_count;
					}
				}
				break;
			}
			case InstructionType::KILL: {
				ASSERT(false); // kill outside if condition would alway kill all particles
				// TODO catch this in the compiler
				break;
			}
			case InstructionType::EMIT: {
				const u32 emitter_idx = ip.read<u32>();
				RunningContext emit_ctx(emitter, m_allocator);
				bool is_ribbon = emitter.resource_emitter.max_ribbons > 0;
				StackArray<float, 16> outputs(m_allocator);
				outputs.resize(m_resource->getEmitters()[emitter_idx].emit_inputs_count);

				if (is_ribbon) {
					for (i32 i = 0; i < stepf4 * 4; ++i) {					
						const i32 particle_index = from + i;
						emit_ctx.instructions.set((const u8*)ip.getData() + ip.getPosition(), ip.remaining());
						emit_ctx.particle_idx = particle_index;
						emit_ctx.registers.resize(res_emitter.update_registers_count);
						emit_ctx.output_memory = outputs.begin();
						run(emit_ctx);
							
						jobs::enter(ctx.emit_mutex);
						ctx.emit_stream->write(emitter_idx);
						ctx.emit_stream->write(outputs.size());
						ctx.emit_stream->write(outputs.begin(), outputs.byte_size());
						ctx.emit_stream->write(ctx.ribbon_index);
						jobs::exit(ctx.emit_mutex);
					}
				}
				else {
					for (i32 i = 0; i < stepf4 * 4; ++i) {					
						const i32 particle_index = from + i;
						emit_ctx.instructions.set((const u8*)ip.getData() + ip.getPosition(), ip.remaining());
						emit_ctx.particle_idx = particle_index;
						emit_ctx.registers.resize(res_emitter.update_registers_count);
						emit_ctx.output_memory = outputs.begin();
						run(emit_ctx);
							
						jobs::enter(ctx.emit_mutex);
						ctx.emit_stream->write(emitter_idx);
						ctx.emit_stream->write(outputs.size());
						ctx.emit_stream->write(outputs.begin(), outputs.byte_size());
						jobs::exit(ctx.emit_mutex);
					}
				}

				// skip emit subroutine
				RunningContext ctx(emitter, m_allocator);
				ctx.instructions.set((const u8*)ip.getData() + ip.getPosition(), ip.remaining());
				ctx.particle_idx = 0;
				ctx.registers.resize(res_emitter.update_registers_count);
				ctx.output_memory = outputs.begin();
				run(ctx);
				ip.setPosition(ip.size() - ctx.instructions.remaining());
				break;
			}
			case InstructionType::SPLINE: {
				DataStream dst = ip.read<DataStream>();
				DataStream op0 = ip.read<DataStream>();
				const u8 subindex = ip.read<u8>();
				ASSERT(dst.type == DataStream::OUT);

				CoreModule* core_module = (CoreModule*)m_world.getModule(types::spline);
				if (!m_world.hasComponent(*m_entity, types::spline)) {
					return; // TODO error message
				}
				Spline& spline = core_module->getSpline(*m_entity);

				const u8 output_idx = dst.index;
				const u32 stride = res_emitter.outputs_count;
				const float* arg = (float*)getStream(emitter, op0, fromf4, ctx.registers);
				float* out = ctx.output_memory + output_idx + fromf4 * 4 * stride;
				const i32 last_idx = spline.points.size() - 2;
				for (i32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
					const float t = arg[i] * last_idx;
					u32 segment = clamp(u32(t), 0, spline.points.size() - 3);
					float rel_t = t - segment;
					float p0 = spline.points[segment + 0][subindex];
					float p1 = spline.points[segment + 1][subindex];
					float p2 = spline.points[segment + 2][subindex];
					p0 = (p1 + p0) * 0.5f;
					p2 = (p1 + p2) * 0.5f;

					out[j] = lerp(lerp(p0, p1, rel_t), lerp(p1, p2, rel_t), rel_t);
				}
				break;
			}
			case InstructionType::GRADIENT: {
				DataStream dst = ip.read<DataStream>();
				DataStream op0 = ip.read<DataStream>();
				u32 count = ip.read<u32>();
				float keys[8];
				float values[8];
				ASSERT(count <= lengthOf(keys));
				ip.read(keys, sizeof(keys[0]) * count);
				ip.read(values, sizeof(values[0]) * count);

				float ms[8];
				for (u32 i = 1; i < count; ++i) {
					ms[i] = (values[i] - values[i - 1]) / (keys[i] - keys[i - 1]);
				}

				if (dst.type == DataStream::OUT) {
					const u8 output_idx = dst.index;
					const u32 stride = emitter.resource_emitter.outputs_count;
					const float* arg = (float*)getStream(emitter, op0, fromf4, ctx.registers);
					float* out = ctx.output_memory + output_idx + fromf4 * 4 * stride;
					for (i32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
						const float v = clamp(arg[i], keys[0], keys[count - 1]);
						u32 k = 1;
						while (v > keys[k]) ++k;
						out[j] = values[k] - (keys[k] - v) * ms[k];
					}
				}
				else if (dst.type == DataStream::REGISTER) {
					const u8 output_idx = dst.index;
					const float* arg = (float*)getStream(emitter, op0, fromf4, ctx.registers);
					float* result = (float*)getStream(emitter, dst, fromf4, ctx.registers);
					for (i32 i = 0; i < stepf4 * 4; ++i) {
						const float v = clamp(arg[i], keys[0], keys[count - 1]);
						u32 k = 1;
						while (v > keys[k]) ++k;
						result[i] = values[k] - (keys[k] - v) * ms[k];
					}
				}
				break;
			}
			case InstructionType::BLEND: op_helper.run3<f4Blend>(ip); break;
			case InstructionType::LT: op_helper.run2<f4CmpLT>(ip); break;
			case InstructionType::GT: op_helper.run2<f4CmpGT>(ip); break;
			case InstructionType::MUL: op_helper.run2<f4Mul>(ip); break; 
			case InstructionType::DIV: op_helper.run2<f4Div>(ip); break;
			case InstructionType::SUB: op_helper.run2<f4Sub>(ip); break;
			case InstructionType::AND: op_helper.run2<f4And>(ip); break;
			case InstructionType::OR: op_helper.run2<f4Or>(ip); break;
			case InstructionType::ADD: op_helper.run2<f4Add>(ip); break; 
			case InstructionType::MIX: op_helper.run3<ProcessHelper::mix>(ip); break; 
			case InstructionType::MULTIPLY_ADD: op_helper.run3<ProcessHelper::madd>(ip); break; 
			case InstructionType::MOD: op_helper.run1<fmodf>(ip); break; 
			case InstructionType::SQRT: op_helper.run1<sqrtf>(ip); break;
			case InstructionType::COS: op_helper.run1<cosf>(ip); break;
			case InstructionType::MAX: op_helper.run2<ProcessHelper::max>(ip); break;
			case InstructionType::MIN: op_helper.run2<ProcessHelper::min>(ip); break;
			case InstructionType::NOISE: op_helper.run1<gnoise>(ip); break;
			case InstructionType::SIN: op_helper.run1<sinf>(ip); break;
			case InstructionType::MOV: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				if (dst.type == DataStream::OUT) {
					const u32 stride = emitter.resource_emitter.outputs_count;
					if (op0.type == DataStream::GLOBAL) {
						const float arg = m_globals[op0.index];
						u8 output_idx = dst.index;
						float* res = ctx.output_memory + output_idx + fromf4 * 4 * stride;
						for (i32 i = 0; i < stepf4 * 4; ++i) {
							res[i * stride] = arg;
						}
					}
					else if (op0.type == DataStream::LITERAL) {
						const float arg = op0.value;
						u8 output_idx = dst.index;
						float* res = ctx.output_memory + output_idx + fromf4 * 4 * stride;
						for (i32 i = 0; i < stepf4 * 4; ++i) {
							res[i * stride] = arg;
						}
					}
					else {
						const float* arg = (float*)getStream(emitter, op0, fromf4, ctx.registers);
						u8 output_idx = dst.index;
						float* res = ctx.output_memory + output_idx + fromf4 * 4 * stride;
						for (i32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
							res[j] = arg[i];
						}
					}
				}
				else {
					float4* result = getStream(emitter, dst, fromf4, ctx.registers);
					const float4* const end = result + stepf4;
				
					if (op0.type == DataStream::LITERAL) {
						const float4 src = f4Splat(op0.value);
						for (; result != end; ++result) {
							*result = src;
						}
					}
					else if (op0.type == DataStream::SYSTEM_VALUE) {
						const float4 src = f4Splat(m_system_values[op0.index]);
						for (; result != end; ++result) {
							*result = src;
						}
					}
					else {
						const float4* src = getStream(emitter, op0, fromf4, ctx.registers);

						for (; result != end; ++result, ++src) {
							*result = *src;
						}
					}
				}
				break;
			}
			case InstructionType::END:
			case InstructionType::RAND:
			case InstructionType::MESH:
				ASSERT(false);
				break;
		}
		itype = ip.read<InstructionType>();
	}
}

void ParticleSystem::applyTransform(const Transform& new_tr) {
	PROFILE_FUNCTION();
	if (m_total_time == 0) {
		m_prev_frame_transform = new_tr;
	}
	const Transform delta_tr = Transform::computeLocal(new_tr, m_prev_frame_transform);
	for (i32 emitter_idx = 0; emitter_idx < m_emitters.size(); ++emitter_idx) {
		Emitter& emitter = m_emitters[emitter_idx];
		if (emitter.resource_emitter.emit_on_move) {
			const bool moved = squaredLength(new_tr.pos - emitter.last_emit_point) > 0.0025f;
			if (moved) {
				emitter.last_emit_point = new_tr.pos;
				m_system_values[(u8)ParticleSystemValues::TOTAL_TIME] = m_total_time;
				if (emitter.resource_emitter.max_ribbons > 0) {
					for (u32 i = 0, c =emitter.ribbons.size(); i < c; ++i) {
						emitRibbonPoints(emitter_idx, i, {}, 1, 0);
					}
				}
				else {
					emit(emitter_idx, {}, 1, 0);
				}
			}
		}
		if ((u32)m_resource->getFlags() & (u32)ParticleSystemResource::Flags::WORLD_SPACE) {
			jobs::forEach(emitter.particles_count, 4096, [&](u32 from, u32 to){
				PROFILE_BLOCK("to world space");
				// TODO make sure first 3 channels are position
				float* LUMIX_RESTRICT x = emitter.channels[0].data;
				float* LUMIX_RESTRICT y = emitter.channels[1].data;
				float* LUMIX_RESTRICT z = emitter.channels[2].data;
				for (u32 i = from; i < to; ++i) {
					Vec3 p{x[i], y[i], z[i]};
					p = Vec3(delta_tr.transform(p));
					x[i] = p.x;
					y[i] = p.y;
					z[i] = p.z;
				}
			});
		}
	}
	m_prev_frame_transform = new_tr;
}

void ParticleSystem::updateRibbons(float dt, u32 emitter_idx, PageAllocator& page_allocator) {
	Emitter& emitter = m_emitters[emitter_idx];
	ParticleSystemResource::Emitter& res_emitter = emitter.resource_emitter;

	if (res_emitter.emit_per_second > 0) {
		emitter.emit_timer += dt;
		if (emitter.emit_timer > 0) {
			PROFILE_BLOCK("emit");
			const float d = 1.f / res_emitter.emit_per_second;
			m_system_values[(u8)ParticleSystemValues::TOTAL_TIME] = m_total_time;
			const u32 count = u32(floorf(emitter.emit_timer / d));
			for(i32 ribbon_index = 0; ribbon_index < emitter.ribbons.size(); ++ribbon_index) {
				emitRibbonPoints(emitter_idx, ribbon_index, {}, count, d);
			}
			emitter.emit_timer -= d * count;
		}
	}

	OutputPagedStream emit_stream(page_allocator);
	jobs::Mutex emit_mutex;
	ChunkProcessorContext ctx(emitter, true, page_allocator);
	ctx.kill_counter = nullptr;
	ctx.emit_mutex = &emit_mutex;
	ctx.emit_stream = &emit_stream;
	ctx.time_delta = dt;

	for (u32 ribbon_idx = 0, c = emitter.ribbons.size(); ribbon_idx < c; ++ribbon_idx) {
		u32 ribbon_length = emitter.ribbons[ribbon_idx].length;
		for (u32 i = 0; i < ribbon_length; i+= 1024) {
			ctx.from = ribbon_idx * res_emitter.max_ribbon_length + i;
			ctx.to = ctx.from + ribbon_length;
			ctx.ribbon_index = ribbon_idx;
			processChunk(ctx);
		}
	}

	InputPagedStream blob(emit_stream);
	while (!blob.isEnd()) {
		u32 emitter_idx = blob.read<u32>();
		u32 outputs_count = blob.read<u32>();
		u32 ribbon_index = blob.read<u32>();
		float outputs[64];
		ASSERT(outputs_count < lengthOf(outputs));
		blob.read(outputs, outputs_count * sizeof(float));
		
		Emitter& dst_emitter = m_emitters[emitter_idx];
		
		emitRibbonPoints(emitter_idx, ribbon_index, Span(outputs, outputs_count), 1, 0);
	}
}

void ParticleSystem::update(float dt, u32 emitter_idx, PageAllocator& page_allocator) {
	PROFILE_FUNCTION();

	Emitter& emitter = m_emitters[emitter_idx];
	if (emitter.resource_emitter.max_ribbons > 0) {
		updateRibbons(dt, emitter_idx, page_allocator);
		return;
	}

	ParticleSystemResource::Emitter& res_emitter = m_resource->getEmitters()[emitter_idx];

	if (res_emitter.emit_per_second > 0) {
		emitter.emit_timer += dt;
		if (emitter.emit_timer > 0) {
			PROFILE_BLOCK("emit");
			const float d = 1.f / res_emitter.emit_per_second;
			m_system_values[(u8)ParticleSystemValues::TOTAL_TIME] = m_total_time;
			const u32 count = u32(floorf(emitter.emit_timer / d));
			emit(emitter_idx, {}, count, d);
			emitter.emit_timer -= d * count;
			profiler::pushInt("count", count);
		}
	}

	if (emitter.particles_count == 0) return;

	m_system_values[(u8)ParticleSystemValues::TOTAL_TIME] = m_total_time;

	profiler::pushInt("particle count", emitter.particles_count);
	u32* kill_counter = (u32*)page_allocator.allocate();
	const u32 chunks_count = (emitter.particles_count + 1023) / 1024;
	ASSERT(chunks_count <= PageAllocator::PAGE_SIZE / sizeof(u32));
	memset(kill_counter, 0, chunks_count * sizeof(u32));
	OutputPagedStream emit_stream(page_allocator);
	jobs::Mutex emit_mutex;

	AtomicI32 counter = 0;
	auto update = [&](){
		PROFILE_BLOCK("update particles");
		
		ChunkProcessorContext ctx(emitter, true, page_allocator);
		ctx.kill_counter = kill_counter;
		ctx.emit_mutex = &emit_mutex;
		ctx.emit_stream = &emit_stream;
		ctx.time_delta = dt;

		u32 processed = 0;
		for (;;) {
			ctx.from = counter.add(1024);
			if (ctx.from >= (i32)emitter.particles_count) return;

			ctx.to = minimum(ctx.from + 1024, emitter.particles_count);
			processChunk(ctx);
			processed += ctx.to - ctx.from;
		}
		profiler::pushInt("Total count", processed);
	};
	
	m_last_update_stats.processed.add(emitter.particles_count);
	if (emitter.particles_count <= 4096) update();
	else jobs::runOnWorkers(update);

	{
		PROFILE_BLOCK("compact");
		u32 head = 0;
		u32 tail = chunks_count - 1;
		const u32 channels_count = res_emitter.channels_count;
		u32 total_killed = 0;
		for (u32 i = 0; i < chunks_count; ++i) total_killed += kill_counter[i];
		while (head != tail) {
			if (kill_counter[head] == 0) {
				++head;
				continue;
			}

			const u32 tail_start = 1024 * tail;
			const u32 tail_count = minimum(1024, emitter.particles_count - tail_start) - kill_counter[tail];

			if (tail_count <= kill_counter[head]) {
				for (u32 i = 0; i < channels_count; ++i) {
					float* data = emitter.channels[i].data;
					memcpy(data + head * 1024 + 1024 - kill_counter[head], data + tail_start, tail_count * sizeof(float));
				}
				--tail;
				kill_counter[head] -= tail_count;
			}
			else {
				for (u32 i = 0; i < channels_count; ++i) {
					float* data = emitter.channels[i].data;
					memcpy(data + head * 1024 + 1024 - kill_counter[head], data + tail_start + tail_count - kill_counter[head], kill_counter[head] * sizeof(float));
				}
				kill_counter[tail] += kill_counter[head];
				++head;
			}
		}
		
		m_last_update_stats.killed.add(total_killed);
		emitter.particles_count -= total_killed;
		profiler::pushInt("kill count", total_killed);
		page_allocator.deallocate(kill_counter);
	}
	
	InputPagedStream blob(emit_stream);
	while (!blob.isEnd()) {
		u32 emitter_idx = blob.read<u32>();
		u32 outputs_count = blob.read<u32>();
		float outputs[64];
		ASSERT(outputs_count < lengthOf(outputs));
		blob.read(outputs, outputs_count * sizeof(float));
		
		Emitter& dst_emitter = m_emitters[emitter_idx];
		
		PROFILE_BLOCK("emit from graph");
		profiler::pushInt("count", dst_emitter.resource_emitter.init_emit_count);
		emit(emitter_idx, Span(outputs, outputs_count), dst_emitter.resource_emitter.init_emit_count, 0);
	}
}

void ParticleSystem::initRibbonEmitter(i32 emitter_idx) {
	Emitter& emitter = m_emitters[emitter_idx];
	
	const u32 num_channels = emitter.resource_emitter.channels_count;
	const u32 num_floats_in_channel = emitter.resource_emitter.max_ribbons * emitter.resource_emitter.max_ribbon_length;
	for (u32 i = 0; i < num_channels; ++i) {
		emitter.channels[i].data = (float*)m_allocator.allocate(num_floats_in_channel * sizeof(float), alignof(float4));
	}

	emitter.ribbons.reserve(emitter.resource_emitter.max_ribbons);
	emitter.ribbons.resize(emitter.resource_emitter.init_ribbons_count);
	for (u32 i = 0; i < emitter.resource_emitter.init_ribbons_count; ++i) {
		emitter.ribbons[i].length = 0;
		emitter.ribbons[i].offset = 0;
		emitter.ribbons[i].emit_index = 0;
		emitRibbonPoints(emitter_idx, i, {}, emitter.resource_emitter.init_emit_count, 0);
	}
}

bool ParticleSystem::update(float dt, PageAllocator& page_allocator)
{
	PROFILE_FUNCTION();
	m_last_update_stats = {};
	if (!m_resource || !m_resource->isReady()) return false;
	
	m_system_values[(u8)ParticleSystemValues::TIME_DELTA] = dt;
	m_system_values[(u8)ParticleSystemValues::TOTAL_TIME] = m_total_time;
	
	if (m_total_time == 0) {
		for (i32 emitter_idx = 0; emitter_idx < m_emitters.size(); ++emitter_idx) {
			const ParticleSystemResource::Emitter& emitter = m_resource->getEmitters()[emitter_idx];
			if (emitter.max_ribbons > 0) {
				initRibbonEmitter(emitter_idx);
			}
			else if (emitter.emit_inputs_count == 0) {
				emit(emitter_idx, {}, emitter.init_emit_count, 0);
			}
		}
	}

	m_total_time += dt;

	for (i32 emitter_idx = 0; emitter_idx < m_emitters.size(); ++emitter_idx) {
		update(dt, emitter_idx, page_allocator);
	}

	u32 c = 0;
	for (const Emitter& emitter : m_emitters) {
		c += emitter.particles_count;
	}
	return c == 0 && m_autodestroy;
}


u32 ParticleSystem::Emitter::getParticlesDataSizeBytes() const {
	return ((particles_count + 3) & ~3) * resource_emitter.outputs_count * sizeof(float);
}


void ParticleSystem::Emitter::fillInstanceData(float* data, PageAllocator& page_allocator) const {
	if (particles_count == 0) return;

	AtomicI32 counter = 0;
	auto fill = [&](){
		PROFILE_BLOCK("fill particle gpu data");

		ChunkProcessorContext ctx(*this, false, page_allocator);
		ctx.instructions_offset = resource_emitter.output_offset;
		ctx.output_memory = data;

		for (;;) {
			ctx.from = counter.add(1024);
			if (ctx.from >= (i32)particles_count) return;
			
			ctx.to = minimum(ctx.from + 1024, particles_count);
			system.processChunk(ctx);
		}
	};

	if (particles_count <= 4096) fill();
	else jobs::runOnWorkers(fill);
}


} // namespace Lumix