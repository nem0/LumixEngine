#pragma once


#include "lumix.h"
#include "core/delegate_list.h"
#include "core/default_allocator.h"
#include "core/hash_map.h"


namespace Lumix
{


namespace Profiler
{


struct Block;
enum class BlockType
{
	TIME,
	FLOAT,
	INT
};


LUMIX_ENGINE_API uint32 getThreadID(int index);
LUMIX_ENGINE_API void setThreadName(const char* name);
LUMIX_ENGINE_API const char* getThreadName(uint32 thread_id);
LUMIX_ENGINE_API int getThreadIndex(uint32 id);
LUMIX_ENGINE_API int getThreadCount();

LUMIX_ENGINE_API float now();
LUMIX_ENGINE_API Block* getRootBlock(uint32 thread_id);
LUMIX_ENGINE_API int getBlockInt(Block* block);
LUMIX_ENGINE_API BlockType getBlockType(Block* block);
LUMIX_ENGINE_API Block* getBlockFirstChild(Block* block);
LUMIX_ENGINE_API Block* getBlockNext(Block* block);
LUMIX_ENGINE_API float getBlockLength(Block* block);
LUMIX_ENGINE_API int getBlockHitCount(Block* block);
LUMIX_ENGINE_API float getBlockHitStart(Block* block, int hit_index);
LUMIX_ENGINE_API float getBlockHitLength(Block* block, int hit_index);
LUMIX_ENGINE_API const char* getBlockName(Block* block);

LUMIX_ENGINE_API void record(const char* name, float value);
LUMIX_ENGINE_API void record(const char* name, int value);
LUMIX_ENGINE_API void beginBlock(const char* name);
LUMIX_ENGINE_API void endBlock();
LUMIX_ENGINE_API void frame();
LUMIX_ENGINE_API DelegateList<void ()>& getFrameListeners();


struct Scope
{
	explicit Scope(const char* name) { beginBlock(name); }
	~Scope() { endBlock(); }
};


} // namespace Profiler


#define PROFILE_INT(name, x) Lumix::Profiler::record((name), (x));
#define PROFILE_FUNCTION() Lumix::Profiler::Scope profile_scope(__FUNCTION__);
#define PROFILE_BLOCK(name) Lumix::Profiler::Scope profile_scope(name);


} // namespace Lumix
