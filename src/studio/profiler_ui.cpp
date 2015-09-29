#include "profiler_ui.h"
#include "core/math_utils.h"
#include "core/profiler.h"
#include "core/resource.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "debug/allocator.h"
#include "debug/stack_tree.h"
#include "engine/engine.h"
#include "ocornut-imgui/imgui.h"
#include "utils.h"


static const int MAX_FRAMES = 200;


enum Column
{
	NAME,
	TIME,
	HIT_COUNT
};


enum MemoryColumn
{
	FUNCTION,
	SIZE
};


void ProfilerUI::AllocationStackNode::clear(Lumix::IAllocator& allocator)
{
	for (auto* child : m_children)
	{
		child->clear(allocator);
		allocator.deleteObject(child);
	}
	m_children.clear();
}


ProfilerUI::AllocationStackNode::~AllocationStackNode()
{
	ASSERT(m_children.empty());
}


ProfilerUI::ProfilerUI(Lumix::Debug::Allocator* allocator, Lumix::ResourceManager* resource_manager)
	: m_main_allocator(allocator)
	, m_resource_manager(resource_manager)
{
	m_allocation_size_from = 0;
	m_allocation_size_to = 1024 * 1024;
	m_current_frame = -1;
	m_root = nullptr;
	m_is_opened = false;
	m_current_block = nullptr;
	Lumix::g_profiler.getFrameListeners().bind<ProfilerUI, &ProfilerUI::onFrame>(this);
	m_allocation_root = m_allocator.newObject<AllocationStackNode>(m_allocator);
	m_allocation_root->m_stack_node = nullptr;
}


ProfilerUI::~ProfilerUI()
{
	m_allocation_root->clear(m_allocator);
	m_allocator.deleteObject(m_allocation_root);
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
	switch(column)
	{
		case NAME:
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
		case TIME:
			while (block)
			{
				auto frame =
					m_current_frame < 0 ? block->m_frames.back() : block->m_frames[m_current_frame];

				if (ImGui::Selectable(StringBuilder<50>("") << frame << "##t" << (int64_t)block,
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
			return;
		case HIT_COUNT:
			while (block)
			{
				int hit_count = m_current_frame < 0 ? block->m_hit_counts.back()
													: block->m_hit_counts[m_current_frame];

				ImGui::Text("%d", hit_count);
				if (block->m_is_opened)
				{
					showProfileBlock(block->m_first_child, column);
				}

				block = block->m_next;
			}
			return;
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

		ImGui::Columns(4, "resc");
		ImGui::Text("Path");
		ImGui::NextColumn();
		ImGui::Text("Size");
		ImGui::NextColumn();
		ImGui::Text("Status");
		ImGui::NextColumn();
		ImGui::Text("References");
		ImGui::NextColumn();
		ImGui::Separator();
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
		ImGui::Separator();
		ImGui::Text("All");
		ImGui::NextColumn();
		ImGui::Text("%.3fKB", sum / 1024.0f);
		ImGui::NextColumn();
		ImGui::NextColumn();

		ImGui::Columns(1);
		
	}
	ImGui::Unindent();
}


ProfilerUI::AllocationStackNode* ProfilerUI::getOrCreate(AllocationStackNode* my_node,
	Lumix::Debug::StackNode* external_node,
	size_t size)
{
	for (auto* child : my_node->m_children)
	{
		if (child->m_stack_node == external_node)
		{
			child->m_inclusive_size += size;
			return child;
		}
	}

	auto new_node = m_allocator.newObject<AllocationStackNode>(m_allocator);
	my_node->m_children.push(new_node);
	new_node->m_stack_node = external_node;
	new_node->m_inclusive_size = size;
	return new_node;
}


void ProfilerUI::addToTree(Lumix::Debug::Allocator::AllocationInfo* info)
{
	Lumix::Debug::StackNode* nodes[1024];
	int count = Lumix::Debug::StackTree::getPath(info->m_stack_leaf, nodes, Lumix::lengthOf(nodes));

	auto node = m_allocation_root;
	for (int i = count - 1; i >= 0; --i)
	{
		node = getOrCreate(node, nodes[i], info->m_size);
	}
	node->m_allocations.push(info);
}


void ProfilerUI::refreshAllocations()
{
	m_allocation_root->clear(m_allocator);
	m_allocator.deleteObject(m_allocation_root);
	m_allocation_root = m_allocator.newObject<AllocationStackNode>(m_allocator);
	m_allocation_root->m_stack_node = nullptr;

	m_main_allocator->lock();
	auto* current_info = m_main_allocator->getFirstAllocationInfo();

	int allocation_count = 0;
	while (current_info)
	{
		addToTree(current_info);
		current_info = current_info->m_next;
	}
	m_main_allocator->unlock();
}


void ProfilerUI::showAllocationTree(AllocationStackNode* node, int column)
{
	if (column == FUNCTION)
	{
		char fn_name[100];
		int line;
		if (Lumix::Debug::StackTree::getFunction(node->m_stack_node, fn_name, sizeof(fn_name), &line))
		{
			if (line >= 0)
			{
				int len = (int)strlen(fn_name);
				if (len + 2 < sizeof(fn_name))
				{
					fn_name[len] = ' ';
					fn_name[len + 1] = '\0';
					++len;
					Lumix::toCString(line, fn_name + len, sizeof(fn_name) - len);
				}
			}
		}
		else
		{
			Lumix::copyString(fn_name, "N/A");
		}

		if (ImGui::TreeNode(node, fn_name))
		{
			node->m_opened = true;
			for (auto* child : node->m_children)
			{
				showAllocationTree(child, column);
			}
			ImGui::TreePop();
		}
		else
		{
			node->m_opened = false;
		}
		return;
	}

	ASSERT(column == SIZE);
	#ifdef _MSC_VER
		char size[50];
		Lumix::toCStringPretty(node->m_inclusive_size, size, sizeof(size));
		ImGui::Text(size);
		if (node->m_opened)
		{
			for (auto* child : node->m_children)
			{
				showAllocationTree(child, column);
			}
		}
	#endif
}


void ProfilerUI::onGuiMemoryProfiler()
{
	if (!m_main_allocator) return;
	if (!ImGui::CollapsingHeader("Memory")) return;

	if (ImGui::Button("Refresh"))
	{
		refreshAllocations();
	}

	ImGui::Text("Total size: %.3fMB", (m_main_allocator->getTotalSize() / 1024) / 1024.0f);
	ImGui::SameLine();
	if (ImGui::Button("Check memory"))
	{
		m_main_allocator->checkGuards();
	}
	
	ImGui::Columns(2, "memc");
	for (auto* child : m_allocation_root->m_children)
	{
		showAllocationTree(child, FUNCTION);
	}
	ImGui::NextColumn();
	for (auto* child : m_allocation_root->m_children)
	{
		showAllocationTree(child, SIZE);
	}
	ImGui::Columns(1);
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
		ImGui::Columns(3, "cpuc");
		showProfileBlock(m_root, NAME);
		ImGui::NextColumn();
		showProfileBlock(m_root, TIME);
		ImGui::NextColumn();
		showProfileBlock(m_root, HIT_COUNT);
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
		auto i = ImGui::PlotHistogramEx("",
			getter,
			&plot_data,
			count,
			0,
			block->m_name,
			0,
			FLT_MAX,
			ImVec2(width, 100),
			m_current_frame - offset);
		if (i != -1) m_current_frame = i + offset;
	}
}


void ProfilerUI::onGui()
{
	PROFILE_FUNCTION();
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