#pragma once


#include <functional>
#include "job.h"
#include "manager.h"


namespace Lumix
{


namespace MTJD
{


	template <class T>
	class GenericJob : public MTJD::Job
	{
		public:
			GenericJob(MTJD::Manager& manager, T function, IAllocator& allocator)
				: MTJD::Job(true, MTJD::Priority::Normal, false, manager, allocator)
				, m_function(function)
			{
			}

			virtual void execute() override
			{
				m_function();
			}

		private:
			std::function<void()> m_function;
	};


	template <class T>
	MTJD::Job* makeJob(MTJD::Manager& manager, T function, IAllocator& allocator)
	{
		return allocator.newObject<GenericJob<T> >(manager, function, allocator);
	}




} // namespace MTJD


} // namespace Lumix
