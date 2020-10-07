#include <imgui/imgui.h>

#include "profiler_ui.h"
#include "engine/crt.h"
#include "engine/debug.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/atomic.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/page_allocator.h"
#include "engine/profiler.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "utils.h"


namespace Lumix
{


static constexpr int DEFAULT_RANGE = 100'000;


enum Column
{
	NAME,
	TIME,
	EXCLUSIVE_TIME,
	HIT_COUNT,
	HITS
};


enum MemoryColumn
{
	FUNCTION,
	SIZE
};


static const char* getContexSwitchReasonString(i8 reason)
{
	const char* reasons[] = {
		"Executive"		   ,
		"FreePage"		   ,
		"PageIn"		   ,
		"PoolAllocation"   ,
		"DelayExecution"   ,
		"Suspended"		   ,
		"UserRequest"	   ,
		"WrExecutive"	   ,
		"WrFreePage"	   ,
		"WrPageIn"		   ,
		"WrPoolAllocation" ,
		"WrDelayExecution" ,
		"WrSuspended"	   ,
		"WrUserRequest"	   ,
		"WrEventPair"	   ,
		"WrQueue"		   ,
		"WrLpcReceive"	   ,
		"WrLpcReply"	   ,
		"WrVirtualMemory"  ,
		"WrPageOut"		   ,
		"WrRendezvous"	   ,
		"WrKeyedEvent"	   ,
		"WrTerminated"	   ,
		"WrProcessInSwap"  ,
		"WrCpuRateControl" ,
		"WrCalloutStack"   ,
		"WrKernel"		   ,
		"WrResource"	   ,
		"WrPushLock"	   ,
		"WrMutex"		   ,
		"WrQuantumEnd"	   ,
		"WrDispatchInt"	   ,
		"WrPreempted"	   ,
		"WrYieldExecution" ,
		"WrFastMutex"	   ,
		"WrGuardedMutex"   ,
		"WrRundown"		   ,
		"MaximumWaitReason",
	};
	if (reason >= (i8)lengthOf(reasons)) return "Unknown";
	return reasons[reason];
}


struct ProfilerUIImpl final : ProfilerUI
{
	ProfilerUIImpl(Debug::Allocator* allocator, Engine& engine)
		: m_main_allocator(allocator)
		, m_resource_manager(engine.getResourceManager())
		, m_engine(engine)
	{
		m_allocation_size_from = 0;
		m_allocation_size_to = 1024 * 1024;
		m_current_frame = -1;
		m_is_open = false;
		m_is_paused = true;
		m_allocation_root = LUMIX_NEW(m_allocator, AllocationStackNode)(nullptr, 0, m_allocator);
		m_filter[0] = 0;
		m_resource_filter[0] = 0;
	}


	~ProfilerUIImpl()
	{
		while (m_engine.getFileSystem().hasWork())
		{
			m_engine.getFileSystem().processCallbacks();
		}

		m_allocation_root->clear(m_allocator);
		LUMIX_DELETE(m_allocator, m_allocation_root);
	}


	void onGUI() override
	{
		PROFILE_FUNCTION();

		if (!m_is_open) return;
		if (ImGui::Begin(ICON_FA_CHART_AREA "Profiler##profiler", &m_is_open))
		{
			onGUICPUProfiler();
			onGUIMemoryProfiler();
			onGUIResources();
		}
		ImGui::End();
	}


	struct AllocationStackNode
	{
		explicit AllocationStackNode(Debug::StackNode* stack_node,
			size_t inclusive_size,
			IAllocator& allocator)
			: m_children(allocator)
			, m_allocations(allocator)
			, m_stack_node(stack_node)
			, m_inclusive_size(inclusive_size)
			, m_open(false)
		{
		}


		~AllocationStackNode()
		{
			ASSERT(m_children.empty());
		}


		void clear(IAllocator& allocator)
		{
			for (auto* child : m_children)
			{
				child->clear(allocator);
				LUMIX_DELETE(allocator, child);
			}
			m_children.clear();
		}


		size_t m_inclusive_size;
		bool m_open;
		Debug::StackNode* m_stack_node;
		Array<AllocationStackNode*> m_children;
		Array<Debug::Allocator::AllocationInfo*> m_allocations;
	};


	void onGUICPUProfiler();
	void onGUIMemoryProfiler();
	void onGUIResources();
	void onFrame();
	void addToTree(Debug::Allocator::AllocationInfo* info);
	void refreshAllocations();
	void showAllocationTree(AllocationStackNode* node, int column) const;
	AllocationStackNode* getOrCreate(AllocationStackNode* my_node,
		Debug::StackNode* external_node, size_t size);

	DefaultAllocator m_allocator;
	Debug::Allocator* m_main_allocator;
	ResourceManagerHub& m_resource_manager;
	AllocationStackNode* m_allocation_root;
	int m_allocation_size_from;
	int m_allocation_size_to;
	int m_current_frame;
	bool m_is_paused;
	u64 m_end;
	u64 m_range = DEFAULT_RANGE;
	char m_filter[100];
	char m_resource_filter[100];
	Engine& m_engine;
	OS::Timer m_timer;
	float m_autopause = -33.3333f;
	bool m_show_context_switches = false;
	bool m_show_frames = true;
	struct {
		u32 signal;
		float x, y;
		bool is_current_pos = false;
	} hovered_signal;
	i64 hovered_link = 0;
	Profiler::GPUMemStatsBlock m_gpu_mem_stats;
	bool m_is_gpu_mem_stats_valid = false;
};


static const char* getResourceStateString(Resource::State state)
{
	switch (state)
	{
		case Resource::State::EMPTY: return "Empty";
		case Resource::State::FAILURE: return "Failure";
		case Resource::State::READY: return "Ready";
	}

	return "Unknown";
}


void ProfilerUIImpl::onGUIResources()
{
	if (!ImGui::CollapsingHeader("Resources")) return;

	const float w = ImGui::CalcTextSize(ICON_FA_TIMES).x + ImGui::GetStyle().ItemSpacing.x * 2;
	ImGui::SetNextItemWidth(-w);
	ImGui::InputTextWithHint("##resource_filter", "Filter", m_resource_filter, sizeof(m_resource_filter));
	ImGui::SameLine();
	if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) {
		m_resource_filter[0] = '\0';
	}

	static const ResourceType RESOURCE_TYPES[] = { ResourceType("animation"),
		ResourceType("material"),
		ResourceType("model"),
		ResourceType("physics"),
		ResourceType("shader"),
		ResourceType("texture") };
	static const char* MANAGER_NAMES[] = {
		"Animations",
		"Materials",
		"Models",
		"Physics",
		"Shaders",
		"Textures"
	};
	ASSERT(lengthOf(RESOURCE_TYPES) == lengthOf(MANAGER_NAMES));
	ImGui::Indent();
	for (u32 i = 0; i < lengthOf(RESOURCE_TYPES); ++i)
	{
		if (!ImGui::CollapsingHeader(MANAGER_NAMES[i])) continue;

		auto* resource_manager = m_resource_manager.get(RESOURCE_TYPES[i]);
		auto& resources = resource_manager->getResourceTable();

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
			if (m_resource_filter[0] != '\0' &&
				stristr(iter.value()->getPath().c_str(), m_resource_filter) == nullptr)
			{
				continue;
			}

			ImGui::Text("%s", iter.value()->getPath().c_str());
			ImGui::NextColumn();
			ImGui::Text("%.3fKB", iter.value()->size() / 1024.0f);
			sum += iter.value()->size();
			ImGui::NextColumn();
			ImGui::Text("%s", getResourceStateString(iter.value()->getState()));
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


ProfilerUIImpl::AllocationStackNode* ProfilerUIImpl::getOrCreate(AllocationStackNode* my_node,
	Debug::StackNode* external_node,
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

	auto new_node = LUMIX_NEW(m_allocator, AllocationStackNode)(external_node, size, m_allocator);
	my_node->m_children.push(new_node);
	return new_node;
}


void ProfilerUIImpl::addToTree(Debug::Allocator::AllocationInfo* info)
{
	Debug::StackNode* nodes[1024];
	int count = Debug::StackTree::getPath(info->stack_leaf, Span(nodes));

	auto node = m_allocation_root;
	for (int i = count - 1; i >= 0; --i)
	{
		node = getOrCreate(node, nodes[i], info->size);
	}
	node->m_allocations.push(info);
}


void ProfilerUIImpl::refreshAllocations()
{
	if (!m_main_allocator) return;

	m_allocation_root->clear(m_allocator);
	LUMIX_DELETE(m_allocator, m_allocation_root);
	m_allocation_root = LUMIX_NEW(m_allocator, AllocationStackNode)(nullptr, 0, m_allocator);

	m_main_allocator->lock();
	auto* current_info = m_main_allocator->getFirstAllocationInfo();

	while (current_info)
	{
		addToTree(current_info);
		current_info = current_info->next;
	}
	m_main_allocator->unlock();
}


void ProfilerUIImpl::showAllocationTree(AllocationStackNode* node, int column) const
{
	if (column == FUNCTION)
	{
		char fn_name[100];
		int line;
		if (Debug::StackTree::getFunction(node->m_stack_node, Span(fn_name), Ref(line)))
		{
			if (line >= 0)
			{
				int len = stringLength(fn_name);
				if (len + 2 < sizeof(fn_name))
				{
					fn_name[len] = ' ';
					fn_name[len + 1] = '\0';
					++len;
					toCString(line, Span(fn_name).fromLeft(len));
				}
			}
		}
		else
		{
			copyString(fn_name, "N/A");
		}

		if (ImGui::TreeNode(node, "%s", fn_name))
		{
			node->m_open = true;
			for (auto* child : node->m_children)
			{
				showAllocationTree(child, column);
			}
			ImGui::TreePop();
		}
		else
		{
			node->m_open = false;
		}
		return;
	}

	ASSERT(column == SIZE);
	#ifdef _MSC_VER
		char size[50];
		toCStringPretty(node->m_inclusive_size, Span(size));
		ImGui::Text("%s", size);
		if (node->m_open)
		{
			for (auto* child : node->m_children)
			{
				showAllocationTree(child, column);
			}
		}
	#endif
}


void ProfilerUIImpl::onGUIMemoryProfiler()
{
	if (!ImGui::CollapsingHeader("Memory")) return;

	if (m_main_allocator) {
		if (ImGui::Button("Refresh"))
		{
			refreshAllocations();
		}

		ImGui::SameLine();
		if (ImGui::Button("Check memory"))
		{
			m_main_allocator->checkGuards();
		}
		ImGui::Text("Total size: %.3fMB", (m_main_allocator->getTotalSize() / 1024) / 1024.0f);
	}
	else {
		ImGui::TextUnformatted("Debug allocator not used, can't print memory stats.");
	}
	const PageAllocator& page_allocator = m_engine.getPageAllocator();
	const float reserved_pages_size = (page_allocator.getReservedCount() * PageAllocator::PAGE_SIZE) / (1024.f * 1024.f);
	ImGui::Text("Page allocator: %.3fMB", reserved_pages_size);

	if (m_is_gpu_mem_stats_valid) {
		const float current = m_gpu_mem_stats.current / (1024.f * 1024.f);
		const float total = m_gpu_mem_stats.total / (1024.f * 1024.f);
		const float dedicated = m_gpu_mem_stats.dedicated / (1024.f * 1024.f);
		ImGui::Text("GPU: %.02fMB/%.02f (%.02fMB dedicated)", current, total, dedicated);
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

template <typename T>
static void read(Profiler::ThreadState& ctx, u32 p, T& value)
{
	const u8* buf = ctx.buffer;
	const u32 buf_size = ctx.buffer_size;
	const u32 l = p % buf_size;
	if (l + sizeof(value) <= buf_size) {
		memcpy(&value, buf + l, sizeof(value));
		return;
	}

	memcpy(&value, buf + l, buf_size - l);
	memcpy((u8*)&value + (buf_size - l), buf, sizeof(value) - (buf_size - l));
}


static void read(Profiler::ThreadState& ctx, u32 p, u8* ptr, int size)
{
	const u8* buf = ctx.buffer;
	const u32 buf_size = ctx.buffer_size;
	const u32 l = p % buf_size;
	if (l + size <= buf_size) {
		memcpy(ptr, buf + l, size);
		return;
	}

	memcpy(ptr, buf + l, buf_size - l);
	memcpy(ptr + (buf_size - l), buf, size - (buf_size - l));
}

static void renderArrow(ImVec2 p_min, ImGuiDir dir, float scale, ImDrawList* dl)
{
	const float h = ImGui::GetFontSize() * 1.00f;
	float r = h * 0.40f * scale;
	ImVec2 center = ImVec2(p_min.x + h * 0.50f, p_min.y + h * 0.50f * scale);

	ImVec2 a, b, c;
	switch (dir)
	{
	case ImGuiDir_Up:
	case ImGuiDir_Down:
		if (dir == ImGuiDir_Up) r = -r;
		a = ImVec2(+0.000f * r,+0.750f * r);
		b = ImVec2(-0.866f * r,-0.750f * r);
		c = ImVec2(+0.866f * r,-0.750f * r);
		break;
	case ImGuiDir_Left:
	case ImGuiDir_Right:
		if (dir == ImGuiDir_Left) r = -r;
		a = ImVec2(+0.750f * r,+0.000f * r);
		b = ImVec2(-0.750f * r,+0.866f * r);
		c = ImVec2(-0.750f * r,-0.866f * r);
		break;
	case ImGuiDir_None:
	case ImGuiDir_COUNT:
		IM_ASSERT(0);
		break;
	}

	dl->AddTriangleFilled(center + a, center + b, center + c, ImGui::GetColorU32(ImGuiCol_Text));
}


struct VisibleBlock
{
	const char* name;
};


struct ThreadRecord
{
	float y;
	u32 thread_id;
	const char* name;
	struct {
		u64 time;
		bool is_enter;
	}last_context_switch;
};


void ProfilerUIImpl::onGUICPUProfiler()
{
	if (!ImGui::CollapsingHeader("CPU/GPU")) return;

	if (ImGui::Checkbox("Pause", &m_is_paused)) {
		Profiler::pause(m_is_paused);
	}
	if (!m_is_paused) {
		m_end = OS::Timer::getRawTimestamp();
	}

	Profiler::GlobalState global;
	const int contexts_count = global.threadsCount();
	if (ImGui::BeginMenu("Advanced")) {
		ImGui::Checkbox("Show frames", &m_show_frames);
		ImGui::Text("Zoom: %f", m_range / double(DEFAULT_RANGE));
		if (ImGui::MenuItem("Reset zoom")) m_range = DEFAULT_RANGE;
		bool do_autopause = m_autopause >= 0;
		if (ImGui::Checkbox("Autopause enabled", &do_autopause)) {
			m_autopause = -m_autopause;
		}
		if (m_autopause >= 0) {
			ImGui::InputFloat("Autopause limit (ms)", &m_autopause, 1.f, 10.f, "%.2f");
		}
		if (ImGui::BeginMenu("Threads")) {
			for (int i = 0; i < contexts_count; ++i) {
				Profiler::ThreadState ctx(global, i);
				ImGui::Checkbox(ctx.name, &ctx.show);
			}
			ImGui::EndMenu();
		}
		if (Profiler::contextSwitchesEnabled())
		{
			ImGui::Checkbox("Show context switches", &m_show_context_switches);
		}
		else {
			ImGui::Separator();
			ImGui::Text("Context switch tracing not available.");
			ImGui::Text("Run the app as an administrator.");
		}
		ImGui::EndMenu();
	}

	const u64 view_start = m_end - m_range;

	const float from_x = ImGui::GetCursorScreenPos().x;
	const float from_y = ImGui::GetCursorScreenPos().y;
	const float to_x = from_x + ImGui::GetContentRegionAvail().x;
	ImDrawList* dl = ImGui::GetWindowDrawList();

	HashMap<u32, ThreadRecord> threads_records(64, m_allocator);
	auto getThreadName = [&](u32 thread_id){
		for(auto iter : threads_records) {
			if(iter.thread_id == thread_id) return iter.name;
		}
		return "Unknown";
	};
	bool any_hovered_signal = false;
	bool any_hovered_link = false;
	bool hovered_signal_current_pos = false;

	for (int i = 0; i < contexts_count; ++i) {
		Profiler::ThreadState ctx(global, i);
		if (!ctx.show) continue;
		
		threads_records.insert(ctx.thread_id, { ImGui::GetCursorScreenPos().y, ctx.thread_id, ctx.name, 0});

		if (!ImGui::TreeNode(ctx.buffer, "%s", ctx.name)) continue;

		float y = ImGui::GetCursorScreenPos().y;
		float top = y;
		u32 lines = 0;

		struct {
			u32 offset;
			i32 switch_id;
			u32 color;
			i64 link;
			Profiler::JobRecord job_info;
		} open_blocks[64];
		int level = -1;
		u32 p = ctx.begin;
		const u32 end = ctx.end;

		struct Property {
			Profiler::EventHeader header;
			int level;
			int offset;
		} properties[64];
		int properties_count = 0;

		auto draw_block = [&](u64 from, u64 to, const char* name, u32 color) {
			if (from > m_end || to < view_start) return;

			const float t_start = float(int(from - view_start) / double(m_range));
			const float t_end = float(int(to - view_start) / double(m_range));
			const float x_start = from_x * (1 - t_start) + to_x * t_start;
			float x_end = from_x * (1 - t_end) + to_x * t_end;
			if (int(x_end) == int(x_start)) ++x_end;
			const float block_y = y;
			const float w = ImGui::CalcTextSize(name).x;

			const ImVec2 ra(x_start, block_y);
			const ImVec2 rb(x_end, block_y + 19);
			if (hovered_signal.signal == open_blocks[level].job_info.signal_on_finish
				&& hovered_signal.signal != JobSystem::INVALID_HANDLE
				&& hovered_signal.is_current_pos)
			{
				dl->AddLine(ra, ImVec2(hovered_signal.x, hovered_signal.y - 2), 0xff0000ff);
			}

			dl->AddRectFilled(ra, rb, color);
			if (x_end - x_start > 2) {
				dl->AddRect(ra, rb, ImGui::GetColorU32(ImGuiCol_Border));
			}
			if (w + 2 < x_end - x_start) {
				dl->AddText(ImVec2(x_start + 2, block_y), 0xff000000, name);
			}
			if (ImGui::IsMouseHoveringRect(ra, rb)) {
				const u64 freq = Profiler::frequency();
				const float t = 1000 * float((to - from) / double(freq));
				ImGui::BeginTooltip();
				ImGui::Text("%s (%.3f ms)", name, t);
				if (open_blocks[level].link) {
					ImGui::Text("Link: %" PRId64, open_blocks[level].link);
						
					any_hovered_link = true;
					hovered_link = open_blocks[level].link;
				}
				if (open_blocks[level].job_info.signal_on_finish != JobSystem::INVALID_HANDLE) {
					any_hovered_signal = true;
					hovered_signal.signal = open_blocks[level].job_info.signal_on_finish;
					ImGui::Text("Signal on finish: %d", open_blocks[level].job_info.signal_on_finish);
				}
				if (open_blocks[level].job_info.precondition != JobSystem::INVALID_HANDLE) {
					ImGui::Text("Precondition signal: %d", open_blocks[level].job_info.precondition);
				}
				for (int i = 0; i < properties_count; ++i) {
					if (properties[i].level != level) continue;

					switch (properties[i].header.type) {
					case Profiler::EventType::INT: {
						Profiler::IntRecord r;
						read(ctx, properties[i].offset, (u8*)&r, sizeof(r));
						ImGui::Text("%s: %d", r.key, r.value);
						break;
					}
					case Profiler::EventType::STRING: {
						char tmp[128];
						const int tmp_size = properties[i].header.size - sizeof(properties[i].header);
						read(ctx, properties[i].offset, (u8*)tmp, tmp_size);
						ImGui::Text("%s", tmp);
						break;
					}
					default: ASSERT(false); break;
					}
				}
				ImGui::EndTooltip();
			}
		};

		while (p != end) {
			Profiler::EventHeader header;
			read(ctx, p, header);
			switch (header.type) {
			case Profiler::EventType::END_FIBER_WAIT:
			case Profiler::EventType::BEGIN_FIBER_WAIT: {
				const bool is_begin = header.type == Profiler::EventType::BEGIN_FIBER_WAIT;
				Profiler::FiberWaitRecord r;
				read(ctx, p + sizeof(Profiler::EventHeader), r);
				if (r.job_system_signal == hovered_signal.signal) {
					float t = float((header.time - view_start) / double(m_range));
					if (header.time < view_start) {
						t = -float((view_start - header.time) / double(m_range));
					}
					const float x = from_x * (1 - t) + to_x * t;
					if (hovered_signal.is_current_pos && (x != hovered_signal.x || y != hovered_signal.y)) {
						dl->AddLine(ImVec2(x, y - 2), ImVec2(hovered_signal.x, hovered_signal.y - 2), 0xff00ff00);
					}
				}
				if (header.time >= view_start && header.time <= m_end || is_begin && hovered_signal.signal == r.job_system_signal) {
					const float t = view_start <= header.time
						? float((header.time - view_start) / double(m_range))
						: -float((view_start - header.time) / double(m_range));
					const float x = from_x * (1 - t) + to_x * t;
					const u32 color = header.type == Profiler::EventType::END_FIBER_WAIT ? 0xffff0000 : 0xff00ff00;
					dl->AddRect(ImVec2(x - 2, y - 2), ImVec2(x + 2, y + 2), color);
					const bool mouse_hovered = ImGui::IsMouseHoveringRect(ImVec2(x - 2, y - 2), ImVec2(x + 2, y + 2));
					if (mouse_hovered || (is_begin && hovered_signal.signal == r.job_system_signal)) {
						hovered_signal.signal = r.job_system_signal;
						hovered_signal.x = x;
						hovered_signal.y = y;
						hovered_signal.is_current_pos = true;
						hovered_signal_current_pos = true;
						if (mouse_hovered) {
							any_hovered_signal = true;
							ImGui::BeginTooltip();
							ImGui::Text("Fiber switch");
							ImGui::Text("  Switch ID: %d", r.id);
							ImGui::Text("  Waiting for signal: %d", r.job_system_signal);
							ImGui::EndTooltip();
						}
					}
				}
				break;
			}
			case Profiler::EventType::LINK:
				if (level >= 0) {
					read(ctx, p + sizeof(Profiler::EventHeader), open_blocks[level].link);
				}
				break;
			case Profiler::EventType::BEGIN_BLOCK:
				++level;
				ASSERT(level < (int)lengthOf(open_blocks));
				open_blocks[level].link = 0;
				open_blocks[level].offset = p;
				open_blocks[level].color = 0xffDDddDD;
				open_blocks[level].job_info.signal_on_finish = JobSystem::INVALID_HANDLE;
				open_blocks[level].job_info.precondition = JobSystem::INVALID_HANDLE;
				lines = maximum(lines, level + 1);
				y += 20.f;
				break;
			case Profiler::EventType::END_BLOCK:
				y = maximum(y - 20.f, top);
				if (level >= 0) {
					Profiler::EventHeader start_header;
					read(ctx, open_blocks[level].offset, start_header);
					const char* name;
					read(ctx, open_blocks[level].offset + sizeof(Profiler::EventHeader), name);
					u32 color = open_blocks[level].color;
					if (open_blocks[level].job_info.signal_on_finish != JobSystem::INVALID_HANDLE
						&& hovered_signal.signal == open_blocks[level].job_info.signal_on_finish
						|| hovered_link == open_blocks[level].link 
						&& hovered_link != 0)
					{
						color = 0xff0000ff;
					}
					draw_block(start_header.time, header.time, name, color);
					while (properties_count > 0 && properties[properties_count - 1].level == level) {
						--properties_count;
					}
					--level;
				}
				break;
			case Profiler::EventType::FRAME:
				ASSERT(false);	//should be in global context
				break;
			case Profiler::EventType::INT:
			case Profiler::EventType::STRING: {
				if (properties_count < (int)lengthOf(properties) && level >= 0) {
					properties[properties_count].header = header;
					properties[properties_count].level = level;
					properties[properties_count].offset = sizeof(Profiler::EventHeader) + p;
					++properties_count;
				}
				else {
					ASSERT(properties_count == 0);
				}
				break;
			}
			case Profiler::EventType::JOB_INFO:
				if (level >= 0) {
					read(ctx, p + sizeof(Profiler::EventHeader), open_blocks[level].job_info);
				}
				break;
			case Profiler::EventType::BLOCK_COLOR:
				if (level >= 0) {
					read(ctx, p + sizeof(Profiler::EventHeader), open_blocks[level].color);
				}
				break;
			default: ASSERT(false); break;
			}
			p += header.size;
		}
		while (level >= 0) {
			y -= 20.f;
			Profiler::EventHeader start_header;
			read(ctx, open_blocks[level].offset, start_header);
			const char* name;
			read(ctx, open_blocks[level].offset + sizeof(Profiler::EventHeader), name);
			draw_block(start_header.time, m_end, name, ImGui::GetColorU32(ImGuiCol_PlotHistogram));
			--level;
		}

		ImGui::Dummy(ImVec2(to_x - from_x, lines * 20.f));

		ImGui::TreePop();
	}

	if (!any_hovered_link) hovered_link = 0;
	if (!any_hovered_signal) hovered_signal.signal = JobSystem::INVALID_HANDLE;
	if (!hovered_signal_current_pos) hovered_signal.is_current_pos = false;

	auto get_view_x = [&](u64 time) {
		const float t = time > view_start
			? float((time - view_start) / double(m_range))
			: -float((view_start - time) / double(m_range));
		return from_x * (1 - t) + to_x * t;
	};

	auto draw_cswitch = [&](float x, const Profiler::ContextSwitchRecord& r, ThreadRecord& tr, bool is_enter) {
		const float y = tr.y + 10;
		dl->AddLine(ImVec2(x, y - 5), ImVec2(x, y + 5), 0xff00ff00);
		if (!is_enter) {
			const u64 prev_switch = tr.last_context_switch.time;
			if (prev_switch) {
				float prev_x = get_view_x(prev_switch);
				dl->AddLine(ImVec2(prev_x, y), ImVec2(x, y), 0xff00ff00);
				dl->AddLine(ImVec2(prev_x, y - 5), ImVec2(prev_x, y + 5), 0xff00ff00);
			}
			else {
				dl->AddLine(ImVec2(x, y), ImVec2(0, y), 0xff00ff00);
			}
		}

		if (ImGui::IsMouseHoveringRect(ImVec2(x - 3, y - 3), ImVec2(x + 3, y + 3))) {
			ImGui::BeginTooltip();
			ImGui::Text("Context switch:");
			ImGui::Text("  from: %s (%d)", getThreadName(r.old_thread_id), r.old_thread_id);
			ImGui::Text("  to: %s (%d)", getThreadName(r.new_thread_id), r.new_thread_id);
			ImGui::Text("  reason: %s", getContexSwitchReasonString(r.reason));
			ImGui::EndTooltip();
		}
		tr.last_context_switch.time = r.timestamp;
		tr.last_context_switch.is_enter = is_enter;
	};

	{
		Profiler::ThreadState ctx(global, -1);

		float before_gpu_y = ImGui::GetCursorScreenPos().y;

		const bool gpu_open = ImGui::TreeNode(ctx.buffer, "GPU");
		
		float y = ImGui::GetCursorScreenPos().y;

		u32 open_blocks[64];
		int level = -1;
		u32 lines = 0;

		u32 p = ctx.begin;
		const u32 end = ctx.end;
		while (p != end) {
			Profiler::EventHeader header;
			read(ctx, p, header);
			switch (header.type) {
				case Profiler::EventType::BEGIN_GPU_BLOCK:
					++level;
					ASSERT(level < (int)lengthOf(open_blocks));
					open_blocks[level] = p;
					lines = maximum(lines, level + 1);
					break;
				case Profiler::EventType::END_GPU_BLOCK:
					if (level >= 0 && gpu_open) {
						Profiler::EventHeader start_header;
						read(ctx, open_blocks[level], start_header);
						Profiler::GPUBlock data;
						read(ctx, open_blocks[level] + sizeof(Profiler::EventHeader), data);
						u64 to;
						read(ctx, p + sizeof(Profiler::EventHeader), to);
						const u64 from = data.timestamp;
						const float t_start = float(int(from - view_start) / double(m_range));
						const float t_end = float(int(to - view_start) / double(m_range));
						const float x_start = from_x * (1 - t_start) + to_x * t_start;
						float x_end = from_x * (1 - t_end) + to_x * t_end;
						if (int(x_end) == int(x_start)) ++x_end;
						const float block_y = level * 20.f + y;
						const float w = ImGui::CalcTextSize(data.name).x;

						const ImVec2 ra(x_start, block_y);
						const ImVec2 rb(x_end, block_y + 19);
						u32 color = 0xffDDddDD;
						if(hovered_link && data.profiler_link == hovered_link) color = 0xffff0000;
						dl->AddRectFilled(ra, rb, color);
						if (x_end - x_start > 2) {
							dl->AddRect(ra, rb, ImGui::GetColorU32(ImGuiCol_Border));
						}
						if (w + 2 < x_end - x_start) {
							dl->AddText(ImVec2(x_start + 2, block_y), 0xff000000, data.name);
						}
						if (ImGui::IsMouseHoveringRect(ra, rb)) {
							const u64 freq = Profiler::frequency();
							const float t = 1000 * float((to - from) / double(freq));
							ImGui::BeginTooltip();
							ImGui::Text("%s (%.3f ms)", data.name, t);
							if (data.profiler_link) {
								ImGui::Text("Link: %" PRId64, data.profiler_link);
								any_hovered_link = true;
								hovered_link = data.profiler_link;
							}
							ImGui::EndTooltip();
						}
					}
					if (level >= 0) {
						--level;
					}
					break;
				case Profiler::EventType::GPU_FRAME:
					break;
				case Profiler::EventType::GPU_MEM_STATS:
					read(ctx, p + sizeof(Profiler::EventHeader), m_gpu_mem_stats);
					m_is_gpu_mem_stats_valid = true;
					break;
				case Profiler::EventType::FRAME:
					if (header.time >= view_start && header.time <= m_end && m_show_frames) {
						const float t = float((header.time - view_start) / double(m_range));
						const float x = from_x * (1 - t) + to_x * t;
						dl->AddLine(ImVec2(x, from_y), ImVec2(x, before_gpu_y), 0xffff0000);
					}
					break;
				case Profiler::EventType::CONTEXT_SWITCH:
					if (m_show_context_switches && header.time >= view_start && header.time <= m_end) {
						Profiler::ContextSwitchRecord r;
						read(ctx, p + sizeof(Profiler::EventHeader), r);
						auto new_iter = threads_records.find(r.new_thread_id);
						auto old_iter = threads_records.find(r.old_thread_id);
						const float x = get_view_x(header.time);

						if (new_iter.isValid()) draw_cswitch(x, r, new_iter.value(), true);
						if (old_iter.isValid()) draw_cswitch(x, r, old_iter.value(), false);
					}
					break;
				default: ASSERT(false); break;
			}
			p += header.size;
		}

		if (gpu_open) {
			if (lines > 0) ImGui::Dummy(ImVec2(to_x - from_x, lines * 20.f));
			ImGui::TreePop();
		}

		if (ImGui::IsMouseHoveringRect(ImVec2(from_x, from_y), ImVec2(to_x, ImGui::GetCursorScreenPos().y))) {
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
				m_end -= i64((ImGui::GetIO().MouseDelta.x / (to_x - from_x)) * m_range);
			}
			const u64 cursor = u64(((ImGui::GetMousePos().x - from_x) / (to_x - from_x)) * m_range) + view_start;
			u64 cursor_to_end = m_end - cursor;
			if (ImGui::GetIO().KeyCtrl) {
				if (ImGui::GetIO().MouseWheel > 0 && m_range > 1) {
					m_range >>= 1;
					cursor_to_end >>= 1;
				}
				else if (ImGui::GetIO().MouseWheel < 0) {
					m_range <<= 1;
					cursor_to_end <<= 1;
				}
				m_end = cursor_to_end + cursor;
			}
		}
	}

	for (const ThreadRecord& tr : threads_records) {
		if (tr.last_context_switch.is_enter) {
			const float x = get_view_x(tr.last_context_switch.time);
			dl->AddLine(ImVec2(to_x, tr.y + 10), ImVec2(x, tr.y + 10), 0xff00ff00);
		}
	}

	if (m_autopause > 0 && !m_is_paused && Profiler::getLastFrameDuration() * 1000.f > m_autopause) {
		m_is_paused = true;
		Profiler::pause(m_is_paused);
		m_end = OS::Timer::getRawTimestamp();
	}
}


ProfilerUI* ProfilerUI::create(Engine& engine)
{
	Debug::Allocator* allocator = engine.getAllocator().isDebug() ? static_cast<Debug::Allocator*>(&engine.getAllocator()) : nullptr;
	return LUMIX_NEW(engine.getAllocator(), ProfilerUIImpl)(allocator, engine);
}


void ProfilerUI::destroy(ProfilerUI& ui)
{
	auto& ui_impl = static_cast<ProfilerUIImpl&>(ui);
	LUMIX_DELETE(ui_impl.m_engine.getAllocator(), &ui);
}


} // namespace Lumix