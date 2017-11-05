#include "particle_system.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/math_utils.h"
#include "engine/profiler.h"
#include "engine/properties.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/simd.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "renderer/material.h"
#include "renderer/render_scene.h"
#include "engine/universe/universe.h"
#include <cmath>


namespace Lumix
{


static const ResourceType MATERIAL_TYPE("material");


enum class Instructions : u8
{
	END,
	ADD,
	ADD_CONST,
	ADD_ARG,
	SUB,
	SUB_CONST,
	MULTIPLY_ADD,
	MULTIPLY_CONST_ADD,
	LT,
	OUTPUT,
	OUTPUT_CONST,
	MOV_CONST,
	MOV,
	RAND,
	KILL,
	EMIT
};


ScriptedParticleEmitter::ScriptedParticleEmitter(Entity entity, IAllocator& allocator)
	: m_allocator(allocator)
	, m_bytecode(allocator)
	, m_entity(entity)
	, m_emit_buffer(allocator)
{
	compile(
		"constants {"
		"	 gravity_times_td -0.16\n"
		"}\n"

		"channels {"
		"	pos_x\n"
		"	pos_y\n"
		"	pos_z\n"
		"	vel_x\n"
		"	vel_y\n"
		"	vel_z\n"
		"}\n"

		"emit {\n"
		"	mov pos_x 0\n"
		"	mov pos_y 0\n"
		"	mov pos_z 0\n"
		"	rand vel_x -10 10\n"
		"	mov vel_y 20\n"
		"	rand vel_z -10 10\n"
		"	add pos_x pos_x $0\n"
		"	add pos_y pos_y $1\n"
		"	add pos_z pos_z $2\n"
		"}\n"

		"update {\n"
		"	add vel_y vel_y gravity_times_td\n"
		"	madd pos_x vel_x time_delta pos_x\n"
		"	madd pos_y vel_y time_delta pos_y\n"
		"	madd pos_z vel_z time_delta pos_z\n"
		"	lt pos_y {\n"
		"		doemit { pos_x pos_y pos_z }\n"
		"		doemit { pos_x pos_y pos_z }\n"
		"		doemit { pos_x pos_y pos_z }\n"
		"		doemit { pos_x pos_y pos_z }\n"
		"		doemit { pos_x pos_y pos_z }\n"
		"		dokill\n"
		"	}\n"
		"}\n"

		"output {"
		"	pos_x\n"
		"	pos_y\n"
		"	pos_z\n"
		"	1.0\n"
		"	1.0\n"
		"	0.0\n"
		"}\n"
	);
	
	float tmp[] = { 0, 0, 0 };
	for(int i = 0; i < 10; ++i)
		emit(tmp);
}


ScriptedParticleEmitter::~ScriptedParticleEmitter()
{
	// TODO
}


void ScriptedParticleEmitter::setMaterial(Material* material)
{
	if (m_material)
	{
		m_material->getResourceManager().unload(*m_material);
	}
	m_material = material;
}


void ScriptedParticleEmitter::emit(const float* args)
{
	if (m_particles_count == m_capacity)
	{
		int new_capacity = Math::maximum(16, m_capacity << 1);
		for (int i = 0; i < m_channels_count; ++i)
		{
			m_channels[i].data = (float*)m_allocator.reallocate_aligned(m_channels[i].data, new_capacity * sizeof(float), 16);
		}
		m_capacity = new_capacity;
	}

	InputBlob blob(&m_bytecode[m_emit_bytecode_offset], m_bytecode.size() - m_emit_bytecode_offset);
	for (;;)
	{
		u8 instruction = blob.read<u8>();
		u8 flag = blob.read<u8>();
		switch((Instructions)instruction)
		{
			case Instructions::END:
				++m_particles_count;
				return;
			case Instructions::ADD_ARG:
			{
				u8 result_ch = blob.read<u8>();
				u8 op1_ch = blob.read<u8>();
				u8 arg_idx = blob.read<u8>();
				m_channels[result_ch].data[m_particles_count] = m_channels[op1_ch].data[m_particles_count] + args[arg_idx];
				break;
			}
			case Instructions::MOV:
			{
				u8 ch = blob.read<u8>();
				float value = blob.read<float>();
				m_channels[ch].data[m_particles_count] = value;
				break;
			}
			case Instructions::RAND:
			{
				u8 ch = blob.read<u8>();
				float from = blob.read<float>();
				float to = blob.read<float>();
				m_channels[ch].data[m_particles_count] = Math::randFloat(from, to);
				break;
			}
		}
	}
}

static bool isWhitespace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }


int ScriptedParticleEmitter::getChannel(const char* name) const
{
	u32 hash = crc32(name);
	for (int i = 0; i < m_channels_count; ++i)
	{
		if (m_channels[i].name == hash) return i;
	}
	return -1;
}


int ScriptedParticleEmitter::getConstant(const char* name) const
{
	u32 hash = crc32(name);
	for (int i = 0; i < m_constants_count; ++i)
	{
		if (m_constants[i].name == hash) return i;
	}
	return -1;
}


void ScriptedParticleEmitter::serialize(OutputBlob& blob)
{
	blob.write(m_entity);
	blob.writeString(m_material ? m_material->getPath().c_str() : "");
}


void ScriptedParticleEmitter::deserialize(InputBlob& blob, ResourceManager& manager)
{
	blob.read(m_entity);
	char path[MAX_PATH_LENGTH];
	blob.readString(path, lengthOf(path));
	auto material_manager = manager.get(MATERIAL_TYPE);
	auto material = static_cast<Material*>(material_manager->load(Path(path)));
	setMaterial(material);
}


struct ParseContext
{
	OutputBlob* current_blob;
	OutputBlob* output_blob;
	OutputBlob* update_blob;
	OutputBlob* emit_blob;
	const char* in;
};


static void getWord(ParseContext& ctx, char(&out)[32])
{
	while (*ctx.in && isWhitespace(*ctx.in)) ++ctx.in;
	char* o = out;
	while (*ctx.in && !isWhitespace(*ctx.in) && o - out < lengthOf(out) - 1)
	{
		*o = *ctx.in;
		++o;
		++ctx.in;
	}
	*o = '\0';
};

struct {
	const char* instruction;
	Instructions opcode;
	int params_count;
	bool is_compound;
	bool is_variadic;
} INSTRUCTIONS[] = {
	{ "madd", Instructions::MULTIPLY_ADD, 4, false, false },
	{ "add", Instructions::ADD, 3, false, false },
	{ "sub", Instructions::SUB, 3, false, false },
	{ "dokill", Instructions::KILL, 0, false, false },
	{ "mov", Instructions::MOV, 2, false, false },
	{ "rand", Instructions::RAND, 3, false, false },
	{ "lt", Instructions::LT, 1, true, false },
	{ "doemit", Instructions::EMIT, 0, false, true }
};

enum class InstructionArgType : u8
{
	CHANNEL,
	CONSTANT,
	REGISTER,
	LITERAL
};

void ScriptedParticleEmitter::parseInstruction(const char* instruction, ParseContext& ctx)
{
	for (const auto& instr : INSTRUCTIONS)
	{
		ASSERT(instr.params_count <= 4); // flags are big enough only for 4 params
		if (equalIStrings(instruction, instr.instruction))
		{
			ctx.current_blob->write(instr.opcode);
			int flags_offset = ctx.current_blob->getPos();
			ctx.current_blob->write((u8)0);
			u8 flags = 0;
			char tmp[32];
			for (int i = 0; i < instr.params_count; ++i)
			{
				getWord(ctx, tmp);
				int ch = getChannel(tmp);
				int constant = getConstant(tmp);
				if (ch >= 0)
				{
					flags |= ((u8)InstructionArgType::CHANNEL) << (i * 2);
					ctx.current_blob->write((u8)ch);
				}
				else if (constant >= 0)
				{
					flags |= ((u8)InstructionArgType::CONSTANT) << (i * 2);
					ctx.current_blob->write((u8)constant);
				}
				else if (tmp[0] == '$')
				{
					flags |= ((u8)InstructionArgType::REGISTER) << (i * 2);
					ctx.current_blob->write(tmp[1] - '0');
				}
				else if (tmp[0] >= '0' && tmp[0] <= '9' || tmp[0] == '-')
				{
					flags |= ((u8)InstructionArgType::LITERAL) << (i * 2);
					float f = (float)atof(tmp);
					ctx.current_blob->write(f);
				}
			}
			*((u8*)ctx.current_blob->getMutableData() + flags_offset) = flags;
			if (instr.is_compound)
			{
				int size_pos = ctx.current_blob->getPos();
				ctx.current_blob->write((u8)0);
				getWord(ctx, tmp);
				ASSERT(equalStrings(tmp, "{"));
				getWord(ctx, tmp);
				while (!equalStrings(tmp, "}"))
				{
					parseInstruction(tmp, ctx);
					getWord(ctx, tmp);
				}
				ctx.current_blob->write(Instructions::END);
				ctx.current_blob->write((u8)0);
				*((u8*)ctx.current_blob->getMutableData() + size_pos) = u8(ctx.current_blob->getPos() - size_pos - 1);
			}
			if (instr.is_variadic)
			{
				int count_pos = ctx.current_blob->getPos();
				ctx.current_blob->write((u8)0);
				getWord(ctx, tmp);
				ASSERT(equalStrings(tmp, "{"));
				getWord(ctx, tmp);
				int count = 0;
				while (!equalStrings(tmp, "}"))
				{
					ctx.current_blob->write((u8)getChannel(tmp));
					++count;
					getWord(ctx, tmp);
				}
				*((u8*)ctx.current_blob->getMutableData() + count_pos) = count;
			}
			return;
		}
	}
	/*
	char tmp[32];
	if (equalStrings(instruction, "madd"))
	{
		char result[32];
		getWord(ctx, result);
		char m1[32];
		getWord(ctx, m1);
		char m2[32];
		getWord(ctx, m2);
		char add[32];
		getWord(ctx, add);
		int m2_ch = getChannel(m2);
		ctx.current_blob->write(m2_ch >= 0 ? Instructions::MULTIPLY_ADD : Instructions::MULTIPLY_CONST_ADD);
		ctx.current_blob->write((u8)getChannel(result));
		ctx.current_blob->write((u8)getChannel(m1));
		if (m2_ch < 0)
		{
			int constant_idx = getConstant(m2);
			ctx.current_blob->write((u8)constant_idx);
		}
		else
		{
			ctx.current_blob->write((u8)m2_ch);
		}
		ctx.current_blob->write((u8)getChannel(add));
	}
	else if (equalStrings(instruction, "add"))
	{
		char result[32];
		getWord(ctx, result);
		char m1[32];
		getWord(ctx, m1);
		char m2[32];
		getWord(ctx, m2);
		int m2_ch = getChannel(m2);
		ctx.current_blob->write(m2[0] == '$' ? Instructions::ADD_ARG : (m2_ch >= 0 ? Instructions::ADD : Instructions::ADD_CONST));
		ctx.current_blob->write((u8)getChannel(result));
		ctx.current_blob->write((u8)getChannel(m1));
		if (m2[0] == '$')
		{
			ctx.current_blob->write(u8(m2[1] - '0'));
		}
		else if (m2_ch < 0)
		{
			int constant_idx = getConstant(m2);
			ctx.current_blob->write((u8)constant_idx);
		}
		else
		{
			ctx.current_blob->write((u8)m2_ch);
		}
	}
	else if (equalStrings(instruction, "sub"))
	{
		char result[32];
		getWord(ctx, result);
		char m1[32];
		getWord(ctx, m1);
		char m2[32];
		getWord(ctx, m2);
		int m2_ch = getChannel(m2);
		ctx.current_blob->write(m2_ch >= 0 ? Instructions::SUB : Instructions::SUB_CONST);
		ctx.current_blob->write((u8)getChannel(result));
		ctx.current_blob->write((u8)getChannel(m1));
		if (m2_ch < 0)
		{
			int constant_idx = getConstant(m2);
			ctx.current_blob->write((u8)constant_idx);
		}
		else
		{
			ctx.current_blob->write((u8)m2_ch);
		}
	}
	else if (equalStrings(instruction, "dokill"))
	{
		ctx.current_blob->write(Instructions::KILL);
	}
	else if (equalStrings(instruction, "doemit"))
	{
		ctx.current_blob->write(Instructions::EMIT);
		int count_pos = ctx.current_blob->getPos();
		ctx.current_blob->write((u8)0);
		getWord(ctx, tmp);
		ASSERT(equalStrings(tmp, "{"));
		getWord(ctx, tmp);
		int count = 0;
		while (!equalStrings(tmp, "}"))
		{
			ctx.current_blob->write((u8)getChannel(tmp));
			++count;
			getWord(ctx, tmp);
		}
		*((u8*)ctx.current_blob->getMutableData() + count_pos) = count;
	}
	else if (equalStrings(instruction, "lt"))
	{
		char op1[32];
		getWord(ctx, op1);
		ctx.current_blob->write(Instructions::LT);
		ctx.current_blob->write((u8)getChannel(op1));
		int size_pos = ctx.current_blob->getPos();
		ctx.current_blob->write((u8)0);
		getWord(ctx, tmp);
		ASSERT(equalStrings(tmp, "{"));
		getWord(ctx, tmp);
		while (!equalStrings(tmp, "}"))
		{
			parseInstruction(tmp, ctx);
			getWord(ctx, tmp);
		}
		
		ctx.current_blob->write(Instructions::END);
		*((u8*)ctx.current_blob->getMutableData() + size_pos) = u8(ctx.current_blob->getPos() - size_pos - 1);
	}
	else if (equalStrings(instruction, "mov"))
	{
		ASSERT(ctx.current_blob == ctx.emit_blob); // not yet done for others
		char dst[32];
		char src[32];
		getWord(ctx, dst);
		getWord(ctx, src);
		ctx.current_blob->write(Instructions::MOV_CONST);
		ctx.current_blob->write((u8)getChannel(dst));
		float val = (float)atof(src);
		ctx.current_blob->write(val);
	}
	else if (equalStrings(instruction, "rand"))
	{
		ASSERT(ctx.current_blob == ctx.emit_blob); // not yet done for others
		char dst[32];
		char from[32];
		char to[32];
		getWord(ctx, dst);
		getWord(ctx, from);
		getWord(ctx, to);
		ctx.current_blob->write(Instructions::RAND);
		ctx.current_blob->write((u8)getChannel(dst));
		ctx.current_blob->write((float)atof(from));
		ctx.current_blob->write((float)atof(to));
	}*/
}


void ScriptedParticleEmitter::compile(const char* code)
{
	m_constants_count = 1;
	m_constants[0].name = crc32("time_delta");

	OutputBlob update_blob(m_allocator);
	OutputBlob output_blob(m_allocator);
	OutputBlob emit_blob(m_allocator);

	ParseContext ctx;
	ctx.in = code;
	ctx.update_blob = &update_blob;
	ctx.output_blob = &output_blob;
	ctx.emit_blob = &emit_blob;

	while (*ctx.in)
	{
		char instruction[32];
		getWord(ctx, instruction);
		if (equalStrings(instruction, "constants"))
		{
			char tmp[32];
			getWord(ctx, tmp);
			ASSERT(equalStrings(tmp, "{"));

			getWord(ctx, tmp);
			while (!equalStrings(tmp, "}"))
			{
				m_constants[m_constants_count].name = crc32(tmp);
				getWord(ctx, tmp);
				m_constants[m_constants_count].value = (float)atof(tmp);
				++m_constants_count;
				getWord(ctx, tmp);
			}
		}
		else if (equalStrings(instruction, "channels"))
		{
			char tmp[32];
			getWord(ctx, tmp);
			ASSERT(equalStrings(tmp, "{"));

			getWord(ctx, tmp);
			while (!equalStrings(tmp, "}"))
			{
				m_channels[m_channels_count].name = crc32(tmp);
				++m_channels_count;
				getWord(ctx, tmp);
			}
		}
		else if (equalStrings(instruction, "output"))
		{
			char tmp[32];
			getWord(ctx, tmp);
			ASSERT(equalStrings(tmp, "{"));

			getWord(ctx, tmp);
			while (!equalStrings(tmp, "}"))
			{
				int idx = getChannel(tmp);
				++m_outputs_per_particle;
				if (idx < 0)
				{
					float constant = (float)atof(tmp);
					ctx.output_blob->write(Instructions::OUTPUT_CONST);
					ctx.output_blob->write(constant);
				}
				else
				{
					ctx.output_blob->write(Instructions::OUTPUT);
					ctx.output_blob->write((u8)idx);
				}
				getWord(ctx, tmp);
			}
		}
		else if (equalStrings(instruction, "update"))
		{
			ctx.current_blob = ctx.update_blob;
			getWord(ctx, instruction);
			ASSERT(equalStrings(instruction, "{"));

			getWord(ctx, instruction);
			while (!equalStrings(instruction, "}"))
			{
				parseInstruction(instruction, ctx);
				getWord(ctx, instruction);
			}
		}
		else if (equalStrings(instruction, "emit"))
		{
			ctx.current_blob = ctx.emit_blob;

			getWord(ctx, instruction);
			ASSERT(equalStrings(instruction, "{"));

			getWord(ctx, instruction);
			while (!equalStrings(instruction, "}"))
			{
				parseInstruction(instruction, ctx);
				getWord(ctx, instruction);
			}
		}
	}
	
	update_blob.write(Instructions::END);
	update_blob.write((u8)0);
	output_blob.write(Instructions::END);
	output_blob.write((u8)0);
	emit_blob.write(Instructions::END);
	emit_blob.write((u8)0);

	m_bytecode.resize(update_blob.getPos() + output_blob.getPos() + emit_blob.getPos());
	m_output_bytecode_offset = update_blob.getPos();
	m_emit_bytecode_offset = m_output_bytecode_offset + output_blob.getPos();
	copyMemory(&m_bytecode[0], ctx.update_blob->getData(), ctx.update_blob->getPos());
	copyMemory(&m_bytecode[m_output_bytecode_offset], output_blob.getData(), output_blob.getPos());
	copyMemory(&m_bytecode[m_emit_bytecode_offset], emit_blob.getData(), emit_blob.getPos());
}


void ScriptedParticleEmitter::kill(int particle_index)
{
	if (particle_index >= m_particles_count) return;
	int last = m_particles_count - 1;
	for (int i = 0; i < m_channels_count; ++i)
	{
		float* data = m_channels[i].data;
		data[particle_index] = data[last];
	}
	--m_particles_count;
}


void ScriptedParticleEmitter::execute(InputBlob& blob, int particle_index)
{
	if (particle_index >= m_particles_count) return;
	for (;;)
	{
		u8 instruction = blob.read<u8>();
		u8 flag = blob.read<u8>();

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
		}
	}
}


void ScriptedParticleEmitter::update(float dt)
{
	PROFILE_FUNCTION();
	PROFILE_INT("particle count", m_particles_count);
	if (m_particles_count == 0) return;

	m_emit_buffer.clear();
	m_constants[0].value = dt;
	InputBlob blob(&m_bytecode[0], m_bytecode.size());

	for (;;)
	{
		u8 instruction = blob.read<u8>();
		u8 flag = blob.read<u8>();
		switch ((Instructions)instruction)
		{
			case Instructions::END:
				goto end;
			case Instructions::LT:
			{
				u8 ch = blob.read<u8>();
				u8 size = blob.read<u8>();
				const float4* iter = (float4*)m_channels[ch].data;
				const float4* end = iter + ((m_particles_count + 3) >> 2);
				InputBlob subblob((u8*)blob.getData() + blob.getPosition(), blob.getSize() - blob.getPosition());
				for (; iter != end; ++iter)
				{
					int m = f4MoveMask(*iter);
					if (m) 
					{
						for (int i = 0; i < 4; ++i)
						{
							float f = *((float*)iter + i);
							if (f < 0)
							{
								subblob.rewind();
								execute(subblob, int(((float*)iter - m_channels[ch].data) + i));
							}
						}
					}
				}
				blob.skip(size);
				break;
			}
			case Instructions::MULTIPLY_ADD:
			{
				u8 result_channel_idx = blob.read<u8>();
				u8 multiply_channel_idx = blob.read<u8>();
				u8 constant_index = blob.read<u8>();
				u8 add_channel_idx = blob.read<u8>();

				float4* result = (float4*)m_channels[result_channel_idx].data;
				const float4* end = result + ((m_particles_count + 3) >> 2);
				const float4* multiply = (float4*)m_channels[multiply_channel_idx].data;
				const float4* add = (float4*)m_channels[add_channel_idx].data;
				const float4 constant = f4Splat(m_constants[constant_index].value);
				for (; result != end; ++result, ++multiply, ++add)
				{
					float4 r = f4Mul(*multiply, constant);
					*result = f4Add(r, *add);
				}
				break;
			}
			case Instructions::ADD:
			{
				u8 result_channel_idx = blob.read<u8>();
				u8 op1_index = blob.read<u8>();

				float4* result = (float4*)m_channels[result_channel_idx].data;
				const float4* end = result + ((m_particles_count + 3) >> 2);
				const float4* op1 = (float4*)m_channels[op1_index].data;
				u8 op2_index = blob.read<u8>();
				if (((flag >> 4) & 0x3) == (u8)InstructionArgType::CONSTANT)
				{
					const float4 constant = f4Splat(m_constants[op2_index].value);
					for (; result != end; ++result, ++op1)
					{
						*result = f4Add(*op1, constant);
					}
				}
				else
				{
					const float4* op2 = (float4*)m_channels[op2_index].data;
					for (; result != end; ++result, ++op1, ++op2)
					{
						*result = f4Add(*op1, *op2);
					}
				}
				break;
			}
			case Instructions::SUB:
			{
				u8 result_channel_idx = blob.read<u8>();
				u8 op1_channel_idx = blob.read<u8>();
				u8 constant_index = blob.read<u8>();

				float4* result = (float4*)m_channels[result_channel_idx].data;
				const float4* end = result + ((m_particles_count + 3) >> 2);
				const float4* multiply = (float4*)m_channels[op1_channel_idx].data;
				const float4 constant = f4Splat(m_constants[constant_index].value);
				for (; result != end; ++result, ++multiply)
				{
					*result = f4Sub(*multiply, constant);
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


bgfx::InstanceDataBuffer ScriptedParticleEmitter::generateInstanceBuffer() const
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
	ASSERT(false);
	return buffer;
}


template <typename T>
static ParticleEmitter::ModuleBase* create(ParticleEmitter& emitter)
{
	return LUMIX_NEW(emitter.getAllocator(), T)(emitter);
}


static ParticleEmitter::ModuleBase* createModule(ComponentType type, ParticleEmitter& emitter)
{
	typedef ParticleEmitter::ModuleBase* (*Creator)(ParticleEmitter& emitter);
	static const struct { ComponentType type; Creator creator; } creators[] = {
		{ ParticleEmitter::ForceModule::s_type, create<ParticleEmitter::ForceModule> },
		{ ParticleEmitter::PlaneModule::s_type, create<ParticleEmitter::PlaneModule> },
		{ ParticleEmitter::LinearMovementModule::s_type, create<ParticleEmitter::LinearMovementModule> },
		{ ParticleEmitter::AlphaModule::s_type, create<ParticleEmitter::AlphaModule> },
		{ ParticleEmitter::RandomRotationModule::s_type, create<ParticleEmitter::RandomRotationModule> },
		{ ParticleEmitter::SizeModule::s_type, create<ParticleEmitter::SizeModule> },
		{ ParticleEmitter::AttractorModule::s_type, create<ParticleEmitter::AttractorModule> },
		{ ParticleEmitter::SpawnShapeModule::s_type, create<ParticleEmitter::SpawnShapeModule> },
		{ ParticleEmitter::SubimageModule::s_type, create<ParticleEmitter::SubimageModule> }
	};

	for(auto& i : creators)
	{
		if(i.type == type)
		{
			return i.creator(emitter);
		}
	}

	return nullptr;
}


ParticleEmitter::ModuleBase::ModuleBase(ParticleEmitter& emitter)
	: m_emitter(emitter)
{
}


ParticleEmitter::SubimageModule::SubimageModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
	, rows(1)
	, cols(1)
{
}


void ParticleEmitter::SubimageModule::serialize(OutputBlob& blob)
{
	blob.write(rows);
	blob.write(cols);
}


void ParticleEmitter::SubimageModule::deserialize(InputBlob& blob)
{
	blob.read(rows);
	blob.read(cols);
}


const ComponentType ParticleEmitter::SubimageModule::s_type = Properties::getComponentType("particle_emitter_subimage");


ParticleEmitter::ForceModule::ForceModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
{
	m_acceleration.set(0, 0, 0);
}


void ParticleEmitter::ForceModule::serialize(OutputBlob& blob)
{
	blob.write(m_acceleration);
}


void ParticleEmitter::ForceModule::deserialize(InputBlob& blob)
{
	blob.read(m_acceleration);
}


void ParticleEmitter::ForceModule::update(float time_delta)
{
	if (m_emitter.m_velocity.empty()) return;

	Vec3* LUMIX_RESTRICT particle_velocity = &m_emitter.m_velocity[0];
	for (int i = 0, c = m_emitter.m_velocity.size(); i < c; ++i)
	{
		particle_velocity[i] += m_acceleration * time_delta;
	}
}


const ComponentType ParticleEmitter::ForceModule::s_type = Properties::getComponentType("particle_emitter_force");


void ParticleEmitter::drawGizmo(WorldEditor& editor, RenderScene& scene)
{
	for (auto* module : m_modules)
	{
		module->drawGizmo(editor, scene);
	}
}


ParticleEmitter::AttractorModule::AttractorModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
	, m_force(0)
{
	m_count = 0;
	for(auto& e : m_entities)
	{
		e = INVALID_ENTITY;
	}
}


void ParticleEmitter::AttractorModule::drawGizmo(WorldEditor& editor, RenderScene& scene)
{
	for (int i = 0; i < m_count; ++i)
	{
		if(m_entities[i] != INVALID_ENTITY) editor.getGizmo().add(m_entities[i]);
	}
}


void ParticleEmitter::AttractorModule::update(float time_delta)
{
	if(m_emitter.m_alpha.empty()) return;

	Vec3* LUMIX_RESTRICT particle_pos = &m_emitter.m_position[0];
	Vec3* LUMIX_RESTRICT particle_vel = &m_emitter.m_velocity[0];

	for(int i = 0; i < m_count; ++i)
	{
		auto entity = m_entities[i];
		if(entity == INVALID_ENTITY) continue;
		if (!m_emitter.m_universe.hasEntity(entity)) continue;
		Vec3 pos = m_emitter.m_universe.getPosition(entity);

		for(int i = m_emitter.m_position.size() - 1; i >= 0; --i)
		{
			Vec3 to_center = pos - particle_pos[i];
			float dist2 = to_center.squaredLength();
			to_center *= 1 / sqrt(dist2);
			particle_vel[i] = particle_vel[i] + to_center * (m_force / dist2) * time_delta;
		}
	}
}


void ParticleEmitter::AttractorModule::serialize(OutputBlob& blob)
{
	blob.write(m_force);
	blob.write(m_count);
	for(int i = 0; i < m_count; ++i)
	{
		blob.write(m_entities[i]);
	}
}


void ParticleEmitter::AttractorModule::deserialize(InputBlob& blob)
{
	blob.read(m_force);
	blob.read(m_count);
	for(int i = 0; i < m_count; ++i)
	{
		blob.read(m_entities[i]);
	}
}


const ComponentType ParticleEmitter::AttractorModule::s_type =
	Properties::getComponentType("particle_emitter_attractor");


ParticleEmitter::PlaneModule::PlaneModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
	, m_bounce(0.5f)
{
	m_count = 0;
	for (auto& e : m_entities)
	{
		e = INVALID_ENTITY;
	}
}


void ParticleEmitter::PlaneModule::drawGizmo(WorldEditor& editor, RenderScene& scene)
{
	for (int i = 0; i < m_count; ++i)
	{
		Entity entity = m_entities[i];
		if (m_entities[i] != INVALID_ENTITY) editor.getGizmo().add(entity);
		if (entity == INVALID_ENTITY) continue;
		if (!m_emitter.m_universe.hasEntity(entity)) continue;

		Matrix mtx = m_emitter.m_universe.getMatrix(entity);
		Vec3 pos = mtx.getTranslation();
		Vec3 right = mtx.getXVector();
		Vec3 normal = mtx.getYVector();
		Vec3 forward = mtx.getZVector();
		u32 color = 0xffff0000;

		for (int i = 0; i < 9; ++i)
		{
			float w = i / 4.0f - 1.0f;
			scene.addDebugLine(pos - right - forward * w, pos + right - forward * w, color, 0);
			scene.addDebugLine(pos - right * w - forward, pos - right * w + forward, color, 0);
		}
	}
}


void ParticleEmitter::PlaneModule::update(float time_delta)
{
	if (m_emitter.m_alpha.empty()) return;

	Vec3* LUMIX_RESTRICT particle_pos = &m_emitter.m_position[0];
	Vec3* LUMIX_RESTRICT particle_vel = &m_emitter.m_velocity[0];

	for (int i = 0; i < m_count; ++i)
	{
		auto entity = m_entities[i];
		if (entity == INVALID_ENTITY) continue;
		if (!m_emitter.m_universe.hasEntity(entity)) continue;
		Vec3 normal = m_emitter.m_universe.getRotation(entity).rotate(Vec3(0, 1, 0));
		float D = -dotProduct(normal, m_emitter.m_universe.getPosition(entity));

		for (int i = m_emitter.m_position.size() - 1; i >= 0; --i)
		{
			const auto& pos = particle_pos[i];
			if (dotProduct(normal, pos) + D < 0)
			{
				float NdotV = dotProduct(normal, particle_vel[i]);
				particle_vel[i] = (particle_vel[i] - normal * (2 * NdotV)) * m_bounce;
			}
		}
	}
}


void ParticleEmitter::PlaneModule::serialize(OutputBlob& blob)
{
	blob.write(m_bounce);
	blob.write(m_count);
	for (int i = 0; i < m_count; ++i)
	{
		blob.write(m_entities[i]);
	}
}


void ParticleEmitter::PlaneModule::deserialize(InputBlob& blob)
{
	blob.read(m_bounce);
	blob.read(m_count);
	for (int i = 0; i < m_count; ++i)
	{
		blob.read(m_entities[i]);
	}
}


const ComponentType ParticleEmitter::PlaneModule::s_type = Properties::getComponentType("particle_emitter_plane");


ParticleEmitter::SpawnShapeModule::SpawnShapeModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
	, m_radius(1.0f)
	, m_shape(SPHERE)
{
}


void ParticleEmitter::SpawnShapeModule::spawnParticle(int index)
{
	// ugly and ~0.1% from uniform distribution, but still faster than the correct solution
	float r2 = m_radius * m_radius;
	for (int i = 0; i < 10; ++i)
	{
		Vec3 v(m_radius * Math::randFloat(-1, 1),
			m_radius * Math::randFloat(-1, 1),
			m_radius * Math::randFloat(-1, 1));

		if (v.squaredLength() < r2)
		{
			m_emitter.m_position[index] += v;
			return;
		}
	}
}


void ParticleEmitter::SpawnShapeModule::serialize(OutputBlob& blob)
{
	blob.write(m_shape);
	blob.write(m_radius);
}


void ParticleEmitter::SpawnShapeModule::deserialize(InputBlob& blob)
{
	blob.read(m_shape);
	blob.read(m_radius);
}


const ComponentType ParticleEmitter::SpawnShapeModule::s_type =
	Properties::getComponentType("particle_emitter_spawn_shape");


ParticleEmitter::LinearMovementModule::LinearMovementModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
{
}


void ParticleEmitter::LinearMovementModule::spawnParticle(int index)
{
	Vec3& velocity = m_emitter.m_velocity[index];
	velocity.x = m_x.getRandom();
	velocity.y = m_y.getRandom();
	velocity.z = m_z.getRandom();
	Quat rot = m_emitter.m_universe.getRotation(m_emitter.m_entity);
	velocity = rot.rotate(velocity);
}


void ParticleEmitter::LinearMovementModule::serialize(OutputBlob& blob)
{
	blob.write(m_x);
	blob.write(m_y);
	blob.write(m_z);
}


void ParticleEmitter::LinearMovementModule::deserialize(InputBlob& blob)
{
	blob.read(m_x);
	blob.read(m_y);
	blob.read(m_z);
}


const ComponentType ParticleEmitter::LinearMovementModule::s_type =
	Properties::getComponentType("particle_emitter_linear_movement");


static void sampleBezier(float max_life, const Array<Vec2>& values, Array<float>& sampled)
{
	ASSERT(values.size() >= 6);
	ASSERT(values[values.size() - 2].x - values[1].x > 0);

	static const int SAMPLES_PER_SECOND = 10;
	sampled.resize(int(max_life * SAMPLES_PER_SECOND));

	float x_range = values[values.size() - 2].x - values[1].x;
	int last_idx = 0;
	for (int i = 1; i < values.size() - 3; i += 3)
	{
		int step_count = int(5 * sampled.size() * ((values[i + 3].x - values[i].x) / x_range));
		float t_step = 1.0f / (float)step_count;
		for (int i_step = 1; i_step <= step_count; i_step++)
		{
			float t = t_step * i_step;
			float u = 1.0f - t;
			float w1 = u * u * u;
			float w2 = 3 * u * u * t;
			float w3 = 3 * u * t * t;
			float w4 = t * t * t;
			auto p = values[i] * (w1 + w2) + values[i + 1] * w2 + values[i + 2] * w3 +
					 values[i + 3] * (w3 + w4);
			int idx = int(sampled.size() * ((p.x - values[1].x) / x_range));
			ASSERT(idx <= last_idx + 1);
			last_idx = idx;
			sampled[idx >= sampled.size() ? sampled.size() - 1 : idx] = p.y;
		}
	}
	sampled[0] = values[0].y;
	sampled.back() = values[values.size() - 2].y;
}


ParticleEmitter::AlphaModule::AlphaModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
	, m_values(emitter.getAllocator())
	, m_sampled(emitter.getAllocator())
{
	m_values.resize(9);
	m_values[0].set(-0.2f, 0);
	m_values[1].set(0, 0);
	m_values[2].set(0.2f, 0.0f);
	m_values[3].set(-0.2f, 0.0f);
	m_values[4].set(0.5f, 1.0f);
	m_values[5].set(0.2f, 0.0f);
	m_values[6].set(-0.2f, 0.0f);
	m_values[7].set(1, 0);
	m_values[8].set(0.2f, 0);
	sample();
}


void ParticleEmitter::AlphaModule::serialize(OutputBlob& blob)
{
	blob.write(m_values.size());
	blob.write(&m_values[0], sizeof(m_values[0]) * m_values.size());
}


void ParticleEmitter::AlphaModule::deserialize(InputBlob& blob)
{
	int size;
	blob.read(size);
	m_values.resize(size);
	blob.read(&m_values[0], sizeof(m_values[0]) * m_values.size());
	sample();
}


void ParticleEmitter::AlphaModule::sample()
{
	sampleBezier(m_emitter.m_initial_life.to, m_values, m_sampled);
}


void ParticleEmitter::AlphaModule::update(float)
{
	if(m_emitter.m_alpha.empty()) return;

	float* LUMIX_RESTRICT particle_alpha = &m_emitter.m_alpha[0];
	float* LUMIX_RESTRICT rel_life = &m_emitter.m_rel_life[0];
	int size = m_sampled.size() - 1;
	float float_size = (float)size;
	for(int i = 0, c = m_emitter.m_size.size(); i < c; ++i)
	{
		float float_idx = float_size * rel_life[i];
		int idx = (int)float_idx;
		int next_idx = Math::minimum(idx + 1, size);
		float w = float_idx - idx;
		particle_alpha[i] = m_sampled[idx] * (1 - w) + m_sampled[next_idx] * w;
	}
}


const ComponentType ParticleEmitter::AlphaModule::s_type = Properties::getComponentType("particle_emitter_alpha");


ParticleEmitter::SizeModule::SizeModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
	, m_values(emitter.getAllocator())
	, m_sampled(emitter.getAllocator())
{
	m_values.resize(9);
	m_values[0].set(-0.2f, 0);
	m_values[1].set(0, 0);
	m_values[2].set(0.2f, 0.0f);
	m_values[3].set(-0.2f, 0.0f);
	m_values[4].set(0.5f, 1.0f);
	m_values[5].set(0.2f, 0.0f);
	m_values[6].set(-0.2f, 0.0f);
	m_values[7].set(1, 0);
	m_values[8].set(0.2f, 0);
	sample();
}


void ParticleEmitter::SizeModule::serialize(OutputBlob& blob)
{
	blob.write(m_values.size());
	blob.write(&m_values[0], sizeof(m_values[0]) * m_values.size());
}


void ParticleEmitter::SizeModule::deserialize(InputBlob& blob)
{
	int size;
	blob.read(size);
	m_values.resize(size);
	blob.read(&m_values[0], sizeof(m_values[0]) * m_values.size());
	sample();
}


void ParticleEmitter::SizeModule::sample()
{
	sampleBezier(m_emitter.m_initial_life.to, m_values, m_sampled);
}


void ParticleEmitter::SizeModule::update(float)
{
	if (m_emitter.m_size.empty()) return;

	float* LUMIX_RESTRICT particle_size = &m_emitter.m_size[0];
	float* LUMIX_RESTRICT rel_life = &m_emitter.m_rel_life[0];
	int size = m_sampled.size() - 1;
	float float_size = (float)size;
	for (int i = 0, c = m_emitter.m_size.size(); i < c; ++i)
	{
		float float_idx = float_size * rel_life[i];
		int idx = (int)float_idx;
		int next_idx = Math::minimum(idx + 1, size);
		float w = float_idx - idx;
		particle_size[i] = m_sampled[idx] * (1 - w) + m_sampled[next_idx] * w;
		PROFILE_INT("Test", int(particle_size[i] * 1000));
	}
}


const ComponentType ParticleEmitter::SizeModule::s_type = Properties::getComponentType("particle_emitter_size");


ParticleEmitter::RandomRotationModule::RandomRotationModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
{
}


void ParticleEmitter::RandomRotationModule::spawnParticle(int index)
{
	m_emitter.m_rotation[index] = Math::randFloat(0, Math::PI * 2);
}


const ComponentType ParticleEmitter::RandomRotationModule::s_type =
	Properties::getComponentType("particle_emitter_random_rotation");


Interval::Interval()
	: from(0)
	, to(0)
{
}


IntInterval::IntInterval()
{
	from = to = 1;
}


int IntInterval::getRandom() const
{
	if (from == to) return from;
	return Math::rand(from, to);
}


void Interval::checkZero()
{
	from = Math::maximum(from, 0.0f);
	to = Math::maximum(from, to);
}


void Interval::check()
{
	to = Math::maximum(from, to);
}


float Interval::getRandom() const
{
	return Math::randFloat(from, to);
}


ParticleEmitter::ParticleEmitter(Entity entity, Universe& universe, IAllocator& allocator)
	: m_allocator(allocator)
	, m_rel_life(allocator)
	, m_life(allocator)
	, m_modules(allocator)
	, m_position(allocator)
	, m_velocity(allocator)
	, m_rotation(allocator)
	, m_rotational_speed(allocator)
	, m_alpha(allocator)
	, m_universe(universe)
	, m_entity(entity)
	, m_size(allocator)
	, m_subimage_module(nullptr)
	, m_autoemit(true)
	, m_local_space(false)
{
	init();
}


ParticleEmitter::~ParticleEmitter()
{
	setMaterial(nullptr);

	for (auto* module : m_modules)
	{
		LUMIX_DELETE(m_allocator, module);
	}
}


void ParticleEmitter::init()
{
	m_spawn_period.from = 1;
	m_spawn_period.to = 2;
	m_initial_life.from = 1;
	m_initial_life.to = 2;
	m_initial_size.from = 1;
	m_initial_size.to = 1;
	m_material = nullptr;
	m_next_spawn_time = 0;
	m_is_valid = true;
}


void ParticleEmitter::reset()
{
	m_rel_life.clear();
	m_life.clear();
	m_size.clear();
	m_position.clear();
	m_velocity.clear();
	m_alpha.clear();
	m_rotation.clear();
	m_rotational_speed.clear();
}


void ParticleEmitter::setMaterial(Material* material)
{
	if (m_material)
	{
		m_material->getResourceManager().unload(*m_material);
	}
	m_material = material;
}


void ParticleEmitter::spawnParticle()
{
	if (m_local_space)
	{
		m_position.emplace(0.0f, 0.0f, 0.0f);
	}
	else
	{
		m_position.push(m_universe.getPosition(m_entity));
	}
	m_rotation.push(0);
	m_rotational_speed.push(0);
	m_life.push(m_initial_life.getRandom());
	m_rel_life.push(0.0f);
	m_alpha.push(1);
	m_velocity.push(Vec3(0, 0, 0));
	m_size.push(m_initial_size.getRandom());
	for (auto* module : m_modules)
	{
		module->spawnParticle(m_life.size() - 1);
	}
}


ParticleEmitter::ModuleBase* ParticleEmitter::getModule(ComponentType type)
{
	for (auto* module : m_modules)
	{
		if (module->getType() == type) return module;
	}
	return nullptr;
}


void ParticleEmitter::addModule(ModuleBase* module)
{
	if (module->getType() == SubimageModule::s_type) m_subimage_module = static_cast<SubimageModule*>(module);
	m_modules.push(module);
}


void ParticleEmitter::destroyParticle(int index)
{
	for (auto* module : m_modules)
	{
		module->destoryParticle(index);
	}
	m_life.eraseFast(index);
	m_rel_life.eraseFast(index);
	m_position.eraseFast(index);
	m_velocity.eraseFast(index);
	m_rotation.eraseFast(index);
	m_rotational_speed.eraseFast(index);
	m_alpha.eraseFast(index);
	m_size.eraseFast(index);
}


void ParticleEmitter::updateLives(float time_delta)
{
	for (int i = 0, c = m_rel_life.size(); i < c; ++i)
	{
		float rel_life = m_rel_life[i];
		rel_life += time_delta / m_life[i];
		m_rel_life[i] = rel_life;

		if (rel_life > 1)
		{
			destroyParticle(i);
			--i;
			--c;
		}
	}
}


void ParticleEmitter::serialize(OutputBlob& blob)
{
	blob.write(m_spawn_count);
	blob.write(m_spawn_period);
	blob.write(m_initial_life);
	blob.write(m_initial_size);
	blob.write(m_entity);
	blob.write(m_autoemit);
	blob.write(m_local_space);
	blob.writeString(m_material ? m_material->getPath().c_str() : "");
	blob.write(m_modules.size());
	for (auto* module : m_modules)
	{
		blob.write(Properties::getComponentTypeHash(module->getType()));
		module->serialize(blob);
	}
}


void ParticleEmitter::deserialize(InputBlob& blob, ResourceManager& manager)
{
	blob.read(m_spawn_count);
	blob.read(m_spawn_period);
	blob.read(m_initial_life);
	blob.read(m_initial_size);
	blob.read(m_entity);
	blob.read(m_autoemit);
	blob.read(m_local_space);
	char path[MAX_PATH_LENGTH];
	blob.readString(path, lengthOf(path));
	auto material_manager = manager.get(MATERIAL_TYPE);
	auto material = static_cast<Material*>(material_manager->load(Path(path)));
	setMaterial(material);

	int size;
	blob.read(size);
	for (auto* module : m_modules)
	{
		LUMIX_DELETE(m_allocator, module);
	}
	m_modules.clear();
	for (int i = 0; i < size; ++i)
	{
		ParticleEmitter::ModuleBase* module = nullptr;
		u32 hash;
		blob.read(hash);
		ComponentType type = Properties::getComponentTypeFromHash(hash);
		module = createModule(type, *this);
		if (module)
		{
			module->deserialize(blob);
			m_modules.push(module);
		}
	}
}


void ParticleEmitter::updatePositions(float time_delta)
{
	for (int i = 0, c = m_position.size(); i < c; ++i)
	{
		m_position[i] += m_velocity[i] * time_delta;
	}
}


void ParticleEmitter::updateRotations(float time_delta)
{
	for (int i = 0, c = m_rotation.size(); i < c; ++i)
	{
		m_rotation[i] += m_rotational_speed[i] * time_delta;
	}
}


void ParticleEmitter::update(float time_delta)
{
	spawnParticles(time_delta);
	updateLives(time_delta);
	updatePositions(time_delta);
	updateRotations(time_delta);
	for (auto* module : m_modules)
	{
		module->update(time_delta);
	}
}


void ParticleEmitter::emit()
{
	int spawn_count = m_spawn_count.getRandom();
	for (int i = 0; i < spawn_count; ++i)
	{
		spawnParticle();
	}
}


void ParticleEmitter::spawnParticles(float time_delta)
{
	if (!m_autoemit) return;
	m_next_spawn_time -= time_delta;

	while (m_next_spawn_time < 0)
	{
		m_next_spawn_time += m_spawn_period.getRandom();
		emit();
	}
}


} // namespace Lumix