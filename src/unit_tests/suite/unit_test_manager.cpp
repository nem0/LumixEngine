#include "unit_tests/suite/lumix_unit_tests.h"

#include "core/log.h"
#include "core/mt/lock_free_fixed_queue.h"
#include "core/mt/task.h"
#include "core/MT/thread.h"
#include "core/mt/transaction.h"
#include "core/queue.h"
#include "core/array.h"

#include <Windows.h>
#include <cstdio>

//#define ASSERT_HANDLE_FAIL

namespace Lumix
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
			WorkerTask(TransQueue* tests_todo, IAllocator& allocator)
				: MT::Task(allocator)
				, m_tests_todo(tests_todo)
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
					
					g_log_info.log("unit") << "-------------------------";
					g_log_info.log("unit") << ut.name;
					g_log_info.log("unit") << "-------------------------";
					ut.func(ut.parameters);
					g_log_info.log("unit") << "-------------------------";

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
					g_log_info.log("unit") << m_unit_tests[i].name;
				}

				g_log_info.log("unit") << "";
				g_log_info.log("unit") << "Running tests ...";
				g_log_info.log("unit") << "";
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
							m_trans_queue.dealoc(test);
						}
					}

					if(i < c)
					{
						UnitTestPair& pair = m_unit_tests[i];
						AsynTest* test = m_trans_queue.alloc(false);
						if (test)
						{
							test->data.name = pair.name;
							test->data.func = pair.func;
							test->data.parameters = pair.parameters;
							i++;
							m_trans_queue.push(test, true);
							m_in_progress.push(test);
						}
						else
						{
							Lumix::MT::yield();
						}
					}

					// fatal error occured. We need to respawn task.
					if (m_task.isFinished())
					{
						// test failed, remove it from the queue and spawn new thread
						AsynTest* test = m_in_progress.front();
						m_in_progress.pop();
							m_trans_queue.dealoc(test);

						m_task.destroy();
						spawnWorkerTask();
					}

					Lumix::MT::yield();
				}

				m_trans_queue.abort();
			}

			void dumpResults() const
			{
				
				if (m_fails > 0)
				{
					g_log_info.log("unit") << "----------Fails----------";
					for (int i = 0; i < m_failed_tests.size(); i++) 
					{
						g_log_info.log("unit") << m_failed_tests[i].m_file_name << "(" << m_failed_tests[i].m_line << ")";
					}
					#ifdef APPVEYOR
						FILE* fout = fopen("tests.xml", "w");
						
						fprintf(fout, "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"no\"?>"
							"<test-results  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:noNamespaceSchemaLocation=\"nunit_schema_2.5.xsd\" name=\"Lumix\" total=\"%d\" errors=\"0\" failures=\"%d\" not-run=\"0\" inconclusive=\"0\" ignored=\"0\" skipped=\"0\" invalid=\"0\">"
							"<culture-info current-culture=\"\" current-uiculture=\"\" />"
							"<test-suite type=\"Powershell\" name=\"suite-name\" executed=\"True\" result=\"Failure\" success=\"False\" time=\"0\" asserts=\"0\"> <results>"
							, m_failed_tests.size(), m_failed_tests.size());

						for (int i = 0; i < m_failed_tests.size(); i++)
						{
							fprintf(fout, "<test-case name=\"%s (%d)\" executed=\"True\" result=\"Failure\" success=\"False\" time=\"0.1443834\" asserts=\"0\"> 	<failure> 		<message>error message</message> 	<stack-trace></stack-trace> 	</failure> </test-case> 	", m_failed_tests[i].m_file_name, m_failed_tests[i].m_line);
						}

						fprintf(fout, "</results> </test-suite></test-results>");
						fclose(fout);
					#endif
				}
				g_log_info.log("unit") << "--------- Results ---------";
				g_log_info.log("unit") << "Fails:     " << m_fails;
				g_log_info.log("unit") << "---------------------------";
			}

			void handleFail(const char* file_name, uint32_t line)
			{	
				FailInfo& fi = m_failed_tests.pushEmpty();
				fi.m_file_name = file_name;
				fi.m_line = line;
				m_fails++;

#if ASSERT_HANDLE_FAIL
				ASSERT(false);
#endif //ASSERT_HANDLE_FAIL

				m_task.exit(10);
			}

			void spawnWorkerTask()
			{
				ASSERT(!m_task.isRunning());
				m_task.create("TestWorkerTask");
				m_task.run();
			}

			ManagerImpl(IAllocator& allocator)
				: m_fails(0)
				, m_task(&m_trans_queue, allocator)
				, m_in_progress(allocator)
				, m_trans_queue(allocator)
				, m_allocator(allocator)
				, m_unit_tests(allocator)
				, m_failed_tests(allocator)
			{
			}

			~ManagerImpl()
			{
				m_task.destroy();
			}

			IAllocator& getAllocator() { return m_allocator; }

		private:
			IAllocator& m_allocator;
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

		Manager::Manager(IAllocator& allocator)
		{
			m_impl = allocator.newObject<ManagerImpl>(allocator);
		}

		Manager::~Manager()
		{
			m_impl->getAllocator().deleteObject(m_impl);
		}
	} //~UnitTest
} //~UnitTest