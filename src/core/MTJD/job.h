#pragma once

#include "core/MTJD/enums.h"
#include "core/MTJD/group.h"

namespace Lumix
{
	namespace MTJD
	{
		class Manager;

		class LUMIX_CORE_API Job : public BaseEntry
		{
			friend class Manager;
			friend class WorkerTask;

		public:
			Job(bool auto_destroy, Priority priority, bool sync_event, Manager& manager);
			virtual ~Job();

			virtual void incrementDependency() override;
			virtual void decrementDependency() override;

			Priority getPriority() const { return m_priority; }

		protected:

			virtual void execute() = 0;
			virtual void onExecuted();

			Manager&	m_manager;
			Priority	m_priority;
			bool		m_auto_destroy;
			bool		m_scheduled;
			bool		m_executed;

		private:
			Job& operator= (const Job& rhs);
			Job(const Job&);

#ifndef __SOME_MASTER_VERSION

		protected:
			void		setJobName(const char* job_name) { m_job_name = job_name; }
			const char* getJobName() const { return m_job_name; }
			const char*	m_job_name;

#else //FINAL_RELEASE

		protected:
			void		setJobName(const char* job_name) { }
			const char* getJobName() const { return ""; }

#endif //FINAL_RELEASE
		};
	} // ~namepsace MTJD
} // ~namepsace Lumix
