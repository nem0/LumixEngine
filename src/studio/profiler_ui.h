#pragma once


#include "core/array.h"
#include "debug/allocator.h"
#include "core/default_allocator.h"
#include "core/profiler.h"


namespace Lumix
{

class ResourceManager;

namespace Debug
{

class StackNode;

}

}


class ProfilerUI
{
public:
	ProfilerUI(Lumix::Debug::Allocator* allocator, Lumix::ResourceManager* resource_manager);
	~ProfilerUI();

	void onGui();

public:
	bool m_is_opened;

private:
	enum class Values
	{
		NAME,
		LENGTH,
		LENGTH_EXCLUSIVE,
		HIT_COUNT,
		COUNT
	};

	struct Block
	{
		Block(Lumix::IAllocator& allocator);
		~Block() {}

		const char* m_name;
		Block* m_parent;
		Block* m_first_child;
		Block* m_next;
		bool m_is_opened;
		Lumix::Array<float> m_frames;
		Lumix::Array<int> m_hit_counts;
	};

	struct AllocationStackNode
	{
		AllocationStackNode(Lumix::IAllocator& allocator)
			: m_children(allocator)
			, m_allocations(allocator)
		{
		}
		~AllocationStackNode();

		void clear(Lumix::IAllocator& allocator);

		size_t m_inclusive_size;
		bool m_opened;
		Lumix::Debug::StackNode* m_stack_node;
		Lumix::Array<AllocationStackNode*> m_children;
		Lumix::Array<Lumix::Debug::Allocator::AllocationInfo*> m_allocations;
	};

private:
	void onGuiCPUProfiler();
	void onGuiMemoryProfiler();
	void onGuiResources();
	void onFrame();
	void showProfileBlock(ProfilerUI::Block* block, int column);
	void cloneBlock(Block* my_block, Lumix::Profiler::Block* remote_block);
	void addToTree(Lumix::Debug::Allocator::AllocationInfo* info);
	void refreshAllocations();
	void showAllocationTree(AllocationStackNode* node, int column);
	AllocationStackNode* getOrCreate(AllocationStackNode* my_node,
		Lumix::Debug::StackNode* external_node, size_t size);

private:
	Lumix::DefaultAllocator m_allocator;
	Block* m_root;
	Block* m_current_block;
	Lumix::Debug::Allocator* m_main_allocator;
	Lumix::ResourceManager* m_resource_manager;
	int m_allocation_size_from;
	int m_allocation_size_to;
	int m_current_frame;
	AllocationStackNode* m_allocation_root;
};