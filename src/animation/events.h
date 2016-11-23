#pragma once


#include "engine/lumix.h"


namespace Lumix
{

	
namespace Anim
{


struct EventHeader
{
	float time;
	u8 type;
	u8 size;
	u16 offset;
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