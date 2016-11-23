#pragma once


#include "engine/lumix.h"


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