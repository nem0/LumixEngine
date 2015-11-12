#pragma once

namespace Lumix
{
	namespace UnitTest
	{
		class Manager
		{
		public:
			typedef void(*unitTestFunc)(const char*);

			static IAllocator& getAllocator()
			{
				static DefaultAllocator allocator;
				return allocator;
			}

			static Manager& instance()
			{
				if (nullptr == s_instance)
				{
					s_instance = LUMIX_NEW(getAllocator(), Manager)(getAllocator());
				}

				return *s_instance;
			}

			static void release() { LUMIX_DELETE(getAllocator(), s_instance); s_instance = nullptr; }


			void registerFunction(const char* name, unitTestFunc func, const char* params);

			void dumpTests() const;
			void runTests(const char* filter_tests);
			void dumpResults() const;

			void handleFail(const char* file_name, uint32 line);

			Manager(IAllocator& allocator);
			~Manager();
		
		private:
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
} //~Lumix

#ifdef _WIN64
#define LUMIX_FORCE_SYMBOL(symbol) \
		__pragma(comment(linker, "/INCLUDE:" STRINGIZE(symbol)))
#else
#define LUMIX_FORCE_SYMBOL(symbol) \
		__pragma(comment(linker, "/INCLUDE:_" STRINGIZE(symbol)))
#endif

#define JOIN_STRINGS_2(A, B) A ## B
#define JOIN_STRINGS(A, B) JOIN_STRINGS_2(A, B)

#define REGISTER_TEST(name, method, params) \
namespace { extern "C" Lumix::UnitTest::Helper JOIN_STRINGS(JOIN_STRINGS(test_register_, method), __LINE__)(name, method, params); } \
	LUMIX_FORCE_SYMBOL(JOIN_STRINGS(test_register_ ,JOIN_STRINGS(method, __LINE__)))

