#include "unit_tests/suite/lumix_unit_tests.h"

#include "engine/mt/lock_free_fixed_queue.h"
#include "engine/mt/task.h"
#include "engine/mt/thread.h"


using namespace Lumix;


namespace
{
	struct Test
	{
		Test()
			: value(1)
		{}

		~Test()
		{
			value = 2;
		}

		i32 value;
	};

	typedef MT::LockFreeFixedQueue<Test, 16> Queue;

	class TestTaskConsumer : public MT::Task
	{
	public:
		TestTaskConsumer(Queue* queue, IAllocator& allocator)
			: MT::Task(allocator)
			, m_queue(queue)
			, m_sum(0)
		{}

		~TestTaskConsumer()
		{}

		int task()
		{
			while (!m_queue->isAborted())
			{
				Test* test = m_queue->pop(true);
				if (nullptr == test)
					break;

				m_sum += test->value;
				test->value++;

				m_queue->dealoc(test);
			}
			return 0;
		}

		i32 getSum() { return m_sum; }

	private:
		Queue* m_queue;
		i32 m_sum;
	};

	void UT_fixed_lock_queue(const char* params)
	{
		DefaultAllocator allocator;
		Queue queue;
		TestTaskConsumer testTaskConsumer(&queue, allocator);
		testTaskConsumer.create("TestTaskConsumer_Task");

		const int RUN_COUNT = 512;

		for (int i = 0; i < RUN_COUNT; i++)
		{
			Test* test = queue.alloc(true);
			queue.push(test, true);
		}

		while (!queue.isEmpty())
		{
			MT::yield();
		}

		queue.abort();
		testTaskConsumer.destroy();

		LUMIX_EXPECT(RUN_COUNT == testTaskConsumer.getSum());
	};
}

REGISTER_TEST("unit_tests/engine/multi_thread/fixed_lock_queue", UT_fixed_lock_queue, "");
