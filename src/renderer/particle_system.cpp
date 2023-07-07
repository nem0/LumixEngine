#include "particle_system.h"
#include "engine/crt.h"
#include "engine/atomic.h"
#include "engine/core.h"
#include "engine/debug.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/page_allocator.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/simd.h"
#include "engine/stack_array.h"
#include "engine/stream.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/render_module.h"
#include "engine/world.h"


namespace Lumix
{

using DataStream = ParticleSystemResource::DataStream;
using InstructionType = ParticleSystemResource::InstructionType;

const ResourceType ParticleSystemResource::TYPE = ResourceType("particle_emitter");
static const ComponentType SPLINE_TYPE = reflection::getComponentType("spline");
static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");

ParticleSystemResource::Emitter::Emitter(ParticleSystemResource& resource)
	: resource(resource)
	, instructions(resource.m_allocator)
	, material(nullptr)
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
{
}


void ParticleSystemResource::unload()
{
	for (Emitter& emitter : m_emitters) {
		if (emitter.material) {
			Material* tmp = emitter.material;
			emitter.material = nullptr;
			removeDependency(*tmp);
			tmp->decRefCount();
		}
		emitter.instructions.clear();
	}
	m_emitters.clear();
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
	emitter.registers_count = registers_count;
	emitter.outputs_count = outputs_count;
	emitter.init_emit_count = init_emit_count;
	emitter.emit_per_second = emit_rate;
	emitter.emit_inputs_count = emit_inputs_count;
	emitter.setMaterial(material);
	
	--m_empty_dep_count;
	checkState();
}

void ParticleSystemResource::Emitter::setMaterial(const Path& path)
{
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
	}
}

bool ParticleSystemResource::load(u64 size, const u8* mem) {
	Header header;
	InputMemoryStream blob(mem, size);
	blob.read(header);
	if (header.magic != Header::MAGIC) {
		logError("Invalid file ", getPath());
		return false;
	}

	if (header.version > Version::LAST) {
		logError("Unsupported version ", getPath());
		return false;
	}

	if (header.version > Version::FLAGS) blob.read(m_flags);

	u32 emitter_count = 1;
	if (header.version > Version::MULTIEMITTER) {
		emitter_count = blob.read<u32>();
	}

	for (u32 i = 0; i < emitter_count; ++i) {
		Emitter& emitter = m_emitters.emplace(*this);
		if (header.version > Version::VERTEX_DECL) {
			blob.read(emitter.vertex_decl);
		}
		else {
			emitter.vertex_decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);	// pos
			emitter.vertex_decl.addAttribute(1, 12, 1, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);	// scale
			emitter.vertex_decl.addAttribute(2, 16, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);	// color
			emitter.vertex_decl.addAttribute(3, 32, 1, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);	// rot
			emitter.vertex_decl.addAttribute(4, 36, 1, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED);	// frame
		}

		emitter.setMaterial(Path(blob.readString()));
		const u32 isize = blob.read<u32>();
		emitter.instructions.resize(isize);
		blob.read(emitter.instructions.getMutableData(), emitter.instructions.size());
		blob.read(emitter.emit_offset);
		blob.read(emitter.output_offset);
		blob.read(emitter.channels_count);
		blob.read(emitter.registers_count);
		blob.read(emitter.outputs_count);
		if (header.version > Version::EMIT_RATE) {
			blob.read(emitter.init_emit_count);
			blob.read(emitter.emit_per_second);
		}
		if (header.version > Version::EMIT) {
			blob.read(emitter.emit_inputs_count);
		}
	}
	return true;
}

ParticleSystem::ParticleSystem(EntityPtr entity, World& world, IAllocator& allocator)
	: m_allocator(allocator)
	, m_world(world)
	, m_entity(entity)
	, m_emitters(allocator)
{
}

ParticleSystem::Emitter::Emitter(ParticleSystem::Emitter&& rhs)
	: system(rhs.system)
	, resource_emitter(rhs.resource_emitter)
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
{
	memcpy(m_constants, rhs.m_constants, sizeof(m_constants));
	
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
			m_allocator.deallocate_aligned(c.data);
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
				m_allocator.deallocate_aligned(c.data);
			}
		}
		m_emitters.clear();
		for (u32 i = 0, c = m_resource->getEmitters().size(); i < c; ++i) {
			m_emitters.emplace(*this, m_resource->getEmitters()[i]);
		}
	}
	for (Emitter& emitter : m_emitters) {
		emitter.emit_timer = 0;
		emitter.particles_count = 0;
		emitter.capacity = 0;
		for (Channel& c : emitter.channels) {
			m_allocator.deallocate_aligned(c.data);
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
	RunningContext(Emitter& emitter, IAllocator& allocator)
		: registers(allocator)
		, outputs(allocator)
		, emitter(emitter)
		, instructions(nullptr, 0)
	{}
	StackArray<float, 16> registers;
	StackArray<float, 16> outputs;
	InputMemoryStream instructions;
	Emitter& emitter;
	u32 particle_idx;
};

void ParticleSystem::emit(u32 emitter_idx, Span<const float> emit_data) {
	Emitter& emitter = m_emitters[emitter_idx];
	m_constants[2] = (float)emitter.emit_index; // TODO
	++emitter.emit_index;
	const ParticleSystemResource::Emitter& res_emitter = m_resource->getEmitters()[emitter_idx];

	if (emitter.particles_count == emitter.capacity) {
		const u32 channels_count = res_emitter.channels_count;
		u32 new_capacity = maximum(16, emitter.capacity << 1);
		for (u32 i = 0; i < channels_count; ++i)
		{
			emitter.channels[i].data = (float*)m_allocator.reallocate_aligned(emitter.channels[i].data, new_capacity * sizeof(float), 16);
		}
		emitter.capacity = new_capacity;
	}

	RunningContext ctx(emitter, m_allocator);
	ctx.registers.resize(res_emitter.registers_count + emit_data.length());
	if (emit_data.length() > 0) {
		memcpy(ctx.registers.begin(), emit_data.begin(), emit_data.length() * sizeof(emit_data[0]));
	}
	ctx.particle_idx = emitter.particles_count;
	ctx.instructions.set(res_emitter.instructions.data() + res_emitter.emit_offset, res_emitter.instructions.size() - res_emitter.emit_offset);
	run(ctx);
	++emitter.particles_count;
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
	, float4* register_mem)
{
	switch (stream.type) {
		case DataStream::CHANNEL: return (float4*)emitter.channels[stream.index].data + offset;
		case DataStream::REGISTER: return register_mem + 256 * stream.index;
		default: ASSERT(false); return nullptr;
	}
}

struct LiteralGetter {
	LiteralGetter(DataStream stream) : stream(stream) {}

	float4* get(const ParticleSystem::Emitter& emitter, i32, i32, float4*) {
		value = f4Splat(stream.value);
		return &value;
	}

	static void step(float4*&) {}
	float4 value;
	DataStream stream;
};

struct ChannelGetter {
	ChannelGetter(DataStream stream) : stream(stream) {}
	float4* get(const ParticleSystem::Emitter& emitter, i32, i32, float4*) {
		return (float4*)emitter.channels[stream.index].data;
	}
	static void step(float4*& val) { ++val; }
	DataStream stream;
};

struct ConstGetter {
	ConstGetter(DataStream stream) : stream(stream) {}

	float4* get(const ParticleSystem::Emitter& emitter, i32, i32, float4*) {
		value = f4Splat(emitter.system.m_constants[stream.index]);
		return &value;
	}
	static void step(float4*& val) {}

	float4 value;
	DataStream stream;
};

struct RegisterGetter {
	RegisterGetter(DataStream stream) : stream(stream) {}

	float4* get(const ParticleSystem::Emitter& emitter, i32, i32 stepf4, float4* reg_mem) {
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
			const u32 stride = emitter->resource_emitter.outputs_count;
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
	
	LUMIX_FORCE_INLINE static float4 f4Mod(float4 a, float4 b) {
		float af[4];
		float bf[4];
		float r[4];
		f4Store(af, a);
		f4Store(bf, b);
		r[0] = fmodf(af[0], bf[0]);
		r[1] = fmodf(af[1], bf[1]);
		r[2] = fmodf(af[2], bf[2]);
		r[3] = fmodf(af[3], bf[3]);
		return f4Load(r);
	}

	const ParticleSystem::Emitter* emitter;
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
			const u32 stride = emitter->resource_emitter.outputs_count;
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

	const ParticleSystem::Emitter* emitter;
	i32 fromf4;
	i32 stepf4;
	float4* reg_mem;
	float* out_mem = nullptr;
};

void ParticleSystem::run(RunningContext& ctx) {
	StackArray<float, 16>& registers = ctx.registers;
	StackArray<float, 16>& outputs = ctx.outputs;
	Emitter& emitter = ctx.emitter;
	InputMemoryStream& ip = ctx.instructions;
	const u32 particle_idx = ctx.particle_idx;

	auto getConstValue = [&](const DataStream& str) -> float {
		switch (str.type) {
			case DataStream::LITERAL: return str.value;
			case DataStream::CONST: return m_constants[str.index];
			case DataStream::OUT: return outputs[str.index];
			case DataStream::REGISTER: return registers[str.index];
			case DataStream::CHANNEL: return emitter.channels[str.index].data[particle_idx];
			default: ASSERT(false); return str.value;
		}
	};

	auto getValue = [&](const DataStream& str) -> float& {
		switch (str.type) {
			case DataStream::OUT: return outputs[str.index];
			case DataStream::REGISTER: return registers[str.index];
			case DataStream::CHANNEL: return emitter.channels[str.index].data[particle_idx];
			default: ASSERT(false); return registers[0];
		}
	};

	for (;;) {
		const InstructionType it = ip.read<InstructionType>();
		switch (it) {
			case InstructionType::END:
				return;
			case InstructionType::MESH: {
				DataStream dst = ip.read<DataStream>();
				DataStream index = ip.read<DataStream>();
				const u8 subindex = ip.read<u8>();

				RenderModule* render_module = (RenderModule*)m_world.getModule(MODEL_INSTANCE_TYPE);
				if (!m_world.hasComponent(*m_entity, MODEL_INSTANCE_TYPE)) return; // TODO error message
				
				Model* model = render_module->getModelInstanceModel(*m_entity);
				if (!model || !model->isReady()) return;

				u32 mesh_idx = 0; // TODO random mesh
				if (mesh_idx >= (u32)model->getMeshCount()) return;
				
				const Mesh& mesh = model->getMesh(mesh_idx);
				if (getConstValue(index) < 0) {
					getValue(index) = (float)rand(0, mesh.vertices.size() - 1);
				}

				const u32 idx = u32(getConstValue(index) + 0.5f);
				if (model->getBoneCount() > 0) {
					ModelInstance* mi = render_module->getModelInstance(*m_entity);
					if (!mi->pose) return;

					const float v = mi->model->evalVertexPose(*mi->pose, mesh_idx, idx)[subindex];
					getValue(dst) = v;
				}
				else {
					const float v = mesh.vertices[idx][subindex];
					getValue(dst) = v;
				}
				break;
			}
			case InstructionType::SPLINE: {
				DataStream dst = ip.read<DataStream>();
				DataStream op0 = ip.read<DataStream>();
				const u8 subindex = ip.read<u8>();

				CoreModule* core_module = (CoreModule*)m_world.getModule(SPLINE_TYPE);
				if (!m_world.hasComponent(*m_entity, SPLINE_TYPE)) return; // TODO error message
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

				getValue(dst) = lerp(lerp(p0, p1, rel_t), lerp(p1, p2, rel_t), rel_t);
				break;
			}
			case InstructionType::MUL: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();

				getValue(dst) = getConstValue(op0) * getConstValue(op1);
				break;
			}
			case InstructionType::ADD: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();

				getValue(dst) = getConstValue(op0) + getConstValue(op1);
				break;
			}
			case InstructionType::MULTIPLY_ADD: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();
				const DataStream op2 = ip.read<DataStream>();

				getValue(dst) = getConstValue(op0) * getConstValue(op1) + getConstValue(op2);
				break;
			}
			case InstructionType::MIX: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();
				const DataStream op2 = ip.read<DataStream>();

				getValue(dst) = lerp(getConstValue(op0), getConstValue(op1), getConstValue(op2));
				break;
			}
			case InstructionType::MOD: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();

				getValue(dst) = fmodf(getConstValue(op0), getConstValue(op1));
				break;
			}
			case InstructionType::DIV: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();

				getValue(dst) = getConstValue(op0) / getConstValue(op1);
				break;
			}
			case InstructionType::SUB: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				const DataStream op1 = ip.read<DataStream>();

				getValue(dst) = getConstValue(op0) - getConstValue(op1);
				break;
			}
			case InstructionType::MOV: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				getValue(dst) = getConstValue(op0);
				break;
			}
			case InstructionType::SIN: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				getValue(dst) = sinf(getConstValue(op0));
				break;
			}
			case InstructionType::COS: {
				const DataStream dst = ip.read<DataStream>();
				const DataStream op0 = ip.read<DataStream>();
				getValue(dst) = cosf(getConstValue(op0));
				break;
			}
			case InstructionType::RAND: {
				DataStream dst = ip.read<DataStream>();
				float from = ip.read<float>();
				float to = ip.read<float>();
				
				getValue(dst) = randFloat(from, to);
				break;
			}
			case InstructionType::NOISE: {
				DataStream dst = ip.read<DataStream>();
				DataStream op0 = ip.read<DataStream>();

				getValue(dst) = gnoise(getConstValue(op0));
				break;
			}
			case InstructionType::GRADIENT:
			case InstructionType::FREE2:
			case InstructionType::EMIT:
			case InstructionType::KILL:
			case InstructionType::LT:
			case InstructionType::GT:
				ASSERT(false);
				break;
		}
	}
}

void ParticleSystem::update(float dt, u32 emitter_idx, const Transform& delta_tr, PageAllocator& allocator) {
	PROFILE_FUNCTION();

	Emitter& emitter = m_emitters[emitter_idx];
	ParticleSystemResource::Emitter& res_emitter = m_resource->getEmitters()[emitter_idx];

	if (res_emitter.emit_per_second > 0) {
		emitter.emit_timer += dt;
		const float d = 1.f / res_emitter.emit_per_second;
		m_constants[1] = m_total_time;
		while (emitter.emit_timer > 0) {
			m_constants[1] += d;
			emit(emitter_idx, {});
			emitter.emit_timer -= d;
		}
	}

	if (emitter.particles_count == 0) return;

	m_constants[1] = m_total_time;
	profiler::pushInt("particle count", emitter.particles_count);
	u32* kill_list = (u32*)allocator.allocate(true);
	volatile i32 kill_counter = 0;
	OutputMemoryStream emit_stream(allocator.allocate(true), allocator.PAGE_SIZE);
	jobs::Mutex emit_mutex;

	volatile i32 counter = 0;
	jobs::runOnWorkers([&](){
		PROFILE_FUNCTION();
		Array<float4> reg_mem(m_allocator);
		reg_mem.resize(res_emitter.registers_count * 256);
		for (;;) {
			const i32 from = atomicAdd(&counter, 1024);
			if (from >= (i32)emitter.particles_count) return;

			const i32 fromf4 = from / 4;
			const i32 stepf4 = minimum(1024, emitter.particles_count - from + 3) / 4;
			InputMemoryStream ip = InputMemoryStream(res_emitter.instructions);
			InstructionType itype = ip.read<InstructionType>();

			while (itype != InstructionType::END) {
				switch (itype) {
					case InstructionType::LT:
					case InstructionType::GT: {
						DataStream dst = ip.read<DataStream>();
						DataStream op0 = ip.read<DataStream>();
						const float4* arg0 = getStream(emitter, dst, fromf4, reg_mem.begin());
						const float4* end = arg0 + stepf4;
						const InstructionType inner_type = ip.read<InstructionType>();

						auto helper = [&](auto f, auto arg1_getter){
							float4* arg1 = arg1_getter.get(emitter, fromf4, stepf4, reg_mem.begin());
							for (const float4* beg = arg0; arg0 != end; ++arg0) {
								const float4 tmp = f(*arg0, *arg1);
								const int m = f4MoveMask(tmp);
								if (m) {
									for (int i = 0; i < 4; ++i) {
										const u32 particle_index = u32(from + (arg0 - beg) * 4 + i);
										if ((m & (1 << i)) && particle_index < emitter.particles_count) {
											switch(inner_type) {
												case InstructionType::EMIT: {
													InputMemoryStream tmp_instr((const u8*)ip.getData() + ip.getPosition(), ip.remaining());
													u32 emitter_idx = tmp_instr.read<u32>();
													RunningContext ctx(emitter, m_allocator);
													ctx.instructions.set((const u8*)tmp_instr.getData() + tmp_instr.getPosition(), tmp_instr.remaining());
													ctx.particle_idx = particle_index;
													ctx.registers.resize(res_emitter.registers_count);
													ctx.outputs.resize(m_resource->getEmitters()[emitter_idx].emit_inputs_count);
													run(ctx);
													jobs::enter(&emit_mutex);
													emit_stream.write(emitter_idx);
													emit_stream.write(ctx.outputs.size());
													emit_stream.write(ctx.outputs.begin(), ctx.outputs.byte_size());
													jobs::exit(&emit_mutex);
													break;
												}
												case InstructionType::KILL: {
													const u32 idx = u32(from + (arg0 - beg) * 4 + i);
													if (idx < emitter.particles_count) {
														const i32 kill_idx = atomicIncrement(&kill_counter) - 1;
														if (kill_idx < PageAllocator::PAGE_SIZE / sizeof(kill_list[0])) {
															kill_list[kill_idx] = idx;
														}
														else {
															// TODO
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
								decltype(arg1_getter)::step(arg1);
							}						
						};
					
						switch(op0.type) {
							case DataStream::CHANNEL: {
								ChannelGetter getter(op0);
								helper(itype == InstructionType::GT ? f4CmpGT : f4CmpLT, getter);
								break;
							}
							case DataStream::REGISTER: {
								RegisterGetter getter(op0);
								helper(itype == InstructionType::GT ? f4CmpGT : f4CmpLT, getter);
								break;
							}
							case DataStream::LITERAL: {
								LiteralGetter getter(op0);
								helper(itype == InstructionType::GT ? f4CmpGT : f4CmpLT, getter);
								break;
							}
							case DataStream::CONST: {
								ConstGetter getter(op0);
								helper(itype == InstructionType::GT ? f4CmpGT : f4CmpLT, getter);
								break;
							}
							default: ASSERT(false); break;
						}

						if (inner_type == InstructionType::EMIT) {
							ip.read<u32>(); // emitter idx
							// skip emit subroutine
							RunningContext ctx(emitter, m_allocator);
							ctx.instructions.set((const u8*)ip.getData() + ip.getPosition(), ip.remaining());
							ctx.particle_idx = 0;
							ctx.registers.resize(res_emitter.registers_count);
							ctx.outputs.resize(res_emitter.outputs_count);
							run(ctx);
							ip.setPosition(ip.size() - ctx.instructions.remaining());

						}
						break;
					}	
					case InstructionType::MUL: {
						BinaryHelper helper;
						helper.emitter = &emitter;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						const DataStream dst = ip.read<DataStream>();
						helper.run<f4Mul>(dst, ip);
						break;
					}
					case InstructionType::MOD: {
						BinaryHelper helper;
						helper.emitter = &emitter;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						const DataStream dst = ip.read<DataStream>();
						helper.run<BinaryHelper::f4Mod>(dst, ip);
						break;
					}
					case InstructionType::DIV: {
						BinaryHelper helper;
						helper.emitter = &emitter;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						const DataStream dst = ip.read<DataStream>();
						helper.run<f4Div>(dst, ip);
						break;
					}
					case InstructionType::SUB: {
						BinaryHelper helper;
						helper.emitter = &emitter;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						const DataStream dst = ip.read<DataStream>();
						helper.run<f4Sub>(dst, ip);
						break;
					}
					case InstructionType::MULTIPLY_ADD: {
						TernaryHelper helper;
						helper.emitter = &emitter;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						const DataStream dst = ip.read<DataStream>();
						helper.run<TernaryHelper::madd>(dst, ip);
						break;
					}
					case InstructionType::ADD: {
						BinaryHelper helper;
						helper.emitter = &emitter;
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
						float4* result = getStream(emitter, dst, fromf4, reg_mem.begin());
						const float4* const end = result + stepf4;
				
						if (op0.type == DataStream::CONST) {
							ASSERT(op0.index == 0);
							const float4 src = f4Splat(dt);
							for (; result != end; ++result) {
								*result = src;
							}
						}
						else {
							const float4* src = getStream(emitter, op0, fromf4, reg_mem.begin());

							for (; result != end; ++result, ++src) {
								*result = *src;
							}
						}

						break;
					}
					case InstructionType::COS: {
						const DataStream dst = ip.read<DataStream>();
						const DataStream op0 = ip.read<DataStream>();
						const float* arg = (float*)getStream(emitter, op0, fromf4, reg_mem.begin());
						float* result = (float*)getStream(emitter, dst, fromf4, reg_mem.begin());
						const float* const end = result + stepf4 * 4;

						for (; result != end; ++result, ++arg) {
							*result = cosf(*arg);
						}
						break;
					}
					case InstructionType::SIN: {
						const DataStream dst = ip.read<DataStream>();
						const DataStream op0 = ip.read<DataStream>();
						const float* arg = (float*)getStream(emitter, op0, fromf4, reg_mem.begin());
						float* result = (float*)getStream(emitter, dst, fromf4, reg_mem.begin());
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
		ASSERT(kill_counter <= (i32)emitter.particles_count);
		kill_counter = minimum(kill_counter, i32(PageAllocator::PAGE_SIZE / sizeof(kill_list[0])));
		qsort(kill_list, kill_counter, sizeof(u32), [](const void* a, const void* b) -> int {
			const u32 i = *(u32*)a;
			const u32 j = *(u32*)b;
			if (i < j) return -1;
			if (i > j) return 1;
			return 0;
		});
		const i32 channels_count = res_emitter.channels_count;
		for (i32 j = kill_counter - 1; j >= 0; --j) {
			const u32 last = emitter.particles_count - 1;
			const u32 particle_index = kill_list[j];
			for (i32 i = 0; i < channels_count; ++i) {
				float* data = emitter.channels[i].data;
				data[particle_index] = data[last];
			}
			--emitter.particles_count;
		}
	}

	if (emit_stream.size() > 0) {
		InputMemoryStream blob(emit_stream);
		while (blob.getPosition() < blob.size()) {
			u32 emitter_idx = blob.read<u32>();
			u32 outputs_count = blob.read<u32>();
			const float* outputs = (const float*)blob.skip(outputs_count * sizeof(float));
	
			const Emitter& dst_emitter = m_emitters[emitter_idx];
			
			for (u32 i = 0; i < dst_emitter.resource_emitter.init_emit_count; ++i) {
				emit(emitter_idx, Span(outputs, outputs_count));
			}
		}
	}

	allocator.deallocate(kill_list, true);
	allocator.deallocate(emit_stream.getMutableData(), true);

	if ((u32)m_resource->getFlags() & (u32)ParticleSystemResource::Flags::WORLD_SPACE) {
		// TODO make sure first 3 channels are position
		float* x = emitter.channels[0].data;
		float* y = emitter.channels[1].data;
		float* z = emitter.channels[2].data;
		for (u32 i = 0, c = emitter.particles_count; i < c; ++i) {
			Vec3 p{x[i], y[i], z[i]};
			p = Vec3(delta_tr.transform(p));
			x[i] = p.x;
			y[i] = p.y;
			z[i] = p.z;
		}
	}
}

bool ParticleSystem::update(float dt, PageAllocator& allocator)
{
	if (!m_resource || !m_resource->isReady()) return false;
	
	m_constants[0] = dt;
	m_constants[1] = m_total_time;
	
	if (m_total_time == 0) {
		m_prev_frame_transform = m_world.getTransform(*m_entity);
		for (i32 emitter_idx = 0; emitter_idx < m_emitters.size(); ++emitter_idx) {
			const ParticleSystemResource::Emitter& emitter = m_resource->getEmitters()[emitter_idx];
			if (emitter.emit_inputs_count == 0) {
				for (u32 i = 0, c = emitter.init_emit_count; i < c; ++i) emit(emitter_idx, {});
			}
		}
	}

	m_total_time += dt;

	const Transform world_tr = m_world.getTransform(*m_entity);
	const Transform delta_tr = world_tr.inverted() * m_prev_frame_transform;
	for (i32 emitter_idx = 0; emitter_idx < m_emitters.size(); ++emitter_idx) {
		update(dt, emitter_idx, delta_tr, allocator);
	}

	u32 c = 0;
	for (const Emitter& emitter : m_emitters) {
		c += emitter.particles_count;
	}
	m_prev_frame_transform = world_tr;
	return c == 0 && m_autodestroy;
}


u32 ParticleSystem::Emitter::getParticlesDataSizeBytes() const {
	return ((particles_count + 3) & ~3) * resource_emitter.outputs_count * sizeof(float);
}


void ParticleSystem::Emitter::fillInstanceData(float* data) const {
	if (particles_count == 0) return;

	volatile i32 counter = 0;
	jobs::runOnWorkers([&](){
		PROFILE_FUNCTION();
		Array<float4> reg_mem(system.m_allocator);
		reg_mem.resize(resource_emitter.registers_count * 256);
		for (;;) {
			const u32 from = (u32)atomicAdd(&counter, 1024);
			if (from >= particles_count) return;
			const u32 fromf4 = from / 4;
			const u32 stepf4 = minimum(1024, particles_count - from + 3) / 4;

			InputMemoryStream ip(resource_emitter.instructions);
			ip.skip(resource_emitter.output_offset);

			InstructionType itype = ip.read<InstructionType>();
			while (itype != InstructionType::END) {
				switch (itype) {
					case InstructionType::SIN: {
						DataStream dst_stream = ip.read<DataStream>();
						DataStream op0 = ip.read<DataStream>();
						const float* arg = (float*)getStream(*this, op0, fromf4, reg_mem.begin());
						
						if (dst_stream.type == DataStream::OUT) {
							u8 output_idx = dst_stream.index;
							const u32 stride = resource_emitter.outputs_count;
							float* dst = data + output_idx + fromf4 * 4 * stride;
							for (u32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
								dst[j] = sinf(arg[i]);
							}
						}
						else {
							float* result = (float*)getStream(*this, dst_stream, fromf4, reg_mem.begin());
							const float* const end = result + stepf4 * 4;

							for (; result != end; ++result, ++arg) {
								*result = sinf(*arg);
							}
						}
						break;
					}
					case InstructionType::NOISE: {
						DataStream dst_stream = ip.read<DataStream>();
						DataStream op0 = ip.read<DataStream>();

						const float* arg = (float*)getStream(*this, op0, fromf4, reg_mem.begin());
						if (dst_stream.type == DataStream::OUT) {
							i32 output_idx = dst_stream.index;
							const u32 stride = resource_emitter.outputs_count;
							float* dst = data + output_idx + fromf4 * 4 * stride;
							for (u32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
								dst[j] = gnoise(arg[i]);
							}
						}
						else {
							float* result = (float*)getStream(*this, dst_stream, fromf4, reg_mem.begin());
							const float* const end = result + stepf4 * 4;

							for (; result != end; ++result, ++arg) {
								*result = gnoise(*arg);
							}
						}
						break;
					}
					case InstructionType::COS: {
						DataStream dst_stream = ip.read<DataStream>();
						DataStream op0 = ip.read<DataStream>();
						const float* arg = (float*)getStream(*this, op0, fromf4, reg_mem.begin());
						if (dst_stream.type == DataStream::OUT) {
							i32 output_idx = dst_stream.index;
							const u32 stride = resource_emitter.outputs_count;
							float* dst = data + output_idx + fromf4 * 4 * stride;
							for (u32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
								dst[j] = cosf(arg[i]);
							}
						}
						else {
							float* result = (float*)getStream(*this, dst_stream, fromf4, reg_mem.begin());
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
					case InstructionType::MOD: {
						BinaryHelper helper;
						helper.emitter = this;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						helper.out_mem = data;
						DataStream dst = ip.read<DataStream>();
						helper.run<BinaryHelper::f4Mod>(dst, ip);
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
					case InstructionType::DIV: {
						BinaryHelper helper;
						helper.emitter = this;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						helper.out_mem = data;
						DataStream dst = ip.read<DataStream>();
						helper.run<f4Div>(dst, ip);
						break;
					}
					case InstructionType::SUB: {
						BinaryHelper helper;
						helper.emitter = this;
						helper.fromf4 = fromf4;
						helper.stepf4 = stepf4;
						helper.reg_mem = reg_mem.begin();
						helper.out_mem = data;
						DataStream dst = ip.read<DataStream>();
						helper.run<f4Sub>(dst, ip);
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
					case InstructionType::SPLINE: {
						DataStream dst = ip.read<DataStream>();
						DataStream op0 = ip.read<DataStream>();
						const u8 subindex = ip.read<u8>();
						ASSERT(dst.type == DataStream::OUT);

						CoreModule* core_module = (CoreModule*)system.m_world.getModule(SPLINE_TYPE);
						if (!system.m_world.hasComponent(*system.m_entity, SPLINE_TYPE)) return; // TODO error message
						Spline& spline = core_module->getSpline(*system.m_entity);

						const u8 output_idx = dst.index;
						const u32 stride = resource_emitter.outputs_count;
						const float* arg = (float*)getStream(*this, op0, fromf4, reg_mem.begin());
						float* out = data + output_idx + fromf4 * 4 * stride;
						const i32 last_idx = spline.points.size() - 2;
						for (u32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
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
						ip.read(keys, sizeof(keys[0]) * count);
						ip.read(values, sizeof(values[0]) * count);

						ASSERT(dst.type == DataStream::OUT);
						const u8 output_idx = dst.index;
						const u32 stride = resource_emitter.outputs_count;
						const float* arg = (float*)getStream(*this, op0, fromf4, reg_mem.begin());
						float* out = data + output_idx + fromf4 * 4 * stride;
						for (u32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
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
						const u32 stride = resource_emitter.outputs_count;
						DataStream dst = ip.read<DataStream>();
						DataStream op0 = ip.read<DataStream>();

						if (op0.type == DataStream::LITERAL) {
							const float arg = op0.value;
							ASSERT(dst.type == DataStream::OUT);
							u8 output_idx = dst.index;
							float* res = data + output_idx + fromf4 * 4 * stride;
							for (u32 i = 0; i < stepf4 * 4; ++i) {
								res[i * stride] = arg;
							}
						}
						else {
							const float* arg = (float*)getStream(*this, op0, fromf4, reg_mem.begin());
							ASSERT(dst.type == DataStream::OUT);
							u8 output_idx = dst.index;
							float* res = data + output_idx + fromf4 * 4 * stride;
							for (u32 i = 0, j = 0; i < stepf4 * 4; ++i, j += stride) {
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