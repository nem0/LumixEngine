#include "unit_tests/suite/lux_unit_tests.h"

#include "core/log.h"
#include "core/MT/lock_free_fixed_queue.h"
#include "core/MT/task.h"
#include "core/MT/transaction.h"
#include "core/queue.h"
#include "core/array.h"

#include <Windows.h>

namespace Lux
{
	namespace UnitTest
	{
		Manager* Manager::s_instance = NULL;

		static const int32_t C_MAX_TRANS = 16;

		struct UnitTestPair
		{
			const char* name;
			const char* parameters;
			Manager::unitTestFunc func;
		};

		struct FailInfo
		{
			const char* m_file_name;
			uint32_t m_line;
		};

		typedef MT::Transaction<UnitTestPair> AsynTest;
		typedef MT::LockFreeFixedQueue<AsynTest, C_MAX_TRANS> TransQueue;
		typedef Queue<AsynTest*, C_MAX_TRANS> InProgressQueue;

		class WorkerTask : public MT::Task
		{
		public:
			WorkerTask(TransQueue* tests_todo)
				: m_tests_todo(tests_todo)
			{
			}

			virtual int task() override
			{
				while(!m_tests_todo->isAborted())
				{
					AsynTest* test = m_tests_todo->pop(true);
					if(NULL == test)
						break;

					UnitTestPair& ut = test->data;
					
					g_log_info.log("unit", "-------------------------");
					g_log_info.log("unit", ut.name);
					g_log_info.log("unit", "-------------------------");
					ut.func(ut.parameters);
					g_log_info.log("unit", "-------------------------");

					test->setCompleted();
				}

				return 0;
			}

		private:
			TransQueue* m_tests_todo;
		};

		typedef Array<UnitTestPair> UnitTestTable;
		typedef Array<FailInfo> FailedTestTable;

		struct ManagerImpl
		{
		public:
			void registerFunction(const char* name, Manager::unitTestFunc func, const char* params)
			{
				UnitTestPair& pair = m_unit_tests.pushEmpty();
				pair.name = name;
				pair.parameters = params;
				pair.func = func;
			}

			void dumpTests() const
			{
				for(int i = 0, c = m_unit_tests.size(); i < c; ++i)
				{
					g_log_info.log("unit", m_unit_tests[i].name);
				}

				g_log_info.log("unit", "");
				g_log_info.log("unit", "Running tests ...");
				g_log_info.log("unit", "");
			}

			void runTests(const char* filter_tests)
			{
				spawnWorkerTask();
				int i = 0, c = m_unit_tests.size();
				while(i < c || m_in_progress.size() != 0 || !m_trans_queue.isEmpty())
				{
					if(m_in_progress.size() > 0)
					{
						AsynTest* test = m_in_progress.front();
						if(test->isCompleted())
						{
							m_in_progress.pop();
							m_trans_queue.dealoc(test, true);
						}
					}

					if(i < c)
					{
						UnitTestPair& pair = m_unit_tests[i];
						AsynTest* test = m_trans_queue.alloc(true);
						test->data.name = pair.name;
						test->data.func = pair.func;
						test->data.parameters = pair.parameters;
						i++;
						m_trans_queue.push(test, true);
						m_in_progress.push(test);
					}

					// fatal error occured. We need to respawn task.
					if(10 == m_task.getExitCode())
					{
						// test failed, remove it from the queue and spawn new thread
						AsynTest* test = m_in_progress.front();
						m_in_progress.pop();
							m_trans_queue.dealoc(test, true);

						m_task.destroy();
						spawnWorkerTask();
					}
				}

				m_trans_queue.abort();
			}

			void dumpResults() const
			{
				if (m_fails > 0)
				{
					g_log_info.log("unit", "----------Fails----------");
					for (int i = 0; i < m_failed_tests.size(); i++) {
						g_log_info.log("unit", "%s(%d)", m_failed_tests[i].m_file_name, m_failed_tests[i].m_line);
					}
				}
				g_log_info.log("unit", "--------- Results ---------");
				g_log_info.log("unit", "Fails:     %d", m_fails);
				g_log_info.log("unit", "---------------------------");
			}

			void handleFail(const char* file_name, uint32_t line)
			{	
				FailInfo& fi = m_failed_tests.pushEmpty();
				fi.m_file_name = file_name;
				fi.m_line = line;
				m_fails++;

				m_task.exit(10);
			}

			void spawnWorkerTask()
			{
				ASSERT(!m_task.isRunning());
				m_task.create("TestWorkerTask");
				m_task.run();
			}

			ManagerImpl()
				: m_fails(0)
				, m_task(&m_trans_queue)
			{
			}

			~ManagerImpl()
			{
				m_task.destroy();
			}

		private:
			uint32_t m_fails;

			UnitTestTable	m_unit_tests;
			FailedTestTable m_failed_tests;
			TransQueue		m_trans_queue;
			InProgressQueue m_in_progress;

			WorkerTask m_task;
		};

		void Manager::registerFunction(const char* name, Manager::unitTestFunc func, const char* params)
		{
			m_impl->registerFunction(name, func, params);
		}

		void Manager::dumpTests() const
		{
			m_impl->dumpTests();
		}

		void Manager::runTests(const char *filter_tests)
		{
			m_impl->runTests(filter_tests);
		}

		void Manager::dumpResults() const
		{
			m_impl->dumpResults();
		}

		void Manager::handleFail(const char* file_name, uint32_t line)
		{
			m_impl->handleFail(file_name, line);
		}

		Manager::Manager()
		{
			m_impl = LUX_NEW(ManagerImpl)();
		}

		Manager::~Manager()
		{
			LUX_DELETE(m_impl);
		}
	} //~UnitTest
} //~UnitTest