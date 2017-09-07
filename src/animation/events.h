#pragma once


#include "engine/lumix.h"
#include "engine/array.h"


namespace Lumix
{

	
namespace Anim
{


struct EventHeader
{
	float time;
	u32 type;
	u16 offset;
	u8 size;
};


struct EnterExitEventHeader
{
	u32 type;
	u16 offset;
	u8 size;
};


struct EventArray
{
	EventArray(IAllocator& allocator)
		: data(allocator)
		, count(0)
	{}
	void remove(int index);
	void append(int size, u32 type);

	Array<u8> data;
	int count;
};


struct SetInputEvent
{
	int input_idx;
	union {
		int i_value;
		float f_value;
		bool b_value;
	};
};


} // namespace Anim


} // namespace Lumix