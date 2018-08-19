#include "particle_system.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/math_utils.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
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
	ADD_CONST,
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


ScriptedParticleEmitter::ScriptedParticleEmitter(EntityPtr entity, IAllocator& allocator)
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
	for (int i = 0; i < m_channels_count; ++i)
	{
		m_allocator.deallocate_aligned(m_channels[i].data);
	}
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
			case Instructions::ADD:
			{
				ASSERT((flag & 3) == (u8)InstructionArgType::CHANNEL);
				ASSERT(((flag >> 2) & 3) == (u8)InstructionArgType::CHANNEL);
				ASSERT(((flag >> 4) & 3) == (u8)InstructionArgType::REGISTER);
				u8 result_ch = blob.read<u8>();
				u8 op1_ch = blob.read<u8>();
				u8 arg_idx = blob.read<u8>();
				m_channels[result_ch].data[m_particles_count] = m_channels[op1_ch].data[m_particles_count] + args[arg_idx];
				break;
			}
			case Instructions::MOV:
			{
				ASSERT((flag & 3) == (u8)InstructionArgType::CHANNEL);
				ASSERT(((flag >> 2) & 3) == (u8)InstructionArgType::LITERAL);
				u8 ch = blob.read<u8>();
				float value = blob.read<float>();
				m_channels[ch].data[m_particles_count] = value;
				break;
			}
			case Instructions::RAND:
			{
				ASSERT((flag & 3) == (u8)InstructionArgType::CHANNEL);
				ASSERT(((flag >> 2) & 3) == (u8)InstructionArgType::LITERAL);
				ASSERT(((flag >> 4) & 3) == (u8)InstructionArgType::LITERAL);
				u8 ch = blob.read<u8>();
				float from = blob.read<float>();
				float to = blob.read<float>();
				m_channels[ch].data[m_particles_count] = Math::randFloat(from, to);
				break;
			}
			default:
				ASSERT(false);
				break;
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
	auto material_manager = manager.get(Material::TYPE);
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
}

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
				else if ((tmp[0] >= '0' && tmp[0] <= '9') || tmp[0] == '-')
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
				ASSERT(((flag >> 0) & 3) == (u8)InstructionArgType::CHANNEL);
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
				ASSERT(((flag >> 0) & 3) == (u8)InstructionArgType::CHANNEL);
				ASSERT(((flag >> 2) & 3) == (u8)InstructionArgType::CHANNEL);
				ASSERT(((flag >> 4) & 3) == (u8)InstructionArgType::CONSTANT);
				ASSERT(((flag >> 6) & 3) == (u8)InstructionArgType::CHANNEL);
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
				ASSERT(((flag >> 0) & 3) == (u8)InstructionArgType::CHANNEL);
				ASSERT(((flag >> 2) & 3) == (u8)InstructionArgType::CHANNEL);
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
				ASSERT(((flag >> 0) & 3) == (u8)InstructionArgType::CHANNEL);
				ASSERT(((flag >> 2) & 3) == (u8)InstructionArgType::CHANNEL);
				ASSERT(((flag >> 4) & 3) == (u8)InstructionArgType::CONSTANT);
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

// TODO
/*
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
}*/


} // namespace Lumix