#include "unit_tests/suite/lumix_unit_tests.h"
#include "core/MTJD/job.h"
#include "core/MTJD/manager.h"


namespace
{
const int32 BUFFER_SIZE = 10000;
const int32 TESTS_COUNT = 10;
const int32 TEST_RUNS = 100;

float IN1_BUFFER[TESTS_COUNT][BUFFER_SIZE];
float IN2_BUFFER[TESTS_COUNT][BUFFER_SIZE];
float OUT_BUFFER[TESTS_COUNT][BUFFER_SIZE];

static int s_auto_delete_count = 0;

static_assert(TESTS_COUNT % 2 == 0, "");
}


class TestJob : public Lumix::MTJD::Job
{
public:
	TestJob(float* buffer_in1,
		float* buffer_in2,
		float* buffer_out,
		int32 size,
		bool auto_destroy,
		Lumix::MTJD::Manager& manager,
		Lumix::IAllocator& allocator)
		: Job((auto_destroy ? Job::AUTO_DESTROY : 0) | Job::SYNC_EVENT,
			  Lumix::MTJD::Priority::Default,
			  manager,
			  allocator,
			  allocator)
		, m_buffer_in1(buffer_in1)
		, m_buffer_in2(buffer_in2)
		, m_buffer_out(buffer_out)
		, m_size(size)
	{
		setJobName("TestJob");
	}

	~TestJob()
	{
		if (m_auto_destroy)
		{
			s_auto_delete_count++;
		}
	}

	void execute() override
	{
		for (int32 i = 0; i < m_size; i++)
		{
			m_buffer_out[i] = m_buffer_in1[i] + m_buffer_in2[i];
		}
	}

private:
	float* m_buffer_in1;
	float* m_buffer_in2;
	float* m_buffer_out;
	int32 m_size;
};

void UT_MTJDFrameworkTest(const char* params)
{
	Lumix::DefaultAllocator allocator;
	Lumix::MTJD::Manager* manager = Lumix::MTJD::Manager::create(allocator);

	for (size_t x = 0; x < TEST_RUNS; x++)
	{
		for (int32 i = 0; i < TESTS_COUNT; i++)
		{
			for (int32 j = 0; j < BUFFER_SIZE; j++)
			{
				IN1_BUFFER[i][j] = (float)j;
				IN2_BUFFER[i][j] = (float)j;
				OUT_BUFFER[i][j] = 0;
			}
		}

		TestJob** jobs = (TestJob**)allocator.allocate(sizeof(TestJob*) * TESTS_COUNT);

		for (int32 i = 0; i < TESTS_COUNT; i++)
		{
			jobs[i] = LUMIX_NEW(allocator, TestJob)(IN1_BUFFER[i],
				IN2_BUFFER[i],
				OUT_BUFFER[i],
				BUFFER_SIZE,
				false,
				*manager,
				allocator);
		}

		for (int32 i = 0; i < TESTS_COUNT / 2; i += 2)
		{
			jobs[i]->addDependency(jobs[i + 1]);
		}

		for (int32 i = TESTS_COUNT - 1; i > -1; i--)
		{
			manager->schedule(jobs[i]);
		}

		for (int32 i = 0; i < TESTS_COUNT; i++)
		{
			jobs[i]->sync();
		}

		for (int32 i = 0; i < TESTS_COUNT; i++)
		{
			for (int32 j = 0; j < BUFFER_SIZE; j++)
			{
				LUMIX_EXPECT_EQ(OUT_BUFFER[i][j], (float)j + (float)j);
			}
		}

		for (int32 i = 0; i < TESTS_COUNT; i++)
		{
			LUMIX_DELETE(allocator, jobs[i]);
		}
		allocator.deallocate(jobs);
	}
	Lumix::MTJD::Manager::destroy(*manager);
}

void UT_MTJDFrameworkDependencyTest(const char* params)
{
	Lumix::DefaultAllocator allocator;
	for (int32 i = 0; i < TESTS_COUNT; i++)
	{
		for (int32 j = 0; j < BUFFER_SIZE; j++)
		{
			IN1_BUFFER[i][j] = (float)j;
			IN2_BUFFER[i][j] = (float)j;
			OUT_BUFFER[i][j] = 0;
		}
	}

	Lumix::MTJD::Manager* manager = Lumix::MTJD::Manager::create(allocator);

	TestJob** jobs = (TestJob**)allocator.allocate(sizeof(TestJob*) * TESTS_COUNT);

	for (int32 i = 0; i < TESTS_COUNT - 1; i++)
	{
		jobs[i] = LUMIX_NEW(allocator, TestJob)(IN1_BUFFER[i],
			IN2_BUFFER[i],
			IN2_BUFFER[i + 1],
			BUFFER_SIZE,
			false,
			*manager,
			allocator);
	}

	jobs[TESTS_COUNT - 1] = LUMIX_NEW(allocator, TestJob)(IN1_BUFFER[TESTS_COUNT - 1],
		IN2_BUFFER[TESTS_COUNT - 1],
		OUT_BUFFER[0],
		BUFFER_SIZE,
		false,
		*manager,
		allocator);

	for (int32 i = 0; i < TESTS_COUNT - 1; i++)
	{
		jobs[i]->addDependency(jobs[i + 1]);
	}

	for (int32 i = 0; i < TESTS_COUNT; i++)
	{
		manager->schedule(jobs[i]);
	}

	for (int32 i = 0; i < TESTS_COUNT; i++)
	{
		jobs[i]->sync();
	}

	for (int32 i = 0; i < BUFFER_SIZE; i++)
	{
		LUMIX_EXPECT_EQ(OUT_BUFFER[0][i], (float)i * (float)(TESTS_COUNT + 1));
	}

	for (int32 i = 0; i < TESTS_COUNT; i++)
	{
		LUMIX_DELETE(allocator, jobs[i]);
	}

	Lumix::MTJD::Manager::destroy(*manager);
	allocator.deallocate(jobs);
}

REGISTER_TEST("unit_tests/core/MTJD/frameworkTest", UT_MTJDFrameworkTest, "")
REGISTER_TEST("unit_tests/core/MTJD/frameworkDependencyTest", UT_MTJDFrameworkDependencyTest, "")