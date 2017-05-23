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
		char name[32];
	};

	struct Input
	{
		Type type = EMPTY;
		int offset;
		char name[32];
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
		ASSERT(false);
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
		for (int i = 0; i < inputs_count; ++i)
		{
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
	Condition(IAllocator& allocator);

	bool operator()(RunningContext& rc);
	bool compile(const char* expression, InputDecl& decl);

	Array<u8> bytecode;
};


} // namespace Anim


} // namespace Lumix