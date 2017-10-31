#pragma once


#include "engine/array.h"
#include "engine/hash_map.h"
#include "engine/lumix.h"
#include <cstring>


namespace Lumix
{


class Animation;
struct IAllocator;
class OutputBlob;


namespace Anim
{


typedef HashMap<u32, Animation*> AnimSet;


struct RunningContext
{
	float time_delta;
	u8* input;
	IAllocator* allocator;
	struct ComponentInstance* current;
	struct Edge* edge;
	AnimSet* anim_set;
	OutputBlob* event_stream;
	ComponentHandle controller;
};



struct InputDecl
{
	enum Type : int
	{
		// don't change order
		FLOAT,
		INT,
		BOOL,
		EMPTY
	};

	struct Constant
	{
		Type type = EMPTY;
		union
		{
			float f_value;
			int i_value;
			bool b_value;
		};
		StaticString<32> name;
	};

	struct Input
	{
		Type type = EMPTY;
		int offset;
		StaticString<32> name;
	};

	Input inputs[32];
	int inputs_count = 0;
	Constant constants[32];
	int constants_count = 0;


	int inputFromLinearIdx(int idx) const
	{
		for (int i = 0; i < lengthOf(inputs); ++i)
		{
			if (inputs[i].type == EMPTY) continue;
			if (idx == 0) return i;
			--idx;
		}
		return -1;
	}


	int inputToLinearIdx(int idx) const
	{
		int linear = 0;
		for (int i = 0; i < lengthOf(inputs); ++i)
		{
			if (i == idx) return inputs[i].type == EMPTY ? -1 : linear;
			if (inputs[i].type == EMPTY) continue;
			++linear;
		}
		return -1;
	}


	void removeInput(int index)
	{
		inputs[index].type = EMPTY;
		--inputs_count;
	}


	void removeConstant(int index)
	{
		constants[index].type = EMPTY;
		--constants_count;
	}


	void moveConstant(int old_idx, int new_idx)
	{
		if (old_idx == new_idx) return;
		ASSERT(constants[new_idx].type == EMPTY);
		constants[new_idx] = constants[old_idx];
		constants[old_idx].type = EMPTY;
	}


	void moveInput(int old_idx, int new_idx)
	{
		if (old_idx == new_idx) return;
		ASSERT(inputs[new_idx].type == EMPTY);
		inputs[new_idx] = inputs[old_idx];
		inputs[old_idx].type = EMPTY;
		recalculateOffsets();
	}


	int addInput()
	{
		ASSERT(inputs_count < lengthOf(inputs));
		
		for (int i = 0; i < lengthOf(inputs); ++i)
		{
			if (inputs[i].type == EMPTY)
			{
				inputs[i].name = "";
				inputs[i].type = BOOL;
				++inputs_count;
				recalculateOffsets();
				return i;
			}
		}
		return -1;
	}


	int addConstant()
	{
		ASSERT(constants_count < lengthOf(constants));

		for (int i = 0; i < lengthOf(constants); ++i)
		{
			if (constants[i].type == EMPTY)
			{
				constants[i].name = "";
				constants[i].type = BOOL;
				++constants_count;
				return i;
			}
		}
		return -1;
	}


	int getInputsCount() const
	{
		return inputs_count;
	}


	int getSize() const
	{
		if (inputs_count == 0) return 0;
		int size = 0;
		for (const auto& input : inputs)
		{
			if(input.type != EMPTY) size = Math::maximum(size, input.offset + getSize(input.type));
		}
		return size;
	}

	int getSize(Type type) const
	{
		switch (type)
		{
			case FLOAT: return sizeof(float);
			case INT: return sizeof(int);
			case BOOL: return sizeof(bool);
			default: ASSERT(false); return 1;
		}
	}

	void recalculateOffsets()
	{
		if (inputs_count == 0) return;
		int last_offset = 0;
		for(auto& input : inputs)
		{ 
			if (input.type == EMPTY) continue;
			input.offset = last_offset;
			last_offset += getSize(input.type);
		}
	}

	int getInputIdx(const char* name, int size) const
	{
		for (int i = 0; i < lengthOf(inputs); ++i)
		{
			if (inputs[i].type == Type::EMPTY) continue;
			if (strncmp(inputs[i].name, name, size) == 0) return i;
		}
		return -1;
	}


	int getConstantIdx(const char* name, int size) const
	{
		for (int i = 0; i < lengthOf(constants); ++i)
		{
			if (constants[i].type != Type::EMPTY && strncmp(constants[i].name, name, size) == 0) return i;
		}
		return -1;
	}
};


struct Condition
{
	enum class Error
	{
		NONE,
		UNKNOWN_IDENTIFIER,
		MISSING_LEFT_PARENTHESIS,
		MISSING_RIGHT_PARENTHESIS,
		UNEXPECTED_CHAR,
		OUT_OF_MEMORY,
		MISSING_BINARY_OPERAND,
		NOT_ENOUGH_PARAMETERS,
		INCORRECT_TYPE_ARGS,
		NO_RETURN_VALUE
	};

	static const char* errorToString(Error error);

	Condition(IAllocator& allocator);

	bool operator()(RunningContext& rc);
	Error compile(const char* expression, InputDecl& decl);

	Array<u8> bytecode;
};


} // namespace Anim


} // namespace Lumix