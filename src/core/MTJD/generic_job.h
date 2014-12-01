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
			GenericJob(IAllocator& allocator, MTJD::Manager& manager, T function)
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
	MTJD::Job* makeJob(IAllocator& allocator, MTJD::Manager& manager, T function)
	{
		return allocator.newObject<GenericJob<T> >(allocator, manager, function);
	}




} // namespace MTJD


} // namespace Lumix
