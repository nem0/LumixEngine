#pragma once


#include "engine/array.h"
#include "engine/hash_map.h"
#include "engine/lumix.h"


namespace Lumix
{


class Animation;
class IAllocator;


namespace Anim
{


typedef HashMap<u32, Animation*> AnimSet;


struct RunningContext
{
	float time_delta;
	u8* input;
	IAllocator* allocator;
	struct ComponentInstance* current;
	AnimSet* anim_set;
};



struct InputDecl
{
	enum Type : int
	{
		FLOAT,
		INT,
		BOOL
	};

	struct Constant
	{
		Type type;
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
		Type type;
		int offset;
		char name[32];
	};

	Input inputs[32];
	int inputs_count = 0;
	Constant constants[32];
	int constants_count = 0;

	int getSize() const
	{
		if (inputs_count == 0) return 0;
		return inputs[inputs_count - 1].offset + getSize(inputs[inputs_count - 1].type);
	}

	int getSize(Type type) const
	{
		switch (type)
		{
			case FLOAT: return sizeof(float);
			case INT: return sizeof(int);
			case BOOL: return sizeof(bool);
			default: ASSERT(1); return 1;
		}
	}

	void recalculateOffsets()
	{
		if (inputs_count == 0) return;
		inputs[0].offset = 0;
		for(int i = 1; i < inputs_count; ++i)
		{ 
			inputs[i].offset = inputs[i - 1].offset + getSize(inputs[i - 1].type);
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
		for (int i = 0; i < constants_count; ++i)
		{
			if (strncmp(constants[i].name, name, size) == 0) return i;
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