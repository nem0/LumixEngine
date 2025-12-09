#pragma once

namespace Lumix {

extern int test_count;
extern int passed_count;

#define ASSERT_EQ(expected, actual, message) \
	if ((expected) != (actual)) { \
		logError("TEST FAILED: ", message, " - Expected: ", expected, ", Actual: ", actual); \
		return false; \
	}

#define ASSERT_TRUE(condition, message) \
	if (!(condition)) { \
		logError("TEST FAILED: ", message); \
		return false; \
	}

#define RUN_TEST(test_func) \
	do { \
		++test_count; \
		if (test_func()) { \
			++passed_count; \
			logInfo("PASSED: ", #test_func); \
		} else { \
			logError("FAILED: ", #test_func); \
		} \
	} while(0)

} // namespace Lumix
