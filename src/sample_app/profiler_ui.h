#pragma once


#include "core/array.h"
#include "core/default_allocator.h"
#include "core/profiler.h"


namespace Lumix
{
	class IAllocator;
}


class ProfilerUI
{
public:
	ProfilerUI();
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

private:
	void onFrame();
	void showProfileBlock(ProfilerUI::Block* block, int column);
	void cloneBlock(Block* my_block, Lumix::Profiler::Block* remote_block);

private:
	Lumix::DefaultAllocator m_allocator;
	Block* m_root;
	Block* m_current_block;
};