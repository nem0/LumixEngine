#include "profiler.h"
#include "engine/core/hash_map.h"
#include "engine/core/log.h"
#include "engine/core/timer.h"
#include "engine/core/mt/sync.h"
#include "engine/core/mt/thread.h"


namespace Lumix
{


namespace Profiler
{


struct Block
{
	struct Hit
	{
		uint64 m_length;
		uint64 m_start;
	};


	explicit Block(IAllocator& allocator)
		: allocator(allocator)
		, m_hits(allocator)
		, m_type(BlockType::TIME)
	{
	}


	~Block()
	{
		while (m_first_child)
		{
			Block* child = m_first_child->m_next;
			LUMIX_DELETE(allocator, m_first_child);
			m_first_child = child;
		}
	}


	void frame()
	{
		m_values.int_value = 0;
		m_hits.clear();
		if (m_first_child)
		{
			m_first_child->frame();
		}
		if (m_next)
		{
			m_next->frame();
		}
	}


	IAllocator& allocator;
	Block* m_parent;
	Block* m_next;
	Block* m_first_child;
	const char* m_name;
	Array<Hit> m_hits;
	BlockType m_type;
	union
	{
		float float_value;
		int int_value;
	} m_values;
};


const char* getBlockName(Block* block)
{
	return block->m_name;
}


int getBlockInt(Block* block)
{
	return block->m_values.int_value;
}


BlockType getBlockType(Block* block)
{
	return block->m_type;
}


Block* getBlockFirstChild(Block* block)
{
	return block->m_first_child;
}


Block* getBlockNext(Block* block)
{
	return block->m_next;
}


uint64 getBlockHitStart(Block* block, int hit_index)
{
		return block->m_hits[hit_index].m_start;
}


uint64 getBlockHitLength(Block* block, int hit_index)
{
	return block->m_hits[hit_index].m_length;
}


int getBlockHitCount(Block* block)
{
	return block->m_hits.size();
}


struct ThreadData
{
	ThreadData() 
	{
		root_block = current_block = nullptr;
		name[0] = '\0';
	}

	Block* root_block;
	Block* current_block;
	char name[30];
};


struct Instance
{
	Instance()
		: threads(allocator)
		, frame_listeners(allocator)
		, m_mutex(false)
	{
		threads.insert(MT::getCurrentThreadID(), &main_thread);
		timer = Timer::create(allocator);
	}


	~Instance()
	{
		Timer::destroy(timer);
		for (auto* i : threads)
		{
			if (i != &main_thread) LUMIX_DELETE(allocator, i);
		}
	}


	DefaultAllocator allocator;
	DelegateList<void()> frame_listeners;
	HashMap<uint32, ThreadData*> threads;
	ThreadData main_thread;
	Timer* timer;
	MT::SpinMutex m_mutex;
};


Instance g_instance;


float getBlockLength(Block* block)
{
	uint64 ret = 0;
	for(int i = 0, c = block->m_hits.size(); i < c; ++i)
	{
		ret += block->m_hits[i].m_length;
	}
	return float(ret / (double)g_instance.timer->getFrequency());
}


struct BlockInfo
{
	Block* block;
	ThreadData* thread_data;
};

static BlockInfo getBlock(const char* name)
{
	auto thread_id = MT::getCurrentThreadID();

	ThreadData* thread_data = nullptr;
	{
		MT::SpinLock lock(g_instance.m_mutex);
		auto iter = g_instance.threads.find(thread_id);
		if (iter == g_instance.threads.end())
		{
			g_instance.threads.insert(thread_id, LUMIX_NEW(g_instance.allocator, ThreadData));
			iter = g_instance.threads.find(thread_id);
		}

		thread_data = iter.value();
	}

	if (!thread_data->current_block)
	{
		Block* LUMIX_RESTRICT root = thread_data->root_block;
		while (root && root->m_name != name)
		{
			root = root->m_next;
		}
		if (root)
		{
			thread_data->current_block = root;
		}
		else
		{
			Block* root = LUMIX_NEW(g_instance.allocator, Block)(g_instance.allocator);
			root->m_parent = nullptr;
			root->m_next = thread_data->root_block;
			root->m_first_child = nullptr;
			root->m_name = name;
			thread_data->root_block = thread_data->current_block = root;
		}
	}
	else
	{
		Block* LUMIX_RESTRICT child = thread_data->current_block->m_first_child;
		while (child && child->m_name != name)
		{
			child = child->m_next;
		}
		if (!child)
		{
			child = LUMIX_NEW(g_instance.allocator, Block)(g_instance.allocator);
			child->m_parent = thread_data->current_block;
			child->m_first_child = nullptr;
			child->m_name = name;
			child->m_next = thread_data->current_block->m_first_child;
			thread_data->current_block->m_first_child = child;
		}

		thread_data->current_block = child;
	}

	return { thread_data->current_block, thread_data };
}


void record(const char* name, int value)
{
	auto data = getBlock(name);
	if (data.block->m_type != BlockType::INT)
	{
		data.block->m_values.int_value = 0;
		data.block->m_type = BlockType::INT;
	}
	data.block->m_values.int_value += value;
	data.thread_data->current_block = data.block->m_parent;
}


void beginBlock(const char* name)
{
	auto data = getBlock(name);

	auto& hit = data.block->m_hits.emplace();
	hit.m_start = g_instance.timer->getRawTimeSinceStart();
	hit.m_length = 0;
}


const char* getThreadName(uint32 thread_id)
{
	auto iter = g_instance.threads.find(thread_id);
	if (iter == g_instance.threads.end()) return "N/A";
	return iter.value()->name;
}


void setThreadName(const char* name)
{
	MT::SpinLock lock(g_instance.m_mutex);
	uint32 thread_id = MT::getCurrentThreadID();
	auto iter = g_instance.threads.find(thread_id);
	if (iter == g_instance.threads.end())
	{
		g_instance.threads.insert(thread_id, LUMIX_NEW(g_instance.allocator, ThreadData));
		iter = g_instance.threads.find(thread_id);
	}
	Lumix::copyString(iter.value()->name, name);
}


uint32 getThreadID(int index)
{
	auto iter = g_instance.threads.begin();
	auto end = g_instance.threads.end();
	for (int i = 0; i < index; ++i)
	{
		++iter;
		if (iter == end) return 0;
	}
	return iter.key();
}


int getThreadIndex(uint32 id)
{
	auto iter = g_instance.threads.begin();
	auto end = g_instance.threads.end();
	int idx = 0;
	while(iter != end)
	{
		if (iter.key() == id) return idx;
		++idx;
		++iter;
	}
	return -1;
}


int getThreadCount()
{
	return g_instance.threads.size();
}


uint64 now()
{
	return g_instance.timer->getRawTimeSinceStart();
}


Block* getRootBlock(uint32 thread_id)
{
	auto iter = g_instance.threads.find(thread_id);
	if (!iter.isValid()) return nullptr;

	return iter.value()->root_block;
}


void endBlock()
{
	auto thread_id = MT::getCurrentThreadID();

	ThreadData* thread_data = nullptr;
	{
		MT::SpinLock lock(g_instance.m_mutex);
		auto iter = g_instance.threads.find(thread_id);
		ASSERT(iter.isValid());
		thread_data = iter.value();
	}

	ASSERT(thread_data->current_block);
	uint64 now = g_instance.timer->getRawTimeSinceStart();
	thread_data->current_block->m_hits.back().m_length = now - thread_data->current_block->m_hits.back().m_start;
	thread_data->current_block = thread_data->current_block->m_parent;
}


void frame()
{
	PROFILE_FUNCTION();

	MT::SpinLock lock(g_instance.m_mutex);
	g_instance.frame_listeners.invoke();
	uint64 now = g_instance.timer->getRawTimeSinceStart();

	for (auto* i : g_instance.threads)
	{
		if (!i->root_block) continue;
		i->root_block->frame();
		auto* block = i->current_block;
		while (block)
		{
			auto& hit = block->m_hits.emplace();
			hit.m_start = now;
			hit.m_length = 0;
			block = block->m_parent;
		}
	}
}


DelegateList<void()>& getFrameListeners()
{
	return g_instance.frame_listeners;
}


} // namespace Lumix


} //	namespace Profiler
