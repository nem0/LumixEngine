#pragma once


#include "core/lux.h"


namespace Lux
{
	struct PropertyListEvent;
	namespace UI
	{
		class Block;
	}
} 


class IComponentUI LUX_ABSTRACT
{
	public:
		virtual ~IComponentUI() {}
		virtual void onEntityProperties(Lux::PropertyListEvent&) = 0;
};