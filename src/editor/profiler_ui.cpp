#include <imgui/imgui.h>

#include "profiler_ui.h"
#include "editor/asset_browser.h"
#include "editor/studio_app.h"
#include "engine/allocators.h"
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
#include "engine/stack_array.h"
#include "engine/string.h"


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

struct ThreadRecord
{
	float y;
	u32 thread_id;
	const char* name;
	bool show;
	struct {
		u64 time = 0;
		bool is_enter;
	} last_context_switch;
};

struct ThreadContextProxy {
	ThreadContextProxy(u8* ptr) 
	{
		InputMemoryStream blob(ptr, 9000);
		name = blob.readString();
		blob.read(thread_id);
		blob.read(begin);
		blob.read(end);
		default_show = blob.read<u8>();
		blob.read(buffer_size);
		buffer = (u8*)blob.getData() + blob.getPosition();
	}

	u8* next() { return buffer + buffer_size; }

	const char* name;
	u32 thread_id;
	u32 begin;
	u32 end;
	u32 buffer_size;
	bool default_show;
	u8* buffer;
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

template <typename T>
static void read(const ThreadContextProxy& ctx, u32 p, T& value)
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

static void read(const ThreadContextProxy& ctx, u32 p, u8* ptr, int size)
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

template <typename T>
static void overwrite(ThreadContextProxy& ctx, u32 p, const T& v) {
	p = p % ctx.buffer_size;
	if (ctx.buffer_size - p >= sizeof(v)) {
		memcpy(ctx.buffer + p, &v, sizeof(v));
	}
	else {
		const u32 prefix_len = ctx.buffer_size - p;
		memcpy(ctx.buffer + p, &v, prefix_len);
		memcpy(ctx.buffer, ((u8*)&v) + prefix_len, sizeof(v) - prefix_len);
	}
}

struct ProfilerUIImpl final : ProfilerUI
{
	ProfilerUIImpl(StudioApp& app, debug::Allocator* allocator, Engine& engine)
		: m_debug_allocator(allocator)
		, m_app(app)
		, m_threads(m_allocator)
		, m_data(m_allocator)
		, m_blocks(m_allocator)
		, m_counters(m_allocator)
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

	void onPause() {
		ASSERT(m_is_paused);
		m_data.clear();
		profiler::serialize(m_data);
		patchStrings();
		findEnd();
		preprocess();
	}

	void findEnd() {
		m_end = 0;
		forEachThread([&](ThreadContextProxy& ctx){
			u32 p = ctx.begin;
			const u32 end = ctx.end;
			while (p != end) {
				profiler::EventHeader header;
				read(ctx, p, header);
				m_end = maximum(header.time, m_end);
				p += header.size;
			}
		});
	}

	ThreadContextProxy getGlobalThreadContextProxy() {
		InputMemoryStream blob(m_data);
		blob.skip(sizeof(u32));
		const u32 count = blob.read<u32>();
		blob.skip(count * sizeof(profiler::Counter));
		blob.skip(sizeof(u32));
		return ThreadContextProxy(m_data.getMutableData() + blob.getPosition());
	}

	void countersUI(float from_x, float to_x) {
		if (!ImGui::TreeNode("Counters")) return;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		
		for (Counter& counter : m_counters) {
			const char* name = counter.name;
			if (m_filter[0] && stristr(name, m_filter) == nullptr) continue;
			if (!ImGui::TreeNode(name)) continue;

			const float top = ImGui::GetCursorScreenPos().y;
			const u64 view_start = m_end - m_range;

			const ImColor border_color(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
			const ImColor text_color(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
			dl->AddLine(ImVec2(from_x, top), ImVec2(to_x, top), border_color);
			dl->AddLine(ImVec2(from_x, top + 100), ImVec2(to_x, top + 100), border_color);
			
			char text_max[32];
			toCString(counter.max, Span(text_max), 2);
			char text_min[32];
			toCString(counter.min, Span(text_min), 2);
			float text_width = ImGui::CalcTextSize(text_max).x;
			dl->AddText(ImVec2(to_x - text_width, top), text_color, text_max);
			
			text_width = ImGui::CalcTextSize(text_min).x;
			dl->AddText(ImVec2(to_x - text_width, top + 100 - ImGui::GetTextLineHeightWithSpacing()), text_color, text_min);
			ImVec2 prev;
			bool first = true;
			const float value_range = maximum(counter.max - counter.min, 0.00001f);
			for (const Counter::Record& c : counter.records) {
				const float t_start = float(int(c.time - view_start) / double(m_range));
				const float x = from_x * (1 - t_start) + to_x * t_start;
				const float y = top + 100 - 100 * (c.value - counter.min) / value_range;
				ImVec2 p(x, y);
				if (!first) {
					dl->AddLine(prev, p, 0xffFFff00);
					dl->AddRect(p - ImVec2(2, 2), p + ImVec2(2, 2), 0xffFFff00);
					if (ImGui::IsMouseHoveringRect(p - ImVec2(2, 2), p + ImVec2(2, 2))) {
						ImGui::SetTooltip("%f", c.value);
					}
				}
				first = false;
				prev = p;
			}
			ImGui::Dummy(ImVec2(-1, 100));
			ImGui::TreePop();
		}
		ImGui::TreePop();
		return;
	}

	void patchStrings() {
		InputMemoryStream tmp(m_data);
		tmp.read<u32>();
		const u32 counters_count = tmp.read<u32>();
		m_counters.reserve(counters_count);
		for (u32 i = 0; i < counters_count; ++i) {
			Counter& c = m_counters.emplace(m_allocator);
			const profiler::Counter pc = tmp.read<profiler::Counter>();
			c.name = pc.name;
			c.min = pc.min;
		}

		const u32 count = tmp.read<u32>();
		u8* iter = (u8*)tmp.skip(0);
		ThreadContextProxy global(iter);
		iter = global.next();
		for (u32 i = 0; i < count; ++i) {
			ThreadContextProxy ctx(iter);
			iter = ctx.next();
		}

		HashMap<const void*, const char*> map(m_allocator);
		map.reserve(512);
		tmp.setPosition(iter - m_data.data());
		u32 str_count = tmp.read<u32>();
		for (u32 i = 0; i < str_count; ++i) {
			const u64 pos = tmp.getPosition();
			const char* key = (const char*)(uintptr)tmp.read<u64>();
			const char* val = tmp.readString();
			memcpy(m_data.getMutableData() + pos, &val, sizeof(val));
			map.insert(key, val);
		}

		forEachThread([&](ThreadContextProxy& ctx){
			u32 p = ctx.begin;
			const u32 end = ctx.end;
			while (p != end) {
				profiler::EventHeader header;
				read(ctx, p, header);
				switch (header.type) {
					case profiler::EventType::BEGIN_BLOCK: {
						profiler::BlockRecord tmp;
						read(ctx, p + sizeof(profiler::EventHeader), tmp);
						const char* new_val = map[tmp.name];
						overwrite(ctx, u32(p + sizeof(profiler::EventHeader)), new_val);
						break;
					}
					case profiler::EventType::INT: {
						profiler::IntRecord r;
						read(ctx, p + sizeof(profiler::EventHeader), (u8*)&r, sizeof(r));
						const char* new_val = map[r.key];
						r.key = new_val;
						overwrite(ctx, u32(p + sizeof(profiler::EventHeader)), r);
						break;
					}
					default: break;
				}
				p += header.size;
			}
		});
	}

	void load() {
		char path[LUMIX_MAX_PATH];
		if (os::getOpenFilename(Span(path), "Profile data\0*.lpd", nullptr)) {
			os::InputFile file;
			if (file.open(path)) {
				m_data.resize(file.size());
				if (!file.read(m_data.getMutableData(), m_data.size())) {
					logError("Could not read ", path);
					m_data.clear();
				}
				else {
					patchStrings();
					findEnd();
					m_is_paused = true;
				}
				file.close();
			}
			else {
				logError("Could not open ", path);
			}
		}
		preprocess();
	}

	void preprocess() {
		m_counters.clear();

		InputMemoryStream blob(m_data);
		const u32 version = blob.read<u32>();
		ASSERT(version == 0);
		const u32 counters_count = blob.read<u32>();
		for (u32 i = 0; i < counters_count; ++i) {
			const profiler::Counter pc = blob.read<profiler::Counter>();
			Counter& c = m_counters.emplace(m_allocator);
			c.name = pc.name;
			c.min = pc.min;
		}
		blob.skip(sizeof(u32));

		ThreadContextProxy global(m_data.getMutableData() + blob.getPosition());
		u32 p = global.begin;
		const u32 end = global.end;
		while (p != end) {
			profiler::EventHeader header;
			read(global, p, header);
			switch (header.type) {
				case profiler::EventType::COUNTER:
					profiler::CounterRecord tmp;
					read(global, p + sizeof(profiler::EventHeader), tmp);
					if (tmp.counter < (u32)m_counters.size()) {
						Counter& c = m_counters[tmp.counter];
						Counter::Record& r = c.records.emplace();
						r.time = header.time;
						r.value = tmp.value;
						c.min = minimum(c.min, r.value);
						c.max = maximum(c.max, r.value);
					}
					break;
			}
			p += header.size;
		}

		m_blocks.clear();
		m_blocks.reserve(16 * 1024);
		forEachThread([&](ThreadContextProxy& ctx){
			u32 p = ctx.begin;
			const u32 end = ctx.end;
			while (p != end) {
				profiler::EventHeader header;
				read(ctx, p, header);
				switch (header.type) {
					case profiler::EventType::CONTINUE_BLOCK: {
						i32 id;
						read(ctx, p + sizeof(profiler::EventHeader), id);
						if (!m_blocks.find(id).isValid()) {
							Block& b = m_blocks.insert(id);
							b.name = "N/A";
						}
						break;
					}
					case profiler::EventType::BEGIN_BLOCK: {
						profiler::BlockRecord tmp;
						read(ctx, p + sizeof(profiler::EventHeader), tmp);
						auto iter = m_blocks.find(tmp.id);
						if (iter.isValid()) {
							iter.value().name = tmp.name;
						}
						else {
							Block& b = m_blocks.insert(tmp.id);
							b.name = tmp.name;
						}
						break;
					}
				}
				p += header.size;
			}
		});
	}

	template <typename F> void forEachThread(const F& f) {
		if (m_data.empty()) return;

		InputMemoryStream blob(m_data);
		const u32 version = blob.read<u32>();
		const u32 counters_count = blob.read<u32>();
		blob.skip(counters_count * sizeof(profiler::Counter));
		const u32 count = blob.read<u32>();
		u8* iter = (u8*)blob.skip(0);
		ThreadContextProxy global(iter);
		f(global);
		iter = global.next();
		for (u32 i = 0; i < count; ++i) {
			ThreadContextProxy ctx(iter);
			f(ctx);
			iter = ctx.next();
		}
	}

	void save() {
		char path[LUMIX_MAX_PATH];
		if (os::getSaveFilename(Span(path), "Profile data\0*.lpd\0", "lpd")) {
			os::OutputFile file;
			if (file.open(path)) {
				if (!file.write(m_data.getMutableData(), m_data.size())) {
					logError("Could not write ", path);
				}
				
				file.close();
			}
			else {
				logError("Could not open ", path);
			}
		}	
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
		explicit AllocationStackNode(debug::StackNode* stack_node,
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
		debug::StackNode* m_stack_node;
		Array<AllocationStackNode*> m_children;
		Array<debug::Allocator::AllocationInfo*> m_allocations;
	};


	void onGUICPUProfiler();
	void onGUIMemoryProfiler();
	void onGUIResources();
	void onFrame();
	void addToTree(debug::Allocator::AllocationInfo* info);
	void refreshAllocations();
	void showAllocationTree(AllocationStackNode* node, int column) const;
	AllocationStackNode* getOrCreate(AllocationStackNode* my_node, debug::StackNode* external_node, size_t size);

	struct Block {
		Block() {
			job_info.signal_on_finish = 0;
		}

		const char* name;
		i64 link = 0;
		u32 color = 0xffdddddd;
		profiler::JobRecord job_info;
	};

	DefaultAllocator m_allocator;
	debug::Allocator* m_debug_allocator;
	ResourceManagerHub& m_resource_manager;
	StudioApp& m_app;
	AllocationStackNode* m_allocation_root;
	int m_allocation_size_from;
	int m_allocation_size_to;
	int m_current_frame;
	bool m_is_paused;
	u64 m_end;
	u64 m_range = DEFAULT_RANGE;
	char m_filter[100];
	char m_resource_filter[100];
	u64 m_resource_size_filter = 0;
	Engine& m_engine;
	HashMap<u32, ThreadRecord> m_threads;
	OutputMemoryStream m_data;
	os::Timer m_timer;
	float m_autopause = -33.3333f;
	bool m_show_context_switches = false;
	bool m_show_frames = true;
	
	struct {
		u32 frame = 0;
		i64 link;
	} hovered_link;

	struct {
		u32 frame = 0;
		i32 id = 0;
		ImVec2 pos;
		i32 signal = 0;
	} hovered_fiber_wait;

	struct {
		u32 frame = 0;
		i32 signal = 0;
		ImVec2 pos;
	} hovered_signal_trigger;

	struct {
		u32 frame = 0;
		i32 signal = 0;
		ImVec2 pos;
	} hovered_job;

	HashMap<i32, Block> m_blocks;
	
	struct Counter {
		struct Record {
			u64 time;
			float value;
		};
		Counter(IAllocator& allocator) : records(allocator) {}
		Array<Record> records;
		StaticString<64> name;
		float min = FLT_MAX;
		float max = -FLT_MAX;
	};

	Array<Counter> m_counters;
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
	if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) m_resource_filter[0] = '\0';
	ImGuiEx::Label("Filter size (KB)");
	ImGui::DragScalar("##fs", ImGuiDataType_U64, &m_resource_size_filter, 1000);

	static const ResourceType RESOURCE_TYPES[] = { ResourceType("animation"),
		ResourceType("material"),
		ResourceType("model"),
		ResourceType("physics_geometry"),
		ResourceType("physics_material"),
		ResourceType("shader"),
		ResourceType("texture") };
	static const char* MANAGER_NAMES[] = {
		"Animations",
		"Materials",
		"Models",
		"Physics geometries",
		"Physics materials",
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

		if (ImGui::BeginTable("resc", 4)) {
            ImGui::TableSetupColumn("Path");
            ImGui::TableSetupColumn("Size");
            ImGui::TableSetupColumn("State");
            ImGui::TableSetupColumn("References");
            ImGui::TableHeadersRow();
            
			size_t sum = 0;
			for (auto iter = resources.begin(), end = resources.end(); iter != end; ++iter) {
				if (m_resource_filter[0] != '\0' && stristr(iter.value()->getPath().c_str(), m_resource_filter) == nullptr) continue;
				if (m_resource_size_filter > iter.value()->size() / 1000) continue;
				
				ImGui::TableNextColumn();
				ImGui::PushID(iter.value());
				if (ImGuiEx::IconButton(ICON_FA_BULLSEYE, "Go to")) {
					m_app.getAssetBrowser().selectResource(iter.value()->getPath(), true, false);
				}
				ImGui::PopID();
				ImGui::SameLine();
				ImGui::Text("%s", iter.value()->getPath().c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%.3fKB", iter.value()->size() / 1024.0f);
				sum += iter.value()->size();
				ImGui::TableNextColumn();
				ImGui::Text("%s", getResourceStateString(iter.value()->getState()));
				ImGui::TableNextColumn();
				ImGui::Text("%u", iter.value()->getRefCount());
			}

			ImGui::TableNextColumn();
			ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImColor(ImGui::GetStyle().Colors[ImGuiCol_FrameBgHovered]));
			ImGui::Text("All");
			ImGui::TableNextColumn();
			ImGui::Text("%.3fKB", sum / 1024.0f);
			ImGui::TableNextColumn();
			ImGui::TableNextColumn();

			ImGui::EndTable();
		}
	}
	ImGui::Unindent();
}


ProfilerUIImpl::AllocationStackNode* ProfilerUIImpl::getOrCreate(AllocationStackNode* my_node,
	debug::StackNode* external_node,
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


void ProfilerUIImpl::addToTree(debug::Allocator::AllocationInfo* info)
{
	debug::StackNode* nodes[1024];
	int count = debug::StackTree::getPath(info->stack_leaf, Span(nodes));

	auto node = m_allocation_root;
	for (int i = count - 1; i >= 0; --i)
	{
		node = getOrCreate(node, nodes[i], info->size);
	}
	node->m_allocations.push(info);
}


void ProfilerUIImpl::refreshAllocations()
{
	if (!m_debug_allocator) return;

	m_allocation_root->clear(m_allocator);
	LUMIX_DELETE(m_allocator, m_allocation_root);
	m_allocation_root = LUMIX_NEW(m_allocator, AllocationStackNode)(nullptr, 0, m_allocator);

	m_debug_allocator->lock();
	auto* current_info = m_debug_allocator->getFirstAllocationInfo();

	while (current_info)
	{
		addToTree(current_info);
		current_info = current_info->next;
	}
	m_debug_allocator->unlock();
}


void ProfilerUIImpl::showAllocationTree(AllocationStackNode* node, int column) const
{
	if (column == FUNCTION)
	{
		char fn_name[100];
		int line;
		if (debug::StackTree::getFunction(node->m_stack_node, Span(fn_name), line))
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

	if (m_debug_allocator) {
		if (ImGui::Button("Refresh"))
		{
			refreshAllocations();
		}

		ImGui::SameLine();
		if (ImGui::Button("Check memory"))
		{
			m_debug_allocator->checkGuards();
		}
	}
	else {
		ImGui::TextUnformatted("Debug allocator not used, can't print memory stats.");
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

void ProfilerUIImpl::onGUICPUProfiler()
{
	static u32 frame_id = 0;
	++frame_id;

	if (!ImGui::CollapsingHeader("CPU/GPU")) return;

	if (m_autopause > 0 && !m_is_paused && profiler::getLastFrameDuration() * 1000.f > m_autopause) {
		m_is_paused = true;
		profiler::pause(m_is_paused);
		onPause();
	}

	if (ImGui::Button(m_is_paused ? ICON_FA_PLAY : ICON_FA_PAUSE)) {
		m_is_paused = !m_is_paused;
		profiler::pause(m_is_paused);
		if (m_is_paused) onPause();
	}

	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_COGS)) {
		ImGui::OpenPopup("profiler_advanced");
	}
	if (ImGui::BeginPopup("profiler_advanced")) {
		if (ImGui::MenuItem("Load")) load();
		if (ImGui::MenuItem("Save")) save();
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
			forEachThread([&](const ThreadContextProxy& ctx){
				auto thread = m_threads.find(ctx.thread_id);
				if (thread.isValid()) {
					ImGui::Checkbox(StaticString<128>(ctx.name, "##t", ctx.thread_id), &thread.value().show);
				}
			});
			ImGui::EndMenu();
		}
		if (profiler::contextSwitchesEnabled())
		{
			ImGui::Checkbox("Show context switches", &m_show_context_switches);
		}
		else {
			ImGui::Separator();
			ImGui::Text("Context switch tracing not available.");
			ImGui::Text("Run the app as an administrator.");
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(150);
	ImGui::InputTextWithHint("##filter", "filter", m_filter, sizeof(m_filter));
	ImGui::SameLine();
	if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) {
		m_filter[0] = '\0';
	}

	if (m_data.empty()) return;
	if (!m_is_paused) return;

	const float from_y = ImGui::GetCursorScreenPos().y;
	const float from_x = ImGui::GetCursorScreenPos().x;
	const float to_x = from_x + ImGui::GetContentRegionAvail().x;
	countersUI(from_x, to_x);

	ThreadContextProxy global = getGlobalThreadContextProxy();

	const u64 view_start = m_end - m_range;

	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->ChannelsSplit(2);

	auto getThreadName = [&](u32 thread_id){
		auto iter = m_threads.find(thread_id);
		return iter.isValid() ? iter.value().name : "Unknown";
	};

	const float line_height = ImGui::GetTextLineHeightWithSpacing();

	forEachThread([&](const ThreadContextProxy& ctx) {
		if (ctx.thread_id == 0) return;
		auto thread = m_threads.find(ctx.thread_id);
		
		if (!thread.isValid()) {
			thread = m_threads.insert(ctx.thread_id, { 0, ctx.thread_id, ctx.name, ctx.default_show, 0});
		}
		thread.value().y = ImGui::GetCursorScreenPos().y;
		if (!thread.value().show) return;
		if (!ImGui::TreeNode(ctx.buffer, "%s", ctx.name)) return;

		float y = ImGui::GetCursorScreenPos().y;
		float top = y;
		u32 lines = 0;

		struct OpenBlock {
			i32 id;
			u64 start_time;
		};
		StackArray<OpenBlock, 64> open_blocks(m_allocator);
		
		u32 p = ctx.begin;
		const u32 end = ctx.end;

		struct Property {
			profiler::EventHeader header;
			int level;
			int offset;
		};
		StackArray<Property, 64> properties(m_allocator);

		auto draw_triggered_signal = [&](u64 time, i32 signal) {
			const float t_start = float(int(time - view_start) / double(m_range));
			const float x = from_x * (1 - t_start) + to_x * t_start;
			
			if (hovered_fiber_wait.signal == signal && hovered_fiber_wait.frame > frame_id - 2) {
				dl->ChannelsSetCurrent(1);
				dl->AddLine(hovered_fiber_wait.pos, ImVec2(x, y), 0xff0000ff);
				dl->ChannelsSetCurrent(0);
			}

			if (time > m_end || time < view_start) return;
			dl->AddTriangle(ImVec2(x - 2, y), ImVec2(x + 2, y - 2), ImVec2(x + 2, y + 2), 0xffffff00);
			if (ImGui::IsMouseHoveringRect(ImVec2(x - 2, y - 2), ImVec2(x + 2, y + 2))) {
				ImGui::BeginTooltip();
				ImGui::Text("Signal triggered: %" PRIx64, (u64)signal);
				ImGui::EndTooltip();

				hovered_signal_trigger.signal = signal;
				hovered_signal_trigger.frame = frame_id;
				hovered_signal_trigger.pos = ImVec2(x, y);
			}
		};

		auto draw_block = [&](u64 from, u64 to, const char* name, u32 color) {
			const Block& block = m_blocks[open_blocks.last().id];
			if (hovered_fiber_wait.signal == block.job_info.signal_on_finish && hovered_fiber_wait.frame > frame_id - 2) {
				const float t_start = float(int(from - view_start) / double(m_range));
				const float x_start = from_x * (1 - t_start) + to_x * t_start;
				dl->ChannelsSetCurrent(1);
				dl->AddLine(hovered_fiber_wait.pos, ImVec2(x_start, y), 0xff0000ff);
				dl->ChannelsSetCurrent(0);
			}
			if (from > m_end || to < view_start) return;

			const float t_start = float(int(from - view_start) / double(m_range));
			const float t_end = float(int(to - view_start) / double(m_range));
			const float x_start = from_x * (1 - t_start) + to_x * t_start;
			float x_end = from_x * (1 - t_end) + to_x * t_end;
			if (int(x_end) == int(x_start)) ++x_end;
			const float block_y = y;
			const float w = ImGui::CalcTextSize(name).x;

			const u32 alpha = m_filter[0] && stristr(name, m_filter) == 0 ? 0x2000'0000 : 0xff00'0000;
			if (hovered_link.link == block.link && hovered_link.frame > frame_id - 2) color = 0xff0000ff;
			color = alpha | (color & 0x00ffffff);
			u32 border_color = ImGui::GetColorU32(ImGuiCol_Border);
			border_color = alpha | (border_color & 0x00ffffff);

			const ImVec2 ra(x_start, block_y);
			const ImVec2 rb(x_end, block_y + line_height - 1);

			dl->AddRectFilled(ra, rb, color);
			if (x_end - x_start > 2) {
				dl->AddRect(ra, rb, border_color);
			}
			if (w + 2 < x_end - x_start) {
				dl->AddText(ImVec2(x_start + 2, block_y), 0x00000000 | alpha, name);
			}
			if (ImGui::IsMouseHoveringRect(ra, rb)) {
				const u64 freq = profiler::frequency();
				const float t = 1000 * float((to - from) / double(freq));
				ImGui::BeginTooltip();
				ImGui::Text("%s (%.3f ms)", name, t);
				if (block.link) {
					ImGui::Text("Link: %" PRId64, block.link);
					hovered_link.frame = frame_id;
					hovered_link.link = block.link;
				}
				if (block.job_info.signal_on_finish) {
					ImGui::Text("Signal on finish: %" PRIx64, (u64)block.job_info.signal_on_finish);
					hovered_job.frame = frame_id;
					hovered_job.pos = ImVec2(x_start, block_y);
					hovered_job.signal = block.job_info.signal_on_finish;
				}
				for (const Property& prop : properties) {
					if (prop.level != open_blocks.size() - 1) continue;

					switch (prop.header.type) {
					case profiler::EventType::INT: {
						profiler::IntRecord r;
						read(ctx, prop.offset, (u8*)&r, sizeof(r));
						ImGui::Text("%s: %d", r.key, r.value);
						break;
					}
					case profiler::EventType::STRING: {
						char tmp[128];
						const int tmp_size = prop.header.size - sizeof(prop.header);
						read(ctx, prop.offset, (u8*)tmp, tmp_size);
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
			profiler::EventHeader header;
			read(ctx, p, header);
			switch (header.type) {
			case profiler::EventType::END_FIBER_WAIT:
			case profiler::EventType::BEGIN_FIBER_WAIT: {
				const bool is_begin = header.type == profiler::EventType::BEGIN_FIBER_WAIT;
				profiler::FiberWaitRecord r;
				read(ctx, p + sizeof(profiler::EventHeader), r);
				
				const float t = view_start <= header.time
					? float((header.time - view_start) / double(m_range))
					: -float((view_start - header.time) / double(m_range));
				const float x = from_x * (1 - t) + to_x * t;

				if (hovered_job.signal == r.job_system_signal && hovered_job.frame > frame_id - 2) {
					dl->ChannelsSetCurrent(1);
					dl->AddLine(ImVec2(x, y - 2), hovered_job.pos, 0xff0000ff);
					dl->ChannelsSetCurrent(0);
				}

				if (hovered_fiber_wait.id == r.id && hovered_fiber_wait.frame > frame_id - 2) {
					dl->ChannelsSetCurrent(1);
					dl->AddLine(ImVec2(x, y - 2), hovered_fiber_wait.pos, 0xff00ff00);
					dl->ChannelsSetCurrent(0);
				}

				if (hovered_signal_trigger.signal == r.job_system_signal && hovered_signal_trigger.frame > frame_id - 2) {
					dl->ChannelsSetCurrent(1);
					dl->AddLine(ImVec2(x, y - 2), hovered_signal_trigger.pos, 0xff0000ff);
					dl->ChannelsSetCurrent(0);
				}

				if (header.time >= view_start && header.time <= m_end) {
					const u32 color = r.is_mutex ? 0xff0000ff : is_begin ? 0xff00ff00 : 0xffff0000;
					dl->AddRect(ImVec2(x - 2, y - 2), ImVec2(x + 2, y + 2), color);
					const bool mouse_hovered = ImGui::IsMouseHoveringRect(ImVec2(x - 2, y - 2), ImVec2(x + 2, y + 2));
					if (mouse_hovered) {
						hovered_fiber_wait.frame = frame_id;
						hovered_fiber_wait.id = r.id;
						hovered_fiber_wait.pos = ImVec2(x, y);
						hovered_fiber_wait.signal = r.job_system_signal;

						ImGui::BeginTooltip();
						ImGui::Text("Fiber wait");
						ImGui::Text("  Wait ID: %d", r.id);
						ImGui::Text("  Waiting for signal: %" PRIx64, (u64)r.job_system_signal);
						ImGui::EndTooltip();
					}
				}
				break;
			}
			case profiler::EventType::LINK:
				if (open_blocks.size() > 0) {
					read(ctx, p + sizeof(profiler::EventHeader), m_blocks[open_blocks.last().id].link);
				}
				break;
			case profiler::EventType::BEGIN_BLOCK:
				profiler::BlockRecord tmp;
				read(ctx, p + sizeof(profiler::EventHeader), tmp);
				open_blocks.push({tmp.id, header.time});
				lines = maximum(lines, open_blocks.size());
				y += line_height;
				break;
			case profiler::EventType::CONTINUE_BLOCK: {
				i32 id;
				read(ctx, p + sizeof(profiler::EventHeader), id);
				open_blocks.push({id, header.time});
				lines = maximum(lines, open_blocks.size());
				y += line_height;
				break;
			}
			case profiler::EventType::SIGNAL_TRIGGERED: {
				i32 signal;
				read(ctx, p + sizeof(profiler::EventHeader), signal);
				draw_triggered_signal(header.time, signal);
				break;
			}
			case profiler::EventType::END_BLOCK:
				y = maximum(y - line_height, top);
				if (open_blocks.size() > 0) {
					const Block& block = m_blocks[open_blocks.last().id];
					const u32 color = block.color;
					draw_block(open_blocks.last().start_time, header.time, block.name, color);
					while (properties.size() > 0 && properties.last().level == open_blocks.size() - 1) {
						properties.pop();
					}
					open_blocks.pop();
				}
				break;
			case profiler::EventType::FRAME:
				ASSERT(false);	//should be in global context
				break;
			case profiler::EventType::INT:
			case profiler::EventType::STRING: {
				if (open_blocks.size() > 0) {
					Property& prop = properties.emplace();
					prop.header = header;
					prop.level = open_blocks.size() - 1;
					prop.offset = sizeof(profiler::EventHeader) + p;
				}
				else {
					ASSERT(properties.size() == 0);
				}
				break;
			}
			case profiler::EventType::JOB_INFO:
				if (open_blocks.size() > 0) {
					read(ctx, p + sizeof(profiler::EventHeader), m_blocks[open_blocks.last().id].job_info);
				}
				break;
			case profiler::EventType::BLOCK_COLOR:
				if (open_blocks.size() > 0) {
					read(ctx, p + sizeof(profiler::EventHeader), m_blocks[open_blocks.last().id].color);
				}
				break;
			default: ASSERT(false); break;
			}
			p += header.size;
		}
		while (open_blocks.size() > 0) {
			y -= line_height;
			const Block& b = m_blocks[open_blocks.last().id];
			draw_block(open_blocks.last().start_time, m_end, b.name, ImGui::GetColorU32(ImGuiCol_PlotHistogram));
			open_blocks.pop();
		}

		ImGui::Dummy(ImVec2(to_x - from_x, lines * line_height));

		ImGui::TreePop();
	});

	auto get_view_x = [&](u64 time) {
		const float t = time > view_start
			? float((time - view_start) / double(m_range))
			: -float((view_start - time) / double(m_range));
		return from_x * (1 - t) + to_x * t;
	};

	auto draw_cswitch = [&](float x, const profiler::ContextSwitchRecord& r, ThreadRecord& tr, bool is_enter) {
		const float y = tr.y + 10;
		dl->AddLine(ImVec2(x + (is_enter ? -2.f : 2.f), y - 5), ImVec2(x, y), 0xff00ff00);
		if (!is_enter) {
			const u64 prev_switch = tr.last_context_switch.time;
			if (prev_switch) {
				if (tr.last_context_switch.is_enter) {
					float prev_x = get_view_x(prev_switch);
					dl->AddLine(ImVec2(prev_x, y), ImVec2(x, y), 0xff00ff00);
				}
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
		ThreadContextProxy& ctx = global;

		float before_gpu_y = ImGui::GetCursorScreenPos().y;

		const bool gpu_open = ImGui::TreeNode(ctx.buffer, "GPU");
		
		float y = ImGui::GetCursorScreenPos().y;

		StackArray<u32, 32> open_blocks(m_allocator);
		u32 lines = 0;

		bool has_stats = false;
		u64 primitives_generated;
		u32 p = ctx.begin;
		const u32 end = ctx.end;
		while (p != end) {
			profiler::EventHeader header;
			read(ctx, p, header);
			switch (header.type) {
				case profiler::EventType::BEGIN_GPU_BLOCK:
					open_blocks.push(p);
					lines = maximum(lines, open_blocks.size());
					break;
				case profiler::EventType::END_GPU_BLOCK:
					if (open_blocks.size() > 0 && gpu_open) {
						profiler::EventHeader start_header;
						read(ctx, open_blocks.last(), start_header);
						profiler::GPUBlock data;
						read(ctx, open_blocks.last() + sizeof(profiler::EventHeader), data);
						u64 to;
						read(ctx, p + sizeof(profiler::EventHeader), to);
						const u64 from = data.timestamp;
						const float t_start = float(int(from - view_start) / double(m_range));
						const float t_end = float(int(to - view_start) / double(m_range));
						const float x_start = from_x * (1 - t_start) + to_x * t_start;
						float x_end = from_x * (1 - t_end) + to_x * t_end;
						if (int(x_end) == int(x_start)) ++x_end;
						const float block_y = (open_blocks.size() - 1) * line_height + y;
						const float w = ImGui::CalcTextSize(data.name).x;

						const ImVec2 ra(x_start, block_y);
						const ImVec2 rb(x_end, block_y + line_height - 1);
						u32 color = 0xffDDddDD;
						if (hovered_link.link == data.profiler_link && hovered_link.frame > frame_id - 2) color = 0xff0000ff;
						dl->AddRectFilled(ra, rb, color);
						if (x_end - x_start > 2) {
							dl->AddRect(ra, rb, ImGui::GetColorU32(ImGuiCol_Border));
						}
						if (w + 2 < x_end - x_start) {
							dl->AddText(ImVec2(x_start + 2, block_y), 0xff000000, data.name);
						}
						if (ImGui::IsMouseHoveringRect(ra, rb)) {
							const u64 freq = profiler::frequency();
							const float t = 1000 * float((to - from) / double(freq));
							ImGui::BeginTooltip();
							ImGui::Text("%s (%.3f ms)", data.name, t);
							if (data.profiler_link) {
								ImGui::Text("Link: %" PRId64, data.profiler_link);
								hovered_link.frame = frame_id;
								hovered_link.link = data.profiler_link;
							}
							if (has_stats) {
								char tmp[32];
								toCStringPretty(primitives_generated, Span(tmp));
								ImGui::Text("Primitives: %s", tmp);
							}
							ImGui::EndTooltip();
						}
					}
					if (open_blocks.size() > 0) open_blocks.pop();
					has_stats = false;
					break;
				case profiler::EventType::GPU_STATS:
					read(ctx, p + sizeof(profiler::EventHeader), primitives_generated);
					has_stats = true;
					break;
				case profiler::EventType::FRAME:
					if (header.time >= view_start && header.time <= m_end && m_show_frames) {
						const float t = float((header.time - view_start) / double(m_range));
						const float x = from_x * (1 - t) + to_x * t;
						dl->ChannelsSetCurrent(1);
						dl->AddLine(ImVec2(x, from_y), ImVec2(x, before_gpu_y), 0xffff0000);
						dl->ChannelsSetCurrent(0);
					}
					break;
				case profiler::EventType::CONTEXT_SWITCH:
					if (m_show_context_switches && header.time >= view_start && header.time <= m_end) {
						profiler::ContextSwitchRecord r;
						read(ctx, p + sizeof(profiler::EventHeader), r);
						auto new_iter = m_threads.find(r.new_thread_id);
						auto old_iter = m_threads.find(r.old_thread_id);
						const float x = get_view_x(header.time);

						if (new_iter.isValid()) draw_cswitch(x, r, new_iter.value(), true);
						if (old_iter.isValid()) draw_cswitch(x, r, old_iter.value(), false);
					}
					break;
				case profiler::EventType::COUNTER:
				case profiler::EventType::PAUSE:
					break;
				default: ASSERT(false); break;
			}
			p += header.size;
		}

		if (gpu_open) {
			if (lines > 0) ImGui::Dummy(ImVec2(to_x - from_x, lines * line_height));
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

	for (ThreadRecord& tr : m_threads) {
		if (tr.last_context_switch.is_enter) {
			const float x = get_view_x(tr.last_context_switch.time);
			dl->AddLine(ImVec2(x + 10, tr.y + 10), ImVec2(x, tr.y + 10), 0xffffff00);
		}
		tr.last_context_switch.time = 0;
	}

	dl->ChannelsMerge();
}


UniquePtr<ProfilerUI> ProfilerUI::create(StudioApp& app)
{
	Engine& engine = app.getEngine();
	debug::Allocator* allocator = engine.getAllocator().isDebug() ? static_cast<debug::Allocator*>(&engine.getAllocator()) : nullptr;
	return UniquePtr<ProfilerUIImpl>::create(engine.getAllocator(), app, allocator, engine);
}


} // namespace Lumix