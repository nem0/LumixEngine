#include "unit_tests/suite/lux_unit_tests.h"

#include "core/MTJD/framework.h"

namespace
{
	const int32_t BUFFER_SIZE = 10000;
	const int32_t TESTS_COUNT = 10;

	float IN1_BUFFER[TESTS_COUNT][BUFFER_SIZE];
	float IN2_BUFFER[TESTS_COUNT][BUFFER_SIZE];
	float OUT_BUFFER[TESTS_COUNT][BUFFER_SIZE];

	static int s_auto_delete_count = 0;

	static_assert(TESTS_COUNT % 2 == 0, "");
}

class TestJob : public Lux::MTJD::Job
{
public:
	TestJob(float* buffer_in1, float* buffer_in2, float* buffer_out, int32_t size, bool auto_destroy, Lux::MTJD::Manager& manager)
		: Job(auto_destroy, Lux::MTJD::Priority::Default, true, manager)
		, m_buffer_in1(buffer_in1)
		, m_buffer_in2(buffer_in2)
		, m_buffer_out(buffer_out)
		, m_size(size)
	{
	}

	~TestJob()
	{
		if (m_auto_destroy)
		{
			s_auto_delete_count++;
		}
	}

	virtual void execute() override
	{
		for (int32_t i = 0; i < m_size; i++)
		{
			m_buffer_out[i] = m_buffer_in1[i] + m_buffer_in2[i];
		}
	}

private:
	float* m_buffer_in1;
	float* m_buffer_in2;
	float* m_buffer_out; 
	int32_t m_size;
};

void UT_MTJDFrameworkTest(const char* params)
{
	for (int32_t i = 0; i < TESTS_COUNT; i++)
	{
		for (int32_t j = 0; j < BUFFER_SIZE; j++)
		{
			IN1_BUFFER[i][j] = (float)j;
			IN2_BUFFER[i][j] = (float)j;
			OUT_BUFFER[i][j] = 0;
		}
	}

	Lux::MTJD::Manager manager;

	TestJob** jobs = LUX_NEW_ARRAY(TestJob*, TESTS_COUNT);

	for (int32_t i = 0; i < TESTS_COUNT; i++)
	{
		jobs[i] = LUX_NEW(TestJob)(IN1_BUFFER[i], IN2_BUFFER[i], OUT_BUFFER[i], BUFFER_SIZE, false, manager);
	}

	for (int32_t i = 0; i < TESTS_COUNT / 2; i += 2)
	{
		jobs[i]->addDependency(jobs[i + 1]);
	}

	for (int32_t i = 0; i < TESTS_COUNT; i++)
	{
		manager.schedule(jobs[i]);
	}

	for (int32_t i = 0; i < TESTS_COUNT; i++)
	{
		jobs[i]->sync();
	}

	for (int32_t i = 0; i < TESTS_COUNT; i++)
	{
		for (int32_t j = 0; j < BUFFER_SIZE; j++)
		{
			LUX_EXPECT_EQ(OUT_BUFFER[i][j], (float)j + (float)j);
		}
	}

	for (int32_t i = 0; i < TESTS_COUNT - 1; i++)
	{
		LUX_DELETE(jobs[i]);
	}

	LUX_DELETE_ARRAY(jobs);
}

void UT_MTJDFrameworkDependencyTest(const char* params)
{
	for (int32_t i = 0; i < TESTS_COUNT; i++)
	{
		for (int32_t j = 0; j < BUFFER_SIZE; j++)
		{
			IN1_BUFFER[i][j] = (float)j;
			IN2_BUFFER[i][j] = (float)j;
			OUT_BUFFER[i][j] = 0;
		}
	}

	Lux::MTJD::Manager manager;

	TestJob** jobs = LUX_NEW_ARRAY(TestJob*, TESTS_COUNT);

	for (int32_t i = 0; i < TESTS_COUNT - 1; i++)
	{
		jobs[i] = LUX_NEW(TestJob)(IN1_BUFFER[i], IN2_BUFFER[i], IN2_BUFFER[i + 1], BUFFER_SIZE, false, manager);
	}

	jobs[TESTS_COUNT - 1] = LUX_NEW(TestJob)(IN1_BUFFER[TESTS_COUNT - 1], IN2_BUFFER[TESTS_COUNT - 1], OUT_BUFFER[0], BUFFER_SIZE, false, manager);

	for (int32_t i = 0; i < TESTS_COUNT - 1; i++)
	{
		jobs[i]->addDependency(jobs[i + 1]);
	}

	for (int32_t i = 0; i < TESTS_COUNT; i++)
	{
		manager.schedule(jobs[i]);
	}

	for (int32_t i = 0; i < TESTS_COUNT; i++)
	{
		jobs[i]->sync();
	}

	for (int32_t i = 0; i < BUFFER_SIZE; i++)
	{
		LUX_EXPECT_EQ(OUT_BUFFER[0][i], (float)i * (float)(TESTS_COUNT + 1));
	}

	for (int32_t i = 0; i < TESTS_COUNT - 1; i++)
	{
		LUX_DELETE(jobs[i]);
	}

	LUX_DELETE_ARRAY(jobs);
}

REGISTER_TEST("unit_tests/core/MTJD/frameworkTest", UT_MTJDFrameworkTest, "")
REGISTER_TEST("unit_tests/core/MTJD/frameworkDependencyTest", UT_MTJDFrameworkDependencyTest, "")