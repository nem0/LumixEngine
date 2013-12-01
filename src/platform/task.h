#pragma once


#include "core/lux.h"


class LUX_PLATFORM_API Task
{
	public:
		Task();
		~Task();

		virtual int task() = 0;

		bool create();
		bool run();
		bool destroy();

	private:
		struct TaskImpl* m_implementation;
};