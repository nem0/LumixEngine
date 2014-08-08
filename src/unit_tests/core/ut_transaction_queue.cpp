#include "unit_tests/suite/lumix_unit_tests.h"

#include "core/mt/lock_free_fixed_queue.h"
#include "core/mt/transaction.h"
#include "core/mt/task.h"
#include "core/mt/thread.h"

namespace
{
	struct Test
	{
		uint32_t idx;
		int32_t proc_count;
		uint32_t thread_id;
	};

	typedef Lumix::MT::Transaction<Test> AsynTrans;
	typedef Lumix::MT::LockFreeFixedQueue<AsynTrans, 16> TransQueue;

	class TestTaskConsumer : public Lumix::MT::Task
	{
	public:
		TestTaskConsumer(TransQueue* queue, Test* array)
			: m_trans_queue(queue)
			, m_array(array)
		{}

		~TestTaskConsumer()
		{}

		int task()
		{
			while (!m_trans_queue->isAborted())
			{
				AsynTrans* tr = m_trans_queue->pop(true);
				if (NULL == tr)
					break;

				tr->data.proc_count++;
				tr->data.thread_id = Lumix::MT::getCurrentThreadID();
				tr->setCompleted();

				m_array[tr->data.idx].proc_count = tr->data.proc_count;
				m_array[tr->data.idx].thread_id = tr->data.thread_id;
				m_trans_queue->dealoc(tr);
			}
			return 0;
		}
	private:
		TransQueue* m_trans_queue;
		Test* m_array;
	};

	class TestTaskProducer : public Lumix::MT::Task
	{
	public:
		TestTaskProducer(TransQueue* queue, Test* array, size_t size)
			: m_trans_queue(queue)
			, m_array(array)
			, m_size(size)
		{}

		~TestTaskProducer()
		{}

		int task()
		{
			for (size_t i = 0; i < m_size; i++)
			{
				AsynTrans* tr = m_trans_queue->alloc(true);
				tr->data.idx = m_array[i].idx;
				tr->data.proc_count = m_array[i].proc_count;
				tr->data.thread_id = m_array[i].thread_id;

				m_trans_queue->push(tr, true);
			}
			return 0;
		}
	private:
		TransQueue* m_trans_queue;
		Test* m_array;
		size_t m_size;
	};

	void UT_tq_heavy_usage(const char* params)
	{
		const size_t itemsCount = 1200000;
		Test* testItems = LUMIX_NEW_ARRAY(Test, itemsCount);
		for (size_t i = 0; i < itemsCount; i++)
		{
			testItems[i].idx = i;
			testItems[i].proc_count = 0;
			testItems[i].thread_id = Lumix::MT::getCurrentThreadID();
		}

		TransQueue trans_queue;

		TestTaskConsumer cons1(&trans_queue, testItems);
		TestTaskConsumer cons2(&trans_queue, testItems);
		TestTaskConsumer cons3(&trans_queue, testItems);
		TestTaskConsumer cons4(&trans_queue, testItems);

		cons1.create("cons1");
		cons2.create("cons2");
		cons3.create("cons3");
		cons4.create("cons4");

		cons1.run();
		cons2.run();
		cons3.run();
		cons4.run();

		TestTaskProducer prod1(&trans_queue, &testItems[0], itemsCount / 4);
		TestTaskProducer prod2(&trans_queue, &testItems[itemsCount / 4], itemsCount / 4);
		TestTaskProducer prod3(&trans_queue, &testItems[itemsCount / 2], itemsCount / 4);
		TestTaskProducer prod4(&trans_queue, &testItems[3 * itemsCount / 4], itemsCount / 4);

		prod1.create("prod1");
		prod2.create("prod2");
		prod3.create("prod3");
		prod4.create("prod4");

		prod1.run();
		prod2.run();
		prod3.run();
		prod4.run();

		while (!prod1.isFinished()
			|| !prod2.isFinished()
			|| !prod3.isFinished()
			|| !prod4.isFinished()
			|| !trans_queue.isEmpty())
			Lumix::MT::yield();

		trans_queue.abort();
		trans_queue.abort();
		trans_queue.abort();
		trans_queue.abort();

		prod1.destroy();
		prod2.destroy();
		prod3.destroy();
		prod4.destroy();

		cons1.destroy();
		cons2.destroy();
		cons3.destroy();
		cons4.destroy();

		Lumix::g_log_info.log("unit") << "UT_tq_heavy_usage is finishing ...";
		Lumix::g_log_info.log("unit") << "UT_tq_heavy_usage is checking results ...";

		for (size_t i = 0; i < itemsCount; i++)
		{
			LUMIX_EXPECT_EQ(testItems[i].idx, i);
			LUMIX_EXPECT_EQ(testItems[i].proc_count, 1);
			LUMIX_EXPECT_NE(testItems[i].thread_id, Lumix::MT::getCurrentThreadID());
		}

		LUMIX_DELETE_ARRAY(testItems);

		Lumix::g_log_info.log("unit") << "UT_tq_heavy_usage finished ...";
	};

	void UT_tq_push(const char* params)
	{
		const size_t itemsCount = 1200000;
		Test* testItems = LUMIX_NEW_ARRAY(Test, itemsCount);
		for (size_t i = 0; i < itemsCount; i++)
		{
			testItems[i].idx = i;
			testItems[i].proc_count = 0;
			testItems[i].thread_id = Lumix::MT::getCurrentThreadID();
		}

		TransQueue trans_queue;

		TestTaskProducer prod(&trans_queue, &testItems[0], itemsCount);
		TestTaskConsumer cons(&trans_queue, testItems);

		prod.create("producer");
		cons.create("consumer");

		prod.run();
		Lumix::MT::sleep(1000);
		cons.run();

		while (!prod.isFinished() || !trans_queue.isEmpty())
			Lumix::MT::yield();

		trans_queue.abort();

		prod.destroy();
		cons.destroy();

		Lumix::g_log_info.log("unit") << "UT_tq_push is finishing ...";
		Lumix::g_log_info.log("unit") << "UT_tq_push is checking results ...";

		for (size_t i = 0; i < itemsCount; i++)
		{
			LUMIX_EXPECT_EQ(testItems[i].idx, i);
			LUMIX_EXPECT_EQ(testItems[i].proc_count, 1);
			LUMIX_EXPECT_NE(testItems[i].thread_id, Lumix::MT::getCurrentThreadID());
		}

		LUMIX_DELETE_ARRAY(testItems);

		Lumix::g_log_info.log("unit") << "UT_tq_heavy_usage finished ...";
	}
}

REGISTER_TEST("unit_tests/core/multi_thread/transaction_queue_heavy_usage", UT_tq_heavy_usage, "");
REGISTER_TEST("unit_tests/core/multi_thread/transaction_queue_push", UT_tq_push, "");