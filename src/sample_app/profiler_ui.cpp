#include "profiler_ui.h"
#include "ocornut-imgui/imgui.h"


static const int MAX_FRAMES = 200;



ProfilerUI::ProfilerUI()
{
	m_is_opened = false;
	Lumix::g_profiler.getFrameListeners().bind<ProfilerUI, &ProfilerUI::onFrame>(this);
}


ProfilerUI::~ProfilerUI()
{
	Lumix::g_profiler.getFrameListeners().unbind<ProfilerUI, &ProfilerUI::onFrame>(this);
}


void ProfilerUI::cloneBlock(Block* my_block, Lumix::Profiler::Block* remote_block)
{
	ASSERT(my_block->m_name == remote_block->m_name);
	my_block->m_frames.push(remote_block->getLength());
	my_block->m_hit_counts.push(remote_block->getHitCount());
	if (my_block->m_frames.size() > MAX_FRAMES)
	{
		my_block->m_frames.erase(0);
	}
	if (my_block->m_hit_counts.size() > MAX_FRAMES)
	{
		my_block->m_hit_counts.erase(0);
	}

	if (!my_block->m_first_child && remote_block->m_first_child)
	{
		Lumix::Profiler::Block* remote_child = remote_block->m_first_child;
		Block* my_child = new Block(m_allocator);
		my_child->m_name = remote_child->m_name;
		my_child->m_parent = my_block;
		my_child->m_next = nullptr;
		my_child->m_first_child = nullptr;
		my_block->m_first_child = my_child;
		cloneBlock(my_child, remote_child);
	}
	else if (my_block->m_first_child)
	{
		Lumix::Profiler::Block* remote_child = remote_block->m_first_child;
		Block* my_child = my_block->m_first_child;
		if (my_child->m_name != remote_child->m_name)
		{
			Block* my_new_child = new Block(m_allocator);
			my_new_child->m_name = remote_child->m_name;
			my_new_child->m_parent = my_block;
			my_new_child->m_next = my_child;
			my_new_child->m_first_child = nullptr;
			my_block->m_first_child = my_new_child;
			my_child = my_new_child;
		}
		cloneBlock(my_child, remote_child);
	}

	if (!my_block->m_next && remote_block->m_next)
	{
		Lumix::Profiler::Block* remote_next = remote_block->m_next;
		Block* my_next = new Block(m_allocator);
		my_next->m_name = remote_next->m_name;
		my_next->m_parent = my_block->m_parent;
		my_next->m_next = nullptr;
		my_next->m_first_child = nullptr;
		my_block->m_next = my_next;
		cloneBlock(my_next, remote_next);
	}
	else if (my_block->m_next)
	{
		if (my_block->m_next->m_name != remote_block->m_next->m_name)
		{
			Block* my_next = new Block(m_allocator);
			Lumix::Profiler::Block* remote_next = remote_block->m_next;
			my_next->m_name = remote_next->m_name;
			my_next->m_parent = my_block->m_parent;
			my_next->m_next = my_block->m_next;
			my_next->m_first_child = nullptr;
			my_block->m_next = my_next;
		}
		cloneBlock(my_block->m_next, remote_block->m_next);
	}
}


void ProfilerUI::onFrame()
{
	if (!m_is_opened) return;

	if (!m_root && Lumix::g_profiler.getRootBlock())
	{
		m_root = new Block(m_allocator);
		m_root->m_name = Lumix::g_profiler.getRootBlock()->m_name;
		m_root->m_parent = nullptr;
		m_root->m_next = nullptr;
		m_root->m_first_child = nullptr;
	}
	else
	{
		ASSERT(m_root->m_name == Lumix::g_profiler.getRootBlock()->m_name);
	}
	if (m_root)
	{
		cloneBlock(m_root, Lumix::g_profiler.getRootBlock());
	}
}


void ProfilerUI::showProfileBlock(Block* block) const
{
	while (block)
	{
		if (ImGui::TreeNode(block, block->m_name))
		{
			showProfileBlock(block->m_first_child);
			ImGui::TreePop();
		}
		if (!block->m_hit_counts.empty())
		{
			ImGui::SameLine();
			ImGui::Text("%d", block->m_hit_counts.back());
			ImGui::SameLine();
			ImGui::Text("%f", block->m_frames.back());
		}
		block = block->m_next;
	}
}


void ProfilerUI::onGui()
{
	if (!m_is_opened) return;

	if (ImGui::Begin("Profiler", &m_is_opened))
	{
		bool b = Lumix::g_profiler.isRecording();
		if (ImGui::Checkbox("Recording", &b))
		{
			Lumix::g_profiler.toggleRecording();
		}
		if (m_root)
		{
			showProfileBlock(m_root);
		}

		if (m_root)
		{
			float times[MAX_FRAMES];
			for (int i = 0; i < m_root->m_frames.size(); ++i)
			{
				times[i] = m_root->m_frames[i];
			}

			auto getter = [](void* data, int idx) -> float {
				auto* block = (Block*)data;
				return block->m_frames[idx];
			};

			ImGui::PlotHistogram("",
								 getter,
								 m_root,
								 m_root->m_hit_counts.size(),
								 0,
								 nullptr,
								 FLT_MAX,
								 FLT_MAX,
								 ImVec2(0, 100));
		}
	}
	ImGui::End();
}


ProfilerUI::Block::Block(Lumix::IAllocator& allocator)
	: m_frames(allocator)
	, m_hit_counts(allocator)
{
}