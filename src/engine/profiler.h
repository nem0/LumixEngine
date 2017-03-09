#pragma once


#include "engine/lumix.h"
#include "engine/delegate_list.h"
#include "engine/default_allocator.h"
#include "engine/hash_map.h"
#include "engine/mt/thread.h"


namespace Lumix
{


namespace Profiler
{


struct Block;
enum class BlockType
{
	TIME,
	INT
};


LUMIX_ENGINE_API MT::ThreadID getThreadID(int index);
LUMIX_ENGINE_API void setThreadName(const char* name);
LUMIX_ENGINE_API const char* getThreadName(MT::ThreadID thread_id);
LUMIX_ENGINE_API int getThreadIndex(u32 id);
LUMIX_ENGINE_API int getThreadCount();

LUMIX_ENGINE_API u64 now();
LUMIX_ENGINE_API Block* getRootBlock(MT::ThreadID thread_id);
LUMIX_ENGINE_API int getBlockInt(Block* block);
LUMIX_ENGINE_API BlockType getBlockType(Block* block);
LUMIX_ENGINE_API Block* getBlockFirstChild(Block* block);
LUMIX_ENGINE_API Block* getBlockNext(Block* block);
LUMIX_ENGINE_API float getBlockLength(Block* block);
LUMIX_ENGINE_API int getBlockHitCount(Block* block);
LUMIX_ENGINE_API u64 getBlockHitStart(Block* block, int hit_index);
LUMIX_ENGINE_API u64 getBlockHitLength(Block* block, int hit_index);
LUMIX_ENGINE_API const char* getBlockName(Block* block);

LUMIX_ENGINE_API void record(const char* name, int value);
LUMIX_ENGINE_API void* beginBlock(const char* name);
LUMIX_ENGINE_API void* endBlock();
LUMIX_ENGINE_API void frame();
LUMIX_ENGINE_API DelegateList<void ()>& getFrameListeners();


#ifdef _DEBUG
	struct Scope
	{
		explicit Scope(const char* name) { ptr = beginBlock(name); }
		~Scope()
		{
			void* tmp = endBlock();
			ASSERT(tmp == ptr);
		}

		const void* ptr;
	};
#else
	struct Scope
	{
		explicit Scope(const char* name) { beginBlock(name); }
		~Scope() { endBlock(); }
	};
#endif


} // namespace Profiler


#define PROFILE_INT(name, x) Lumix::Profiler::record((name), (x));
#define PROFILE_FUNCTION() Lumix::Profiler::Scope profile_scope(__FUNCTION__);
#define PROFILE_BLOCK(name) Lumix::Profiler::Scope profile_scope(name);


} // namespace Lumix
