#pragma once


#include "engine/array.h"
#include "engine/lumix.h"


namespace Lumix
{


class IAllocator;


namespace Anim
{


struct RunningContext
{
	float time_delta;
	uint8* input;
	IAllocator* allocator;
	struct ItemInstance* current;
};



struct InputDecl
{
	enum Type
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

	Array<uint8> bytecode;
};


} // namespace Anim


} // namespace Lumix