#include "unit_tests/suite/lumix_unit_tests.h"

#include "core/mt/lock_free_fixed_queue.h"
#include "core/mt/task.h"
#include "core/mt/thread.h"

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

		int32 value;
	};

	typedef Lumix::MT::LockFreeFixedQueue<Test, 16> Queue;

	class TestTaskConsumer : public Lumix::MT::Task
	{
	public:
		TestTaskConsumer(Queue* queue, Lumix::IAllocator& allocator)
			: Lumix::MT::Task(allocator)
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

		int32 getSum() { return m_sum; }

	private:
		Queue* m_queue;
		int32 m_sum;
	};

	void UT_fixed_lock_queue(const char* params)
	{
		Lumix::DefaultAllocator allocator;
		Queue queue;
		TestTaskConsumer testTaskConsumer(&queue, allocator);
		testTaskConsumer.create("TestTaskConsumer_Task");
		testTaskConsumer.run();

		const int RUN_COUNT = 512;

		for (int i = 0; i < RUN_COUNT; i++)
		{
			Test* test = queue.alloc(true);
			queue.push(test, true);
		}

		while (!queue.isEmpty())
		{
			Lumix::MT::yield();
		}

		queue.abort();
		testTaskConsumer.destroy();

		LUMIX_EXPECT(RUN_COUNT == testTaskConsumer.getSum());
	};
}

REGISTER_TEST("unit_tests/core/multi_thread/fixed_lock_queue", UT_fixed_lock_queue, "");
