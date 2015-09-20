#include "profiler_ui.h"
#include "core/math_utils.h"
#include "core/resource.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "debug/allocator.h"
#include "debug/stack_tree.h"
#include "engine/engine.h"
#include "ocornut-imgui/imgui.h"
#include "string_builder.h"


static const int MAX_FRAMES = 200;


enum Column
{
	NAME,
	TIME
};


ProfilerUI::ProfilerUI(Lumix::Debug::Allocator* allocator, Lumix::ResourceManager* resource_manager)
	: m_main_allocator(allocator)
	, m_resource_manager(resource_manager)
{
	m_root = nullptr;
	m_is_opened = false;
	m_current_block = nullptr;
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


void ProfilerUI::showProfileBlock(Block* block, int column)
{
	if (column == NAME)
	{
		while (block)
		{
			if (ImGui::TreeNode(block->m_name))
			{
				block->m_is_opened = true;
				showProfileBlock(block->m_first_child, column);
				ImGui::TreePop();
			}
			else
			{
				block->m_is_opened = false;
			}

			block = block->m_next;
		}
		return;
	}

	if (column == TIME)
	{
		while (block)
		{
			if (ImGui::Selectable(
					StringBuilder<50>("") << block->m_frames.back() << "##t"
										  << (int64_t)block,
					m_current_block == block,
					ImGuiSelectableFlags_SpanAllColumns))
			{
				m_current_block = block;
			}
			if (block->m_is_opened)
			{
				showProfileBlock(block->m_first_child, column);
			}

			block = block->m_next;
		}
	}
}


static void showCallstack(Lumix::Debug::Allocator::AllocationInfo* info)
{
	char fn_name[256];
	auto* node = info->m_stack_leaf;
	while (node)
	{
		if (Lumix::Debug::StackTree::getFunction(node, fn_name, sizeof(fn_name)))
		{
			ImGui::BulletText(fn_name);
		}
		else
		{
			ImGui::BulletText("N/A");
		}
		node = Lumix::Debug::StackTree::getParent(node);
	}
}


static const char* getResourceStateString(Lumix::Resource::State state)
{
	switch (state)
	{
		case Lumix::Resource::State::EMPTY: return "Empty"; break;
		case Lumix::Resource::State::FAILURE: return "Failure"; break;
		case Lumix::Resource::State::LOADING: return "Loading"; break;
		case Lumix::Resource::State::READY: return "Ready"; break;
		case Lumix::Resource::State::UNLOADING: return "Unloading"; break;
	}

	return "Unknown";
}


void ProfilerUI::onGuiResources()
{
	if (!m_resource_manager) return;
	if (!ImGui::CollapsingHeader("Resources")) return;

	uint32_t manager_types[] = {Lumix::ResourceManager::ANIMATION,
		Lumix::ResourceManager::MATERIAL,
		Lumix::ResourceManager::MODEL,
		Lumix::ResourceManager::PHYSICS,
		Lumix::ResourceManager::PIPELINE,
		Lumix::ResourceManager::SHADER,
		Lumix::ResourceManager::TEXTURE};
	const char* manager_names[] = {
		"Animations",
		"Materials",
		"Models",
		"Physics",
		"Pipelines",
		"Shaders",
		"Textures"
	};
	ASSERT(Lumix::lengthOf(manager_types) == Lumix::lengthOf(manager_names));
	ImGui::Indent();
	for (int i = 0; i < Lumix::lengthOf(manager_types); ++i)
	{
		if (!ImGui::CollapsingHeader(manager_names[i])) continue;

		auto* material_manager = m_resource_manager->get(manager_types[i]);
		auto& resources = material_manager->getResourceTable();

		ImGui::Columns(4);
		size_t sum = 0;
		for (auto iter = resources.begin(), end = resources.end(); iter != end; ++iter)
		{
			ImGui::Text(iter.value()->getPath().c_str());
			ImGui::NextColumn();
			ImGui::Text("%.3fKB", iter.value()->size() / 1024.0f);
			sum += iter.value()->size();
			ImGui::NextColumn();
			ImGui::Text(getResourceStateString(iter.value()->getState()));
			ImGui::NextColumn();
			ImGui::Text("%u", iter.value()->getRefCount());
			ImGui::NextColumn();
		}

		ImGui::Text("All");
		ImGui::NextColumn();
		ImGui::Text("%.3fKB", sum / 1024.0f);
		ImGui::NextColumn();
		ImGui::NextColumn();

		ImGui::Columns(1);
		ImGui::Separator();
	}
	ImGui::Unindent();
}


void ProfilerUI::onGuiMemoryProfiler()
{
	if (!m_main_allocator) return;
	if (!ImGui::CollapsingHeader("Memory")) return;

	ImGui::Text("Total size: %.3fMB", (m_main_allocator->getTotalSize() / 1024) / 1024.0f);
	static int from = 0;
	static int to = 0x7fffFFFF;
	ImGui::SameLine();
	ImGui::DragIntRange2("Interval", &from, &to);
	auto* current_info = m_main_allocator->getFirstAllocationInfo();

	int allocation_count = 0;
	while (current_info)
	{
		auto info = current_info;
		current_info = current_info->m_next;

		if (info->m_size < from || info->m_size > to) continue;

		if (info->m_size < 1024)
		{
			if (ImGui::TreeNode(info, "%dB", int(info->m_size)))
			{
				showCallstack(info);
				ImGui::TreePop();
			}
		}
		else if (info->m_size < 1024 * 1024)
		{
			if (ImGui::TreeNode(info, "%dKB", int(info->m_size / 1024)))
			{
				showCallstack(info);
				ImGui::TreePop();
			}
		}
		else
		{
			if (ImGui::TreeNode(info, "%.3fMB", (info->m_size / 1024) / 1024.0f))
			{
				showCallstack(info);
				ImGui::TreePop();
			}
		}
		++allocation_count;
	}

	ImGui::Text("Total number of allocations: %d", allocation_count);
}


void ProfilerUI::onGuiCPUProfiler()
{
	if (!ImGui::CollapsingHeader("CPU")) return;

	bool b = Lumix::g_profiler.isRecording();
	if (ImGui::Checkbox("Recording", &b))
	{
		Lumix::g_profiler.toggleRecording();
	}
	if (m_root)
	{
		ImGui::Columns(2);
		showProfileBlock(m_root, NAME);
		ImGui::NextColumn();
		showProfileBlock(m_root, TIME);
		ImGui::NextColumn();
		ImGui::Columns(1);
	}

	if (m_root)
	{
		float times[MAX_FRAMES];
		for (int i = 0; i < m_root->m_frames.size(); ++i)
		{
			times[i] = m_root->m_frames[i];
		}

		auto* block = m_current_block ? m_current_block : m_root;
		float width = ImGui::GetWindowContentRegionWidth();
		int count = Lumix::Math::minValue(int(width / 5), block->m_hit_counts.size());
		int offset = block->m_hit_counts.size() - count;
		struct PlotData
		{
			Block* block;
			int offset;
		};
		auto getter = [](void* data, int idx) -> float
		{
			auto* plot_data = (PlotData*)data;
			return plot_data->block->m_frames[plot_data->offset + idx];
		};
		PlotData plot_data;
		plot_data.block = block;
		plot_data.offset = offset;
		ImGui::PlotHistogram("",
			getter,
			&plot_data,
			count,
			0,
			block->m_name,
			0,
			FLT_MAX,
			ImVec2(width, 100));
	}
}


void ProfilerUI::onGui()
{
	if (!m_is_opened) return;

	if (ImGui::Begin("Profiler", &m_is_opened))
	{
		onGuiCPUProfiler();
		onGuiMemoryProfiler();
		onGuiResources();
	}
	ImGui::End();
}


ProfilerUI::Block::Block(Lumix::IAllocator& allocator)
	: m_frames(allocator)
	, m_hit_counts(allocator)
{
}