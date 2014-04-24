#include "unit_tests/suite/lux_unit_tests.h"

#include "core/transaction_queue.h"
#include "core/task.h"

namespace
{
	struct Test
	{
		uint32_t idx;
		int32_t proc_count;
		uint32_t thread_id;
	};

	typedef Lux::MT::Transaction<Test> AsynTrans;
	typedef Lux::MT::TransactionQueue<AsynTrans, 16> TransQueue;

	class TestTaskConsumer : public Lux::MT::Task
	{
	public:
		TestTaskConsumer(TransQueue* queue, Test* array) 
			: m_trans_queue(queue) 
			, m_array(array)
		{}
		~TestTaskConsumer() {}

		int task()
		{
			while (!m_trans_queue->isAborted())
			{
				AsynTrans* tr = m_trans_queue->pop(true);
				if (NULL == tr)
					break;

				tr->data.proc_count++;
				tr->data.thread_id = Lux::MT::getCurrentThreadID();
				tr->setCompleted();

				m_array[tr->data.idx].proc_count = tr->data.proc_count;
				m_array[tr->data.idx].thread_id = tr->data.thread_id;
				m_trans_queue->dealoc(tr, true);
			}
			return 0;
		}
	private:
		TransQueue* m_trans_queue;
		Test* m_array;
	};

	class TestTaskProducer : public Lux::MT::Task
	{
	public:
		TestTaskProducer(TransQueue* queue, Test* array, size_t size) 
			: m_trans_queue(queue) 
			, m_array(array)
			, m_size(size)
		{}

		~TestTaskProducer() {}

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

	void UT_transaction_queue(const char* params)
	{
		const size_t itemsCount = 1200000;
		Test* testItems = LUX_NEW_ARRAY(Test, itemsCount);
		for (size_t i = 0; i < itemsCount; i++)
		{
			testItems[i].idx = i;
			testItems[i].proc_count = 0;
			testItems[i].thread_id = Lux::MT::getCurrentThreadID();
		}

		TransQueue transQueue;

		TestTaskConsumer* cons1 = LUX_NEW(TestTaskConsumer)(&transQueue, testItems);
		TestTaskConsumer* cons2 = LUX_NEW(TestTaskConsumer)(&transQueue, testItems);
		TestTaskConsumer* cons3 = LUX_NEW(TestTaskConsumer)(&transQueue, testItems);
		TestTaskConsumer* cons4 = LUX_NEW(TestTaskConsumer)(&transQueue, testItems);

		cons1->create("cons1");
		cons2->create("cons2");
		cons3->create("cons3");
		cons4->create("cons4");

		cons1->run();
		cons2->run();
		cons3->run();
		cons4->run();

		TestTaskProducer* prod1 = LUX_NEW(TestTaskProducer)(&transQueue, &testItems[0], itemsCount / 4);
		TestTaskProducer* prod2 = LUX_NEW(TestTaskProducer)(&transQueue, &testItems[itemsCount / 4], itemsCount / 4);
		TestTaskProducer* prod3 = LUX_NEW(TestTaskProducer)(&transQueue, &testItems[itemsCount / 2], itemsCount / 4);
		TestTaskProducer* prod4 = LUX_NEW(TestTaskProducer)(&transQueue, &testItems[3 * itemsCount / 4], itemsCount / 4);

		prod1->create("prod1");
		prod2->create("prod2");
		prod3->create("prod3");
		prod4->create("prod4");

		prod1->run();
		prod2->run();
		prod3->run();
		prod4->run();

		while (!prod1->isFinished() 
			|| !prod2->isFinished()
			|| !prod3->isFinished()
			|| !prod4->isFinished())
			Lux::MT::sleep(0);

		prod1->destroy();
		prod2->destroy();
		prod3->destroy();
		prod4->destroy();

		while (!transQueue.isEmpty())
			Lux::MT::sleep(0);

		transQueue.abort();
		transQueue.abort();
		transQueue.abort();
		transQueue.abort();

		cons1->destroy();
		cons2->destroy();
		cons3->destroy();
		cons4->destroy();

		LUX_DELETE(cons1);
		LUX_DELETE(cons2);
		LUX_DELETE(cons3);
		LUX_DELETE(cons4);

		LUX_DELETE(prod1);
		LUX_DELETE(prod2);
		LUX_DELETE(prod3);
		LUX_DELETE(prod4);

		Lux::g_log_info.log("Unit", "UT_transaction_queue finishing ...");

		for (size_t i = 0; i < itemsCount; i++)
		{
			LUX_EXPECT_EQ(testItems[i].idx, i);
			LUX_EXPECT_EQ(testItems[i].proc_count, 1);
			LUX_EXPECT_NE(testItems[i].thread_id, Lux::MT::getCurrentThreadID());
		}

		LUX_DELETE_ARRAY(testItems);

		Lux::g_log_info.log("Unit", "UT_transaction_queue finished ...");
	};
}

REGISTER_TEST("unit_tests/core/multi_thread/transaction_queue", UT_transaction_queue, "")