#include "particle_system.h"
#include "engine/crc32.h"
#include "engine/crt.h"
#include "engine/mt/atomic.h"
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
#include "engine/universe/universe.h"


namespace Lumix
{


const ResourceType ParticleEmitterResource::TYPE = ResourceType("particle_emitter");


ParticleEmitterResource::ParticleEmitterResource(const Path& path
	, ResourceManager& manager
	, Renderer& renderer
	, IAllocator& allocator)
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

	Compiler(OutputMemoryStream& bytecode) 
		: m_bytecode(bytecode) 
	{
		m_streams[0].type = DataStream::CONST;
		m_streams[0].index = 0;
		m_streams[1].type = DataStream::CONST;
		m_streams[1].index = 1;
		m_streams[2].type = DataStream::CONST;
		m_streams[2].index = 2;
		m_streams[3].type = DataStream::CONST;
		m_streams[3].index = 3;
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

	void writeDataStream(lua_State* L, int index) const
	{
		if(lua_istable(L, index)) {
			lua_rawgeti(L, index, 1);
			if(!lua_isnumber(L, -1)) LuaWrapper::argError(L, index, "data source");
			
			const int ds_idx = (int)lua_tointeger(L, -1);
			lua_pop(L, 1);
			const DataStream& d = m_streams[ds_idx];
			m_bytecode.write(d.type);
			m_bytecode.write(d.index);
		}
		else {
			const float value = LuaWrapper::checkArg<float>(L, index);
			m_bytecode.write(DataStream::LITERAL);
			m_bytecode.write(value);
		}
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

		lua_newtable(L);
		lua_pushinteger(L, c->m_streams_count);
		lua_rawseti(L, -2, 1);

		++c->m_channels_count;
		++c->m_streams_count;

		return 1;
	}
	

	static int newRegister(lua_State* L)
	{
		Compiler* c = getCompiler(L);

		c->m_streams[c->m_streams_count].type = DataStream::REGISTER;
		c->m_streams[c->m_streams_count].index = c->m_registers_count;

		lua_newtable(L);
		lua_pushinteger(L, c->m_streams_count);
		lua_rawseti(L, -2, 1);

		++c->m_registers_count;
		++c->m_streams_count;

		return 1;
	}
	
	
	static void writeUnaryInstruction(Instructions instruction, lua_State* L)
	{
		Compiler* c = getCompiler(L);
		c->m_bytecode.write(instruction);
		
		c->writeDataStream(L, 1);
		c->writeDataStream(L, 2);
	}
	
	
	static void writeBinaryInstruction(Instructions instruction, lua_State* L)
	{
		Compiler* c = getCompiler(L);
		c->m_bytecode.write(instruction);
		
		c->writeDataStream(L, 1);
		c->writeDataStream(L, 2);
		c->writeDataStream(L, 3);
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


	static int rand(lua_State* L)
	{
		writeBinaryInstruction(Instructions::RAND, L);
		return 0;
	}


	static int sub(lua_State* L)
	{
		writeBinaryInstruction(Instructions::SUB, L);
		return 0;
	}


	static int out(lua_State* L)
	{
		Compiler* c = getCompiler(L);
		c->m_bytecode.write(Instructions::OUTPUT);
		c->writeDataStream(L, 1);
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
	OutputMemoryStream& m_bytecode;
	bool m_error = false;
};


void ParticleEmitterResource::setMaterial(const Path& path)
{
	Material* material = m_resource_manager.getOwner().load<Material>(path);
	if (m_material) {
		Material* material = m_material;
		m_material = nullptr;
		removeDependency(*material);
		material->getResourceManager().unload(*material);
	}
	m_material = material;
	if (m_material) {
		addDependency(*m_material);
	}
}


bool ParticleEmitterResource::load(u64 size, const u8* mem)
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

	DEFINE_LUA_FUNC(mov);

	DEFINE_LUA_FUNC(add);
	DEFINE_LUA_FUNC(sub);
	DEFINE_LUA_FUNC(mul);
	DEFINE_LUA_FUNC(cos);
	DEFINE_LUA_FUNC(sin);
	DEFINE_LUA_FUNC(rand);

	DEFINE_LUA_FUNC(out);

	#undef DEFINE_LUA_FUNC

	if(luaL_loadbuffer(L, (const char*)mem, size, getPath().c_str()) != 0) {
		logError("Renderer") << lua_tostring(L, -1);
		lua_pop(L, 1);
		lua_close(L);
		return false;
	}

	if (lua_pcall(L, 0, 0, 0) != 0) {
		logError("Renderer") << lua_tostring(L, -1);
		lua_pop(L, 1);
		lua_close(L);
		return false;
	}

	lua_getfield(L, LUA_GLOBALSINDEX, "update");
	if(lua_isfunction(L, -1)) {
		
		lua_newtable(L);
		lua_pushinteger(L, 0);
		lua_rawseti(L, -2, 1);

		if(lua_pcall(L, 1, 0, 0) != 0) {
			logError("Renderer") << lua_tostring(L, -1);
			lua_pop(L, 1);
			lua_close(L);
			return false;
		}
	}
	lua_pop(L, 1);
	compiler.m_bytecode.write(Instructions::END);
	m_emit_byte_offset = (int)compiler.m_bytecode.getPos();

	lua_getfield(L, LUA_GLOBALSINDEX, "emit");
	if(lua_isfunction(L, -1)) {

		lua_newtable(L);
		lua_pushinteger(L, 1);
		lua_rawseti(L, -2, 1);

		lua_newtable(L);
		lua_pushinteger(L, 2);
		lua_rawseti(L, -2, 1);

		lua_newtable(L);
		lua_pushinteger(L, 3);
		lua_rawseti(L, -2, 1);

		if(!LuaWrapper::pcall(L, 3, 0)) {
			lua_close(L);
			return false;
		}
	}
	lua_pop(L, 1);
	compiler.m_bytecode.write(Instructions::END);

	m_output_byte_offset = (int)compiler.m_bytecode.getPos();
	lua_getfield(L, LUA_GLOBALSINDEX, "output");
	if(lua_isfunction(L, -1)) {
		if(lua_pcall(L, 0, 0, 0) != 0) {
			logError("Renderer") << lua_tostring(L, -1);
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

	lua_close(L);

	if (!m_material) {
		logError("Renderer") << getPath() << " has no material.";
		return false;
	}
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


float ParticleEmitter::readSingleValue(InputMemoryStream& blob) const
{
	const auto type = blob.read<Compiler::DataStream::Type>();
	switch(type) {
		case Compiler::DataStream::LITERAL:
			return blob.read<float>();
			break;
		case Compiler::DataStream::CHANNEL: {
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
	const int channels_count = m_resource->getChannelsCount();
	if (m_particles_count == m_capacity)
	{
		int new_capacity = maximum(16, m_capacity << 1);
		for (int i = 0; i < channels_count; ++i)
		{
			m_channels[i].data = (float*)m_allocator.reallocate_aligned(m_channels[i].data, new_capacity * sizeof(float), 16);
		}
		m_capacity = new_capacity;
	}


	const OutputMemoryStream& bytecode = m_resource->getBytecode();
	const u8* emit_bytecode = (const u8*)bytecode.getData() + m_resource->getEmitByteOffset();
	InputMemoryStream blob(emit_bytecode, bytecode.getPos() - m_resource->getEmitByteOffset());
	for (;;) {
		const u8 instruction = blob.read<u8>();
		switch((Instructions)instruction) {
			case Instructions::END:
				++m_particles_count;
				return;
			case Instructions::MOV: {
				const Compiler::DataStream::Type dst_type = blob.read<Compiler::DataStream::Type>();
				u8 dst_idx = blob.read<u8>();
				ASSERT(dst_type == Compiler::DataStream::CHANNEL);
				const float value = readSingleValue(blob);
				m_channels[dst_idx].data[m_particles_count] = value;
				break;
			}
			case Instructions::RAND: {
				const Compiler::DataStream::Type dst_type = blob.read<Compiler::DataStream::Type>();
				ASSERT(dst_type == Compiler::DataStream::CHANNEL);
				u8 dst_idx = blob.read<u8>();
				
				const float from = readSingleValue(blob);
				const float to = readSingleValue(blob);
				
				m_channels[dst_idx].data[m_particles_count] = randFloat(from, to);
				break;
			}
			default:
				ASSERT(false);
				break;
		}
	}
}


void ParticleEmitter::serialize(IOutputStream& blob)
{
	blob.write(m_entity);
	blob.writeString(m_resource ? m_resource->getPath().c_str() : "");
}


void ParticleEmitter::deserialize(IInputStream& blob, ResourceManagerHub& manager)
{
	blob.read(m_entity);
	char path[MAX_PATH_LENGTH];
	blob.readString(Span(path));
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


void ParticleEmitter::execute(InputMemoryStream& blob, int particle_index)
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


static float4* getStream(const ParticleEmitter& emitter
	, Compiler::DataStream::Type type
	, int idx
	, int particles_count
	, float4* register_mem)
{
	switch(type) {
		case Compiler::DataStream::CHANNEL: return (float4*)emitter.getChannelData(idx); //-V1032
		case Compiler::DataStream::REGISTER: return register_mem + particles_count * idx;
		default: ASSERT(false); return nullptr;
	}
}


void ParticleEmitter::update(float dt)
{
	if (!m_resource || !m_resource->isReady()) return;

	// TODO remove
	static bool xx = [&]{
		for (int i = 0; i < 450'000; ++i) {
			emit(nullptr);
		}
		return true;
	}();

	PROFILE_FUNCTION();
	Profiler::pushInt("particle count", m_particles_count);
	if (m_particles_count == 0) return;

	m_emit_buffer.clear();
	m_constants[0].value = dt;
	const OutputMemoryStream& bytecode = m_resource->getBytecode();
	InputMemoryStream blob(bytecode.getData(), bytecode.getPos());
	// TODO
	Array<float4> reg_mem(m_allocator);
	reg_mem.resize(m_resource->getRegistersCount() * ((m_particles_count + 3) >> 2));
	m_instances_count = m_particles_count;

	for (;;)
	{
		u8 instruction = blob.read<u8>();
		switch ((Instructions)instruction)
		{
			case Instructions::END:
				goto end;
			case Instructions::MUL: {
				const Compiler::DataStream::Type dst_type = blob.read<Compiler::DataStream::Type>();
				const u8 dst_idx = blob.read<u8>();
				const Compiler::DataStream::Type arg0_type = blob.read<Compiler::DataStream::Type>();
				const u8 arg0_idx = blob.read<u8>();
				const Compiler::DataStream::Type arg1_type = blob.read<Compiler::DataStream::Type>();
				
				const float4* arg0 = getStream(*this, arg0_type, arg0_idx, m_particles_count, reg_mem.begin());
				float4* result = getStream(*this, dst_type, dst_idx, m_particles_count, reg_mem.begin());
				const float4* const end = result + ((m_particles_count + 3) >> 2);

				if(arg1_type == Compiler::DataStream::LITERAL) {
					const float4 arg1 = f4Splat(blob.read<float>());

					for (; result != end; ++result, ++arg0) {
						*result = f4Mul(*arg0, arg1);
					}
				}
				else {
					const u8 arg1_idx = blob.read<u8>();
					const float4* arg1 = getStream(*this, arg1_type, arg1_idx, m_particles_count, reg_mem.begin());
					for (; result != end; ++result, ++arg0, ++arg1) {
						*result = f4Mul(*arg0, *arg1);
					}
				}

				break;
			}
			case Instructions::MOV: {
				const Compiler::DataStream::Type dst_type = blob.read<Compiler::DataStream::Type>();
				const u8 dst_idx = blob.read<u8>();
				const Compiler::DataStream::Type src_type = blob.read<Compiler::DataStream::Type>();
				const u8 src_idx = blob.read<u8>();
				
				float4* result = getStream(*this, dst_type, dst_idx, m_particles_count, reg_mem.begin());
				const float4* const end = result + ((m_particles_count + 3) >> 2);
				
				if(src_type == Compiler::DataStream::CONST) {
					ASSERT(src_idx == 0);
					const float4 src = f4Splat(dt);
					for (; result != end; ++result) {
						*result = src;
					}
				}
				else {
					const float4* src = getStream(*this, src_type, src_idx, m_particles_count, reg_mem.begin());

					for (; result != end; ++result, ++src) {
						*result = *src;
					}
				}

				break;
			}
			case Instructions::ADD: {
				const Compiler::DataStream::Type dst_type = blob.read<Compiler::DataStream::Type>();
				const u8 dst_idx = blob.read<u8>();
				const Compiler::DataStream::Type arg0_type = blob.read<Compiler::DataStream::Type>();
				const u8 arg0_idx = blob.read<u8>();
				const Compiler::DataStream::Type arg1_type = blob.read<Compiler::DataStream::Type>();
				const u8 arg1_idx = blob.read<u8>();
				
				// TODO
				float4* result = getStream(*this, dst_type, dst_idx, m_particles_count, reg_mem.begin());
				const float4* const end = result + ((m_particles_count + 3) >> 2);
				const float4* arg0 = getStream(*this, arg0_type, arg0_idx, m_particles_count, reg_mem.begin());

				if(arg1_type == Compiler::DataStream::CONST) { 
					ASSERT(arg1_idx == 0);
					const float4 arg1 = f4Splat(dt);

					for (; result != end; ++result, ++arg0) {
						*result = f4Add(*arg0, arg1);
					}
				}
				else {
					const float4* arg1 = getStream(*this, arg1_type, arg1_idx, m_particles_count, reg_mem.begin());

					for (; result != end; ++result, ++arg0, ++arg1) {
						*result = f4Add(*arg0, *arg1);
					}
				}

				break;
			}
			case Instructions::COS: {
				const Compiler::DataStream::Type dst_type = blob.read<Compiler::DataStream::Type>();
				const u8 dst_idx = blob.read<u8>();
				const Compiler::DataStream::Type arg_type = blob.read<Compiler::DataStream::Type>();
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
				const Compiler::DataStream::Type dst_type = blob.read<Compiler::DataStream::Type>();
				const u8 dst_idx = blob.read<u8>();
				const Compiler::DataStream::Type arg_type = blob.read<Compiler::DataStream::Type>();
				const u8 arg_idx = blob.read<u8>();
				
				const float* arg = (float*)getStream(*this, arg_type, arg_idx, m_particles_count, reg_mem.begin());
				float* result = (float*)getStream(*this, dst_type, dst_idx, m_particles_count, reg_mem.begin());
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
	const OutputMemoryStream& bytecode = m_resource->getBytecode();
	const int output_offset = m_resource->getOutputByteOffset();
	
	// TODO
	m_constants[1].value = (float)cam_pos.x;
	m_constants[2].value = (float)cam_pos.y;
	m_constants[3].value = (float)cam_pos.z;
	// TODO
	Array<float4> reg_mem(m_allocator);
	reg_mem.resize(m_resource->getRegistersCount() * ((m_particles_count + 3) >> 2));

	auto sim = [&](u32 offset, u32 count){
		PROFILE_BLOCK("particle simulation");
		InputMemoryStream blob((u8*)bytecode.getData() + output_offset, bytecode.getPos());
		int output_idx = 0;
		for (;;) {
			u8 instruction = blob.read<u8>();
			switch ((Instructions)instruction) {
				case Instructions::END:
					return;
				case Instructions::SIN: {
					const Compiler::DataStream::Type dst_type = blob.read<Compiler::DataStream::Type>();
					const u8 dst_idx = blob.read<u8>();
					const Compiler::DataStream::Type arg_type = blob.read<Compiler::DataStream::Type>();
					const u8 arg_idx = blob.read<u8>();
				
					const float* arg = (float*)getStream(*this, arg_type, arg_idx, m_particles_count, reg_mem.begin()) + offset;
					float* result = (float*)getStream(*this, dst_type, dst_idx, m_particles_count, reg_mem.begin()) + offset;
					const float* const end = result + count;

					for (; result != end; ++result, ++arg) {
						*result = sinf(*arg);
					}
					break;
				}
				case Instructions::COS: {
					const Compiler::DataStream::Type dst_type = blob.read<Compiler::DataStream::Type>();
					const u8 dst_idx = blob.read<u8>();
					const Compiler::DataStream::Type arg_type = blob.read<Compiler::DataStream::Type>();
					const u8 arg_idx = blob.read<u8>();
				
					const float* arg = (float*)getStream(*this, arg_type, arg_idx, m_particles_count, reg_mem.begin()) + offset;
					float* result = (float*)getStream(*this, dst_type, dst_idx, m_particles_count, reg_mem.begin()) + offset;
					const float* const end = result + count;

					for (; result != end; ++result, ++arg) {
						*result = cosf(*arg);
					}
					break;
				}
				case Instructions::MUL: {
					const Compiler::DataStream::Type dst_type = blob.read<Compiler::DataStream::Type>();
					const u8 dst_idx = blob.read<u8>();
					const Compiler::DataStream::Type arg0_type = blob.read<Compiler::DataStream::Type>();
					const u8 arg0_idx = blob.read<u8>();
					const Compiler::DataStream::Type arg1_type = blob.read<Compiler::DataStream::Type>();
				
					float4* result = getStream(*this, dst_type, dst_idx, m_particles_count, reg_mem.begin()) + (offset >> 2);
					const float4* arg0 = getStream(*this, arg0_type, arg0_idx, m_particles_count, reg_mem.begin()) + (offset >> 2);
					const float4* const end = result + (count >> 2);
					if(arg1_type == Compiler::DataStream::LITERAL) {
						const float4 arg1 = f4Splat(blob.read<float>());

						for (; result != end; ++result, ++arg0) {
							*result = f4Mul(*arg0, arg1);
						}
					}
					else {
						const u8 arg1_idx = blob.read<u8>();
						const float4* arg1 = getStream(*this, arg1_type, arg1_idx, m_particles_count, reg_mem.begin()) + (offset >> 2);

						for (; result != end; ++result, ++arg0, ++arg1) {
							*result = f4Mul(*arg0, *arg1);
						}
					}

					break;
				}
				case Instructions::OUTPUT: {
					const int stride = m_resource->getOutputsCount();
					const Compiler::DataStream::Type arg_type = blob.read<Compiler::DataStream::Type>();
					const u8 arg_idx = blob.read<u8>();
					const float* arg = (float*)getStream(*this, arg_type, arg_idx, m_particles_count, reg_mem.begin()) + offset;
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
		}
	};
	if(m_particles_count > 16 * 1024) {
		volatile i32 counter = 0;
		JobSystem::runOnWorkers([&](){
			for(;;) {
				const i32 i = MT::atomicAdd(&counter, 16 * 1024);
				if (i >= m_particles_count) return;
				sim((u32)i, minimum((m_particles_count - i + 3) & ~3, 16 * 1024));
			}
		});
	}
	else {
		sim(0, (m_particles_count + 3) & ~3);
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

	
	InputMemoryStream blob(&m_bytecode[0] + m_output_bytecode_offset, m_bytecode.size() - m_output_bytecode_offset);

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