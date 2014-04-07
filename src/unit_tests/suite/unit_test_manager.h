#pragma once

namespace Lux
{
	namespace UnitTest
	{
		class Manager
		{
		public:
			typedef void(*unitTestFunc)(const char*);

			static Manager& instance()
			{
				if (NULL == s_instance)
				{
					s_instance = LUX_NEW(Manager)();
				}

				return *s_instance;
			}

			static void release() { LUX_DELETE(s_instance); s_instance = NULL; }


			void registerFunction(const char* name, unitTestFunc func, const char* params);

			void dumpTests() const;
			void runTests(const char* filter_tests);
			void dumpResults() const;

			void handleFail(const char* file_name, uint32_t line);

		private:
			Manager();
			~Manager();

			struct ManagerImpl* m_impl;
			static Manager* s_instance;
		};

		class Helper
		{
		public:
			Helper(const char* name, Manager::unitTestFunc func, const char* params)
			{
				Manager::instance().registerFunction(name, func, params);
			}

			~Helper() {}
		};
	} //~UnitTest
} //~UnitTest

#define REGISTER_TEST(name, method, params) \
namespace { extern "C" Lux::UnitTest::Helper JOIN_STRINGS(JOIN_STRINGS(test_register_, method), __LINE__)(name, method, params); } \
	LUX_FORCE_SYMBOL(JOIN_STRINGS(test_register_ ,JOIN_STRINGS(method, __LINE__)))

TODO("Count error messages.");
TODO("Count warning messages.");

