#include "particle_system.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/lua_wrapper.h"
#include "engine/math_utils.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/simd.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "renderer/material.h"
#include "renderer/render_scene.h"
#include "engine/universe/universe.h"
#include <cmath>
#include <cstring>


namespace Lumix
{


Resource* ParticleEmitterResourceManager::createResource(const Path& path)
{
	return LUMIX_NEW(m_allocator, ParticleEmitterResource)(path, *this, m_allocator);
}


void ParticleEmitterResourceManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(m_allocator, static_cast<ParticleEmitterResource*>(&resource));
}


const ResourceType ParticleEmitterResource::TYPE = ResourceType("particle_emitter");


ParticleEmitterResource::ParticleEmitterResource(const Path& path, ResourceManager& manager, IAllocator& allocator)
	: Resource(path, manager, allocator)
	, m_bytecode(allocator)
	, m_material(nullptr)
{
}


void ParticleEmitterResource::unload()
{
	m_bytecode.clear();
}


enum class InstructionArgType : u8
{
	CHANNEL,
	CONSTANT,
	REGISTER,
	LITERAL
};


enum class Instructions : u8
{
	END,
	ADD,
	COS,
	SIN,
	ADD_CONST,
	SUB,
	SUB_CONST,
	MUL,
	MULTIPLY_ADD,
	MULTIPLY_CONST_ADD,
	LT,
	OUTPUT,
	OUTPUT_CONST,
	MOV,
	RAND,
	KILL,
	EMIT
};


struct Compiler
{
	struct DataStream
	{
		enum Type : u8 {
			CHANNEL,
			CONST,
			REGISTER,
			LITERAL
		};

		Type type;
		u8 index; // in the group of the same typed streams
		float value;
	};

	Compiler(OutputBlob& bytecode) 
		: m_bytecode(bytecode) 
	{
		m_streams[0].type = DataStream::CONST;
		m_streams[0].index = 0;
		++m_streams_count;
		++m_constants_count;
	}

	static Compiler* getCompiler(lua_State* L)
	{
		lua_getfield(L, LUA_GLOBALSINDEX, "this");
		Compiler* compiler = (Compiler*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		return compiler;
	}

	DataStream getDataStream(lua_State* L, int index) const
	{
		const int ds_idx = LuaWrapper::checkArg<int>(L, index);
		return m_streams[ds_idx];
	}

	static int material(lua_State* L)
	{
		lua_getfield(L, LUA_GLOBALSINDEX, "emitter");
		ParticleEmitterResource* res = (ParticleEmitterResource*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		
		const char* path = LuaWrapper::checkArg<const char*>(L, 1);
		res->setMaterial(Path(path));

		return 0;
	}

	static int newChannel(lua_State* L)
	{
		Compiler* c = getCompiler(L);

		c->m_streams[c->m_streams_count].type = DataStream::CHANNEL;
		c->m_streams[c->m_streams_count].index = c->m_channels_count;

		lua_pushinteger(L, c->m_streams_count);

		++c->m_channels_count;
		++c->m_streams_count;

		return 1;
	}
	

	static int newRegister(lua_State* L)
	{
		Compiler* c = getCompiler(L);

		c->m_streams[c->m_streams_count].type = DataStream::REGISTER;
		c->m_streams[c->m_streams_count].index = c->m_registers_count;

		lua_pushinteger(L, c->m_streams_count);
		
		++c->m_registers_count;
		++c->m_streams_count;

		return 1;
	}
	

	static int newLiteral(lua_State* L)
	{
		Compiler* c = getCompiler(L);
		const float value = LuaWrapper::checkArg<float>(L, 1);

		c->m_streams[c->m_streams_count].type = DataStream::LITERAL;
		c->m_streams[c->m_streams_count].index = c->m_literals_count;
		c->m_streams[c->m_streams_count].value = value;

		lua_pushinteger(L, c->m_streams_count);
		
		++c->m_literals_count;
		++c->m_streams_count;

		return 1;
	}
	
	
	static void writeUnaryInstruction(Instructions instruction, lua_State* L)
	{
		Compiler* c = getCompiler(L);
		c->m_bytecode.write(instruction);
		
		const DataStream dst = c->getDataStream(L, 1);
		const DataStream arg = c->getDataStream(L, 2);

		c->m_bytecode.write(dst.type);
		c->m_bytecode.write(dst.index);
		c->m_bytecode.write(arg.type);
		c->m_bytecode.write(arg.index);
	}
	
	
	static void writeBinaryInstruction(Instructions instruction, lua_State* L)
	{
		Compiler* c = getCompiler(L);
		c->m_bytecode.write(instruction);
		
		const DataStream dst = c->getDataStream(L, 1);
		const DataStream arg0 = c->getDataStream(L, 2);
		const DataStream arg1 = c->getDataStream(L, 3);

		c->m_bytecode.write(dst.type);
		c->m_bytecode.write(dst.index);
		c->m_bytecode.write(arg0.type);
		c->m_bytecode.write(arg0.index);
		c->m_bytecode.write(arg1.type);
		c->m_bytecode.write(arg1.index);
	}

	static int sin(lua_State* L)
	{
		writeUnaryInstruction(Instructions::SIN, L);
		return 0;
	}


	static int cos(lua_State* L)
	{
		writeUnaryInstruction(Instructions::COS, L);
		return 0;
	}


	static int mul(lua_State* L)
	{
		writeBinaryInstruction(Instructions::MUL, L);
		return 0;
	}


	static int mov(lua_State* L)
	{
		writeUnaryInstruction(Instructions::MOV, L);
		return 0;
	}


	static int add(lua_State* L)
	{
		writeBinaryInstruction(Instructions::ADD, L);
		return 0;
	}


	static int out(lua_State* L)
	{
		Compiler* c = getCompiler(L);
		c->m_bytecode.write(Instructions::OUTPUT);
		const DataStream src = c->getDataStream(L, 1);
		c->m_bytecode.write(src.type);
		c->m_bytecode.write(src.index);
		++c->m_outputs_count;
		return 0;
	}


	DataStream m_streams[64];
	int m_streams_count = 0;
	int m_channels_count = 0;
	int m_registers_count = 0;
	int m_literals_count = 0;
	int m_constants_count = 0;
	int m_outputs_count = 0;
	int m_bytecode_offset = 0;
	OutputBlob& m_bytecode;
	bool m_error = false;
};


void ParticleEmitterResource::setMaterial(const Path& path)
{
	Material* material = m_resource_manager.getOwner().load<Material>(path);
	if (m_material) {
		Material* material = m_material;
		m_material = nullptr;
		removeDependency(*m_material);
		m_material->getResourceManager().unload(*m_material);
	}
	m_material = material;
	if (m_material) {
		addDependency(*m_material);
	}
}


bool ParticleEmitterResource::load(FS::IFile& file)
{
	// TODO reuse state
	lua_State* L = luaL_newstate();

	Compiler compiler(m_bytecode);

	lua_pushlightuserdata(L, &compiler);
	lua_setfield(L, LUA_GLOBALSINDEX, "this");
	lua_pushlightuserdata(L, this);
	lua_setfield(L, LUA_GLOBALSINDEX, "emitter");

	#define DEFINE_LUA_FUNC(name) \
		lua_pushcclosure(L, Compiler::name, 0); \
		lua_setfield(L, LUA_GLOBALSINDEX, #name);

	DEFINE_LUA_FUNC(material);
	DEFINE_LUA_FUNC(newChannel);
	DEFINE_LUA_FUNC(newRegister);
	DEFINE_LUA_FUNC(newLiteral);

	DEFINE_LUA_FUNC(mov);

	DEFINE_LUA_FUNC(add);
	DEFINE_LUA_FUNC(mul);
	DEFINE_LUA_FUNC(cos);
	DEFINE_LUA_FUNC(sin);

	DEFINE_LUA_FUNC(out);

	#undef DEFINE_LUA_FUNC

	if(luaL_loadbuffer(L, (const char*)file.getBuffer(), file.size(), getPath().c_str()) != 0) {
		g_log_error.log("Renderer") << lua_tostring(L, -1);
		lua_pop(L, 1);
		lua_close(L);
		return false;
	}

	if (lua_pcall(L, 0, 0, 0) != 0) {
		g_log_error.log("Renderer") << lua_tostring(L, -1);
		lua_pop(L, 1);
		lua_close(L);
		return false;
	}

	lua_getfield(L, LUA_GLOBALSINDEX, "update");
	if(lua_isfunction(L, -1)) {
		lua_pushinteger(L, 0);
		if(lua_pcall(L, 1, 0, 0) != 0) {
			g_log_error.log("Renderer") << lua_tostring(L, -1);
			lua_pop(L, 1);
			lua_close(L);
			return false;
		}
	}
	lua_pop(L, 1);
	compiler.m_bytecode.write(Instructions::END);
	m_emit_byte_offset = compiler.m_bytecode.getPos();

	lua_getfield(L, LUA_GLOBALSINDEX, "emit");
	if(lua_isfunction(L, -1)) {
		if(lua_pcall(L, 0, 0, 0) != 0) {
			g_log_error.log("Renderer") << lua_tostring(L, -1);
			lua_pop(L, 1);
			lua_close(L);
			return false;
		}
	}
	lua_pop(L, 1);
	compiler.m_bytecode.write(Instructions::END);
	m_channels_count = compiler.m_channels_count;
	m_registers_count = compiler.m_registers_count;
	m_outputs_count = compiler.m_outputs_count;

	int num_literals = 0;
	for(int i = 0; i < compiler.m_streams_count; ++i) {
		if(compiler.m_streams[i].type == Compiler::DataStream::LITERAL) {
			m_literals[num_literals] = compiler.m_streams[i].value;
			++num_literals;
		}
	}
	
	lua_close(L);

	if (!m_material) return false;
	return true;
}



ParticleEmitter::ParticleEmitter(EntityPtr entity, IAllocator& allocator)
	: m_allocator(allocator)
	, m_entity(entity)
	, m_emit_buffer(allocator)
	, m_instance_data(allocator)
{
}


ParticleEmitter::~ParticleEmitter()
{
	for (const Channel& c : m_channels) {
		m_allocator.deallocate_aligned(c.data);
	}
}


void ParticleEmitter::setResource(ParticleEmitterResource* res)
{
	if (m_resource) {
		m_resource->getResourceManager().unload(*m_resource);
	}
	m_resource = res;
}


void ParticleEmitter::emit(const float* args)
{
	const int channels_count = m_resource->getChannelsCount();
	if (m_particles_count == m_capacity)
	{
		int new_capacity = Math::maximum(16, m_capacity << 1);
		for (int i = 0; i < channels_count; ++i)
		{
			m_channels[i].data = (float*)m_allocator.reallocate_aligned(m_channels[i].data, new_capacity * sizeof(float), 16);
		}
		m_capacity = new_capacity;
	}


	const OutputBlob& bytecode = m_resource->getBytecode();
	const u8* emit_bytecode = (const u8*)bytecode.getData() + m_resource->getEmitByteOffset();
	InputBlob blob(emit_bytecode, bytecode.getPos() - m_resource->getEmitByteOffset());
	for (;;)
	{
		const u8 instruction = blob.read<u8>();
		switch((Instructions)instruction)
		{
			case Instructions::END:
				++m_particles_count;
				return;
			/*case Instructions::ADD: {
				ASSERT((flag & 3) == (u8)InstructionArgType::CHANNEL);
				ASSERT(((flag >> 2) & 3) == (u8)InstructionArgType::CHANNEL);
				ASSERT(((flag >> 4) & 3) == (u8)InstructionArgType::REGISTER);
				u8 result_ch = blob.read<u8>();
				u8 op1_ch = blob.read<u8>();
				u8 arg_idx = blob.read<u8>();
				m_channels[result_ch].data[m_particles_count] = m_channels[op1_ch].data[m_particles_count] + args[arg_idx];
				break;
			}*/
			case Instructions::MOV: {
				const auto dst_type = blob.read<Compiler::DataStream::Type>();
				u8 dst_idx = blob.read<u8>();
				const auto src_type = blob.read<Compiler::DataStream::Type>();
				u8 src_idx = blob.read<u8>();
				ASSERT(src_type == Compiler::DataStream::LITERAL);
				ASSERT(dst_type == Compiler::DataStream::CHANNEL);
				const float value = m_resource->getLiteralValue(src_idx);
				m_channels[dst_idx].data[m_particles_count] = value;
				break;
			}
			/*case Instructions::RAND: {
				ASSERT((flag & 3) == (u8)InstructionArgType::CHANNEL);
				ASSERT(((flag >> 2) & 3) == (u8)InstructionArgType::LITERAL);
				ASSERT(((flag >> 4) & 3) == (u8)InstructionArgType::LITERAL);
				u8 ch = blob.read<u8>();
				float from = blob.read<float>();
				float to = blob.read<float>();
				m_channels[ch].data[m_particles_count] = Math::randFloat(from, to);
				break;
			}*/
			default:
				ASSERT(false);
				break;
		}
	}
}

static bool isWhitespace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }


void ParticleEmitter::serialize(OutputBlob& blob)
{
	blob.write(m_entity);
	blob.writeString(m_resource ? m_resource->getPath().c_str() : "");
}


void ParticleEmitter::deserialize(InputBlob& blob, ResourceManagerHub& manager)
{
	blob.read(m_entity);
	char path[MAX_PATH_LENGTH];
	blob.readString(path, lengthOf(path));
	ResourceManager* material_manager = manager.get(ParticleEmitterResource::TYPE);
	auto* res = manager.load<ParticleEmitterResource>(Path(path));
	setResource(res);
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


void ParticleEmitter::execute(InputBlob& blob, int particle_index)
{
	if (particle_index >= m_particles_count) return;
	for (;;)
	{
		u8 instruction = blob.read<u8>();
		/*u8 flag = */blob.read<u8>();

		switch ((Instructions)instruction)
		{
			case Instructions::END:
				return;
			case Instructions::KILL:
				kill(particle_index);
				ASSERT(blob.read<u8>() == (u8)Instructions::END);
				return;
			case Instructions::EMIT:
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


static float4* getStream(ParticleEmitter& emitter
	, Compiler::DataStream::Type type
	, int idx
	, int particles_count
	, float4* register_mem)
{
	switch(type) {
		case Compiler::DataStream::CHANNEL: return (float4*)emitter.getChannelData(idx);
		case Compiler::DataStream::REGISTER: return register_mem + particles_count * idx;
		default: ASSERT(false); return nullptr;
	}
}


void ParticleEmitter::update(float dt)
{
	if (!m_resource || !m_resource->isReady()) return;

	emit(nullptr); // TODO remove

	PROFILE_FUNCTION();
	PROFILE_INT("particle count", m_particles_count);
	if (m_particles_count == 0) return;

	m_emit_buffer.clear();
	m_constants[0].value = dt;
	const OutputBlob& bytecode = m_resource->getBytecode();
	InputBlob blob(bytecode.getData(), bytecode.getPos());
	// TODO
	Array<float4> reg_mem(m_allocator);
	reg_mem.resize(m_resource->getRegistersCount() * ((m_particles_count + 3) >> 2));
	m_instances_count = m_particles_count;
	m_instance_data.resize(m_particles_count * m_resource->getOutputsCount() * 4);
	int output_idx = 0;

	for (;;)
	{
		u8 instruction = blob.read<u8>();
		switch ((Instructions)instruction)
		{
			case Instructions::END:
				goto end;
			case Instructions::MUL: {
				const auto dst_type = blob.read<Compiler::DataStream::Type>();
				const u8 dst_idx = blob.read<u8>();
				const auto arg0_type = blob.read<Compiler::DataStream::Type>();
				const u8 arg0_idx = blob.read<u8>();
				const auto arg1_type = blob.read<Compiler::DataStream::Type>();
				const u8 arg1_idx = blob.read<u8>();
				
				// TODO
				ASSERT(arg1_type == Compiler::DataStream::LITERAL);
				const float4 arg1 = f4Splat(m_resource->getLiteralValue(arg1_idx));

				const float4* arg0 = getStream(*this, arg0_type, arg0_idx, m_particles_count, reg_mem.begin());
				float4* result = getStream(*this, dst_type, dst_idx, m_particles_count, reg_mem.begin());
				const float4* const end = result + ((m_particles_count + 3) >> 2);

				for (; result != end; ++result, ++arg0) {
					*result = f4Mul(*arg0, arg1);
				}

				break;
			}
			case Instructions::ADD: {
				const auto dst_type = blob.read<Compiler::DataStream::Type>();
				const u8 dst_idx = blob.read<u8>();
				const auto arg0_type = blob.read<Compiler::DataStream::Type>();
				const u8 arg0_idx = blob.read<u8>();
				const auto arg1_type = blob.read<Compiler::DataStream::Type>();
				const u8 arg1_idx = blob.read<u8>();
				
				// TODO
				ASSERT(arg1_type == Compiler::DataStream::CONST && arg1_idx == 0);
				const float4 arg1 = f4Splat(dt);

				const float4* arg0 = getStream(*this, arg0_type, arg0_idx, m_particles_count, reg_mem.begin());
				float4* result = getStream(*this, dst_type, dst_idx, m_particles_count, reg_mem.begin());
				const float4* const end = result + ((m_particles_count + 3) >> 2);

				for (; result != end; ++result, ++arg0) {
					*result = f4Add(*arg0, arg1);
				}

				break;
			}
			case Instructions::COS: {
				const auto dst_type = blob.read<Compiler::DataStream::Type>();
				const u8 dst_idx = blob.read<u8>();
				const auto arg_type = blob.read<Compiler::DataStream::Type>();
				const u8 arg_idx = blob.read<u8>();
				
				const float* arg = (float*)getStream(*this, arg_type, arg_idx, m_particles_count, reg_mem.begin());
				float* result = (float*)getStream(*this, dst_type, dst_idx, m_particles_count, reg_mem.begin());
				const float* const end = result + ((m_particles_count + 3) & ~3);

				for (; result != end; ++result, ++arg) {
					*result = cosf(*arg);
				}
				break;
			}
			case Instructions::SIN: {
				const auto dst_type = blob.read<Compiler::DataStream::Type>();
				const u8 dst_idx = blob.read<u8>();
				const auto arg_type = blob.read<Compiler::DataStream::Type>();
				const u8 arg_idx = blob.read<u8>();
				
				const float* arg = (float*)getStream(*this, arg_type, arg_idx, m_particles_count, reg_mem.begin());
				float* result = (float*)getStream(*this, dst_type, dst_idx, m_particles_count, reg_mem.begin());
				const float* const end = result + ((m_particles_count + 3) & ~3);

				for (; result != end; ++result, ++arg) {
					*result = sinf(*arg);
				}
				break;
			}
			case Instructions::OUTPUT: {
				const auto arg_type = blob.read<Compiler::DataStream::Type>();
				const u8 arg_idx = blob.read<u8>();
				const float* arg = (float*)getStream(*this, arg_type, arg_idx, m_particles_count, reg_mem.begin());
				float* dst = m_instance_data.begin() + output_idx;
				++output_idx;
				const int stride = m_resource->getOutputsCount();
				for (int i = 0; i < m_particles_count; ++i) {
					dst[i * stride] =  arg[i];
				}
				break;
			}
			default:
				ASSERT(false);
				break;
		}
	}

	end:
		InputBlob emit_buffer(m_emit_buffer);
		while (emit_buffer.getPosition() < emit_buffer.getSize())
		{
			u8 count = emit_buffer.read<u8>();
			float args[16];
			ASSERT(count <= lengthOf(args));
			emit_buffer.read(args, sizeof(args[0]) * count);
			emit(args);
		}
}

// TODO
/*
bgfx::InstanceDataBuffer ParticleEmitter::generateInstanceBuffer() const
{
	bgfx::InstanceDataBuffer buffer;
	buffer.size = 0;
	buffer.data = nullptr;
	if (m_particles_count == 0) return buffer;
	
	u16 stride = ((u16)(sizeof(float) * m_outputs_per_particle) + 15) & ~15;
	if (bgfx::getAvailInstanceDataBuffer(m_particles_count, stride) < (u32)m_particles_count) return buffer;
	
	bgfx::allocInstanceDataBuffer(&buffer, m_particles_count, stride);

	
	InputBlob blob(&m_bytecode[0] + m_output_bytecode_offset, m_bytecode.size() - m_output_bytecode_offset);

	int output_idx = 0;
	for (;;)
	{
		u8 instruction = blob.read<u8>();
		switch ((Instructions)instruction)
		{
			case Instructions::END:
				return buffer;
			case Instructions::OUTPUT:
			{
				u8 value_idx = blob.read<u8>();

				const float* value = m_channels[value_idx].data;
				float* output = (float*)&buffer.data[output_idx * sizeof(float)];
				int output_size = stride >> 2;
				float* end = output + m_particles_count * output_size;

				for (; output != end; ++value, output += output_size)
				{
					*output = *value;
				}
				++output_idx;
				break;
			}
			case Instructions::OUTPUT_CONST:
			{
				float value = blob.read<float>();

				float* output = (float*)&buffer.data[output_idx * sizeof(float)];
				int output_size = stride >> 2;
				float* end = output + m_particles_count * output_size;

				for (; output != end; output += output_size)
				{
					*output = value;
				}
				++output_idx;
				break;
			}
			default:
				ASSERT(false);
				break;
		}
	}
}*/


} // namespace Lumix