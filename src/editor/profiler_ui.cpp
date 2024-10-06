#include <imgui/imgui.h>

#include "core/arena_allocator.h"
#include "core/atomic.h"
#include "core/command_line_parser.h"
#include "core/crt.h"
#include "core/debug.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/math.h"
#include "core/os.h"
#include "core/page_allocator.h"
#include "core/profiler.h"
#include "core/stack_array.h"
#include "core/string.h"
#include "core/tag_allocator.h"
#include "editor/asset_browser.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/resource_manager.h"
#include "engine/resource.h"
#include "profiler_ui.h"


namespace Lumix {

namespace {

static constexpr i32 DEFAULT_RANGE = 100'000;

// stores only visible data 
struct ThreadData {
	struct Rect {
		i32 id;
		u64 start_time;
		u64 end_time;
		u32 line;
		i32 first_property;
		u32 num_properties = 0;
	};

	struct Signal {
		u64 time;
		i32 signal;
		u32 line;
	};

	struct FiberWait : profiler::FiberWaitRecord {
		u64 time;
		u32 line;
		bool is_begin;
	};

	struct GPUBlock {
		StaticString<32> name;
		i64 profiler_link;
		u64 start;
		u64 end;
		i64 primitives_generated = -1;
		u32 line;
	};

	ThreadData(IAllocator& allocator, u32 thread_id, const char* name, bool show)
		: rects(allocator)
		, signals(allocator)
		, fiber_waits(allocator)
		, properties(allocator)
		, frames(allocator)
		, context_switches(allocator)
		, gpu_blocks(allocator)
		, show(show)
		, name(name)
		, thread_id(thread_id)
	{}

	u32 thread_id;
	const char* name;
	bool show; // some threads are hidden by default
	bool open = false; // is treenode open
	u32 lines = 0;
	u64 last_cswitch_time = 0;
	bool last_cswitch_is_enter;
	float y;

	// visible stuff
	Array<u32> properties;
	Array<FiberWait> fiber_waits;
	Array<Rect> rects;
	Array<Signal> signals;
	Array<u64> frames;
	Array<u32> context_switches;
	Array<GPUBlock> gpu_blocks;
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

const char* getContexSwitchReasonString(i8 reason) {
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

const char* toString(Resource::State state) {
	switch (state) {
		case Resource::State::EMPTY: return "Empty";
		case Resource::State::FAILURE: return "Failure";
		case Resource::State::READY: return "Ready";
	}
	return "Unknown";
}

template <typename T>
void read(const ThreadContextProxy& ctx, u32 p, T& value) {
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

void read(const ThreadContextProxy& ctx, u32 p, u8* ptr, int size) {
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
void overwrite(ThreadContextProxy& ctx, u32 p, const T& v) {
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

struct AllocationTag {
	AllocationTag(const TagAllocator* tag_allocator, IAllocator& allocator)
		: m_allocations(allocator)
		, m_child_tags(allocator)
		, m_tag(tag_allocator->m_tag, allocator)
		, m_tag_allocator((uintptr)tag_allocator)
	{}

	struct Allocation {
		debug::StackNode* stack_node;
		size_t size;
		u32 count = 1;
	};

	Array<AllocationTag> m_child_tags;
	Array<Allocation> m_allocations;
	String m_tag;
	uintptr m_tag_allocator; // don't deref
	size_t m_size = 0;
	size_t m_exclusive_size = 0;
};

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

struct Block {
	Block() {
		job_info.signal_on_finish = 0;
	}

	const char* name;
	i64 link = 0;
	u32 color = 0xffdddddd;
	profiler::JobRecord job_info;
};

struct ProfilerUIImpl final : StudioApp::GUIPlugin {
	ProfilerUIImpl(StudioApp& app, debug::Allocator* allocator, Engine& engine)
		: m_allocator(engine.getAllocator(), "profiler ui")
		, m_debug_allocator(allocator)
		, m_app(app)
		, m_threads(m_allocator)
		, m_data(m_allocator)
		, m_blocks(m_allocator)
		, m_counters(m_allocator)
		// we can't use m_allocator for tags, because it would create circular dependency and deadlock
		, m_allocation_tags(getGlobalAllocator())
		, m_engine(engine)
	{
		m_current_frame = -1;
		m_is_open = false;
		m_is_paused = true;

		m_app.getSettings().registerPtr("profiler_open", &m_is_open);
	}

	void onPause() {
		ASSERT(m_is_paused);
		m_data.clear();
		profiler::serialize(m_data);
		patchStrings();
		findEnd();
		preprocess();
		cacheVisibleBlocks();
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

	void timeline(float from_x, float to_x, float top, float bottom, u64 start_t) {
		const float view_length_us = (float)1'000'000 * float(m_range / double(profiler::frequency()));
		const ImColor border_color(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
		
		ImDrawList* dl = ImGui::GetWindowDrawList();
		float steps = (to_x - from_x) / 100;
		float step_len_us = view_length_us / steps;
		if (step_len_us > 2000) {
			step_len_us = floorf(step_len_us / 1000) * 1000;
		}
		steps = view_length_us / step_len_us;
		const float step_w = (to_x - from_x) / steps;

		const u64 view_start = m_end - m_range;
		const float start_x = from_x - i64(view_start - start_t) / float(m_range) * (to_x - from_x);
		float x = start_x;
		float t = 0;
		
		if (x < from_x) {
			const u32 presteps = u32((from_x - x) / step_w);
			x += step_w * presteps;
			t += step_len_us * presteps;
		}

		while (x < to_x) {
			const ImVec2 p(x, top);
			if (step_len_us < 2000) {
				StaticString<64> text(u32(t), "us");
				dl->AddText(ImVec2(1, 0) + p, border_color, text);
			}
			else {
				StaticString<64> text(u32(t / 1000), "ms");
				dl->AddText(ImVec2(1, 0) + p, border_color, text);
			}
			dl->AddLine(p, ImVec2(p.x, bottom), border_color);
			x += step_w;
			t += step_len_us;
		}

		x = start_x - step_w;
		t = -step_len_us;
		if (x > to_x) {
			const u32 presteps = u32((x - to_x) / step_w);
			x -= step_w * presteps;
			t -= step_len_us * presteps;
		}

		while (x > from_x) {
			const ImVec2 p(x, top);
			if (step_len_us < 2000) {
				StaticString<64> text(i32(t), "us");
				dl->AddText(ImVec2(1, 0) + p, border_color, text);
			}
			else {
				StaticString<64> text(i32(t / 1000), "ms");
				dl->AddText(ImVec2(1, 0) + p, border_color, text);
			}
			dl->AddLine(p, ImVec2(p.x, bottom), border_color);
			x -= step_w;
			t -= step_len_us;
		}
	}

	void countersUI(float from_x, float to_x) {
		if (!ImGui::TreeNode("Counters")) return;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		
		for (Counter& counter : m_counters) {
			const char* name = counter.name;
			if (!m_filter.pass(name)) continue;
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
		char path[MAX_PATH];
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
		if (!m_threads.find(0).isValid()) {
			auto iter = m_threads.insert(0, ThreadData(m_allocator, 0, "Global", true));
		}
		u32 p = global.begin;
		const u32 end = global.end;
		while (p != end) {
			profiler::EventHeader header;
			read(global, p, header);
			if (header.type == profiler::EventType::COUNTER) {
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
							if (startsWith(b.name, "Lumix::")) b.name += 7;
							else if (startsWith(b.name, "`anonymous-namespace'::")) b.name += 23;
						}
						break;
					}
					default: break;
				}
				p += header.size;
			}
		});
	}

	template <typename F> void forEachThread(const F& f) {
		if (m_data.empty()) return;

		InputMemoryStream blob(m_data);
		const u32 version = blob.read<u32>();
		ASSERT(version == 0);
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
		char path[MAX_PATH];
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

		if (m_app.checkShortcut(m_toggle_ui, true)) m_is_open = !m_is_open;

		if (!m_is_open) return;
		if (ImGui::Begin(ICON_FA_CHART_AREA "Profiler##profiler", &m_is_open))
		{
			if (ImGui::BeginTabBar("tb")) {
				if (ImGui::BeginTabItem("GPU/CPU")) {
					CPUGPUProfilerUI();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Memory")) {
					memoryProfilerUI();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Resources")) {
					resourcesUI();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
			++m_frame_idx;
		}
		ImGui::End();
	}

	const char* getName() const override { return "profiler"; }
	void toggleUI() { m_is_open = !m_is_open; }
	bool isOpen() { return m_is_open; }

	const char* getThreadName(u32 thread_id) const {
		auto iter = m_threads.find(thread_id);
		return iter.isValid() ? iter.value().name : "Unknown";
	}

	void resourcesUI() {
		if (m_app.checkShortcut(m_focus_filter)) ImGui::SetKeyboardFocusHere();
		m_resource_filter.gui("Filter", -1, false, &m_focus_filter);
	
		ImGuiEx::Label("Filter size (KB)");
		ImGui::DragScalar("##fs", ImGuiDataType_U64, &m_resource_size_filter, 1000);

		static const struct {
			ResourceType type;
			const char* name;
		} RESOURCE_TYPES[] = { 
			{ ResourceType("animation"), "Animations" },
			{ ResourceType("material"), "Materials" },
			{ ResourceType("model"), "Models" },
			{ ResourceType("physics_geometry"), "Physics geometries" },
			{ ResourceType("physics_material"), "Physics materials" },
			{ ResourceType("shader"), "Shaders" },
			{ ResourceType("texture"), "Textures" }
		};
		ImGui::Indent();
		for (u32 i = 0; i < lengthOf(RESOURCE_TYPES); ++i)
		{
			if (!ImGui::CollapsingHeader(RESOURCE_TYPES[i].name)) continue;

			ResourceManager* resource_manager = m_engine.getResourceManager().get(RESOURCE_TYPES[i].type);
			ResourceManager::ResourceTable& resources = resource_manager->getResourceTable();

			if (ImGui::BeginTable("resc", 4)) {
				ImGui::TableSetupColumn("Path");
				ImGui::TableSetupColumn("Size");
				ImGui::TableSetupColumn("State");
				ImGui::TableSetupColumn("References");
				ImGui::TableHeadersRow();

				size_t sum = 0;
				for (const Resource* res  : resources) {
					if (res->isEmpty()) continue;
					if (!m_resource_filter.pass(res->getPath())) continue;
					if (m_resource_size_filter > res->getFileSize() / 1024) continue;
				
					ImGui::TableNextColumn();
					ImGui::PushID(res);
					if (ImGuiEx::IconButton(ICON_FA_BULLSEYE, "Go to")) {
						m_app.getAssetBrowser().openEditor(res->getPath());
					}
					ImGui::PopID();
					ImGui::SameLine();
					ImGuiEx::TextUnformatted(res->getPath());
					ImGui::TableNextColumn();
					ImGui::Text("%.3fKB", res->getFileSize() / 1024.0f);
					sum += res->getFileSize();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(toString(res->getState()));
					ImGui::TableNextColumn();
					ImGui::Text("%u", res->getRefCount());
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

	static void callstackTooltip(debug::StackNode* n) {
		if (!ImGui::BeginTooltip()) return;

		ImGui::TextUnformatted("Callstack:");
		while (n) {
			char fn_name[256];
			i32 line;
			if (debug::StackTree::getFunction(n, Span(fn_name), line)) {
				ImGui::Text("%s: %d", fn_name, line);
			}
			else {
				ImGui::TextUnformatted("N/A");
			}
			n = debug::StackTree::getParent(n);
		}
		ImGui::EndTooltip();
	}

	void gui(const AllocationTag& tag) {
		if (m_filter.isActive()) {
			for (const AllocationTag& child : tag.m_child_tags) gui(child);

			if (!m_filter.pass(tag.m_tag)) return;
		}
		if (ImGui::TreeNode(&tag, "%s - %.2f MB", tag.m_tag.c_str(), tag.m_size / 1024.f / 1024.f)) {
			for (const AllocationTag& child : tag.m_child_tags) gui(child);
			if (tag.m_child_tags.empty() || m_filter.isActive() || ImGui::TreeNode("allocs", "Allocations - %.1f MB", tag.m_exclusive_size / 1024.f / 1024.f)) {
				ImGui::Columns(3);
				ImGuiListClipper clipper;
				clipper.Begin(tag.m_allocations.size());
				while (clipper.Step()) {
					for (int j = clipper.DisplayStart; j < clipper.DisplayEnd; ++j) {
						const AllocationTag::Allocation& a = tag.m_allocations[j];
						char fn_name[256] = "N/A";
						i32 line;
						debug::StackNode* n = a.stack_node;
						do {
							if (!debug::StackTree::getFunction(n, Span(fn_name), line)) {
								copyString(fn_name, "N/A");
								break;
							}
							n = debug::StackTree::getParent(n);
						} while (n && strstr(fn_name, "Allocator::") != 0);
						if (startsWith(fn_name, "Lumix::")) {
							ImGui::Text("%s: L%d:", fn_name + 7, line);
						}
						else {
							ImGui::Text("%s: L%d:", fn_name, line);
						}
						if (ImGui::IsItemHovered()) callstackTooltip(a.stack_node);
						ImGui::NextColumn();
						ImGui::Text("%.3f kB", a.size / 1024.f);
						ImGui::NextColumn();
						ImGui::Text("%d", a.count);
						ImGui::NextColumn();
					}
				}
				ImGui::Columns();
				if (!tag.m_child_tags.empty() && !m_filter.isActive()) ImGui::TreePop();
			}
			ImGui::TreePop();
		}
	}

	void memoryProfilerUI() {
		if (!m_debug_allocator) {
			ImGui::TextUnformatted("Debug allocator not used, can't print memory stats.");
			return;
		}

		if (ImGui::Button("Capture")) captureAllocations();
		ImGui::SameLine();
		if (ImGui::Button("Check memory")) m_debug_allocator->checkGuards();
		ImGui::SameLine();

		size_t total = 0;
		if (m_app.checkShortcut(m_focus_filter)) ImGui::SetKeyboardFocusHere();
		m_filter.gui("Filter", 150, false, &m_focus_filter);
		for (AllocationTag& tag : m_allocation_tags) {
			total += tag.m_size;
			gui(tag);
		}
		ImGui::Separator();
		ImGui::Text("Total: %d MB", u32(total / 1024 / 1024));
		const u32 reserved_pages = m_app.getEngine().getPageAllocator().getReservedCount() * PageAllocator::PAGE_SIZE;
		ImGui::Text("Page allocator: %.1f MB", reserved_pages / 1024.f / 1024.f);
		ImGui::Text("Arena allocators: %.1f MB", ArenaAllocator::getTotalCommitedBytes() / 1024.f / 1024.f);
		ImGui::Text("Profiler contexts: %.1f MB", profiler::getThreadContextMemorySize() / 1024.f / 1024.f);
		// TODO gpu mem
	}

	void profileStart() {
		static bool done = false;
		if (done) return;
		done = true;

		if (CommandLineParser::isOn("-profile_start")) {
			m_is_paused = true;
			onPause();
		}
	}

	void cacheVisibleBlocks() {
		const u64 from_time = m_end - m_range;
		const u64 to_time = m_end;
		forEachThread([&](ThreadContextProxy& ctx){
			auto iter = m_threads.find(ctx.thread_id);
			if (!iter.isValid()) return;
			if (!iter.value().open && ctx.thread_id != 0) return;
			ASSERT(iter.isValid());
			ThreadData& thread = iter.value();
			thread.rects.clear();
			thread.signals.clear();
			thread.fiber_waits.clear();
			thread.frames.clear();
			thread.context_switches.clear();
			thread.gpu_blocks.clear();
			struct OpenBlock {
				i32 id;
				u64 start_time;
			};
			struct Property {
				u32 offset;
				u32 level;
			};
			StackArray<u32, 16> open_gpu_blocks(m_allocator);
			StackArray<OpenBlock, 16> open_blocks(m_allocator);
			StackArray<Property, 16> properties(m_allocator);
			u32 lines = 0;
			u32 line = 0;
			float top = 0;
			u32 p = ctx.begin;
			u64 primitives_generated = 0;
			i32 gpu_stats_line = -1;
			while (p != ctx.end) {
				profiler::EventHeader header;
				read(ctx, p, header);
				switch (header.type) {
					case profiler::EventType::GPU_STATS: {
						read(ctx, p + sizeof(profiler::EventHeader), primitives_generated);
						gpu_stats_line = open_gpu_blocks.size() - 1;
						break;
					}
					case profiler::EventType::END_GPU_BLOCK: {
						if (open_gpu_blocks.empty()) break;
						profiler::GPUBlock r;
						u64 to;
						read(ctx, open_gpu_blocks.last() + sizeof(profiler::EventHeader), r);
						read(ctx, p + sizeof(profiler::EventHeader), to);
						open_gpu_blocks.pop();
						
						if (to < from_time || r.timestamp > m_end) break;
						ThreadData::GPUBlock gpu_block = {
							.name = r.name,
							.profiler_link = r.profiler_link,
							.start = r.timestamp,
							.end = to,
							.primitives_generated = i64(gpu_stats_line == (i32)open_gpu_blocks.size() ? primitives_generated : -1),
							.line = (u32)open_gpu_blocks.size()
						};
						thread.gpu_blocks.push(gpu_block); 
						if (open_gpu_blocks.size() <= gpu_stats_line) gpu_stats_line = -1;
						break;
					}
					case profiler::EventType::BEGIN_GPU_BLOCK:
						open_gpu_blocks.push(p);
						lines = maximum(lines, open_gpu_blocks.size());
						break;
					case profiler::EventType::CONTEXT_SWITCH:
						if (header.time < from_time || header.time > to_time) break;
						thread.context_switches.push(p);
						break;
					case profiler::EventType::FRAME:
						if (header.time < from_time || header.time > to_time) break;
						thread.frames.push(header.time);
						break;
					case profiler::EventType::STRING:
					case profiler::EventType::INT:
						properties.push({p, line});
						break;
					case profiler::EventType::JOB_INFO:
						if (open_blocks.size() > 0) {
							read(ctx, p + sizeof(profiler::EventHeader), m_blocks[open_blocks.last().id].job_info);
						}
						break;
					case profiler::EventType::LINK:
						if (open_blocks.size() > 0) {
							read(ctx, p + sizeof(profiler::EventHeader), m_blocks[open_blocks.last().id].link);
						}
						break;
					case profiler::EventType::END_FIBER_WAIT:
					case profiler::EventType::BEGIN_FIBER_WAIT: {
						if (header.time < from_time || header.time > to_time) break;

						const bool is_begin = header.type == profiler::EventType::BEGIN_FIBER_WAIT;
						profiler::FiberWaitRecord r;
						read(ctx, p + sizeof(profiler::EventHeader), r);

						ThreadData::FiberWait& wait = thread.fiber_waits.emplace();
						wait.id = r.id;
						wait.is_mutex = r.is_mutex;
						wait.job_system_signal = r.job_system_signal;
						wait.time = header.time;
						wait.line = line;
						wait.is_begin = is_begin;
						break;
					}
					case profiler::EventType::SIGNAL_TRIGGERED: {
						const bool in_view = header.time >= from_time && header.time <= to_time;
						if (in_view) {
							ThreadData::Signal& signal = thread.signals.emplace();
							read(ctx, p + sizeof(profiler::EventHeader), signal.signal);
							signal.time = header.time;
							signal.line = line;
						}
						break;
					}
					case profiler::EventType::BEGIN_BLOCK:
						profiler::BlockRecord tmp;
						read(ctx, p + sizeof(profiler::EventHeader), tmp);
						open_blocks.push({tmp.id, header.time});
						lines = maximum(lines, open_blocks.size());
						++line;
						break;
					case profiler::EventType::CONTINUE_BLOCK: {
						i32 id;
						read(ctx, p + sizeof(profiler::EventHeader), id);
						open_blocks.push({id, header.time});
						lines = maximum(lines, open_blocks.size());
						++line;
						break;
					}
					case profiler::EventType::END_BLOCK:
						if (line > 0) --line;
						if (open_blocks.size() > 0) {
							const Block& block = m_blocks[open_blocks.last().id];
							const bool in_view = open_blocks.last().start_time < to_time && header.time > from_time;
							if (in_view) {
								ThreadData::Rect& r = thread.rects.emplace();
								r.id = open_blocks.last().id;
								r.start_time = open_blocks.last().start_time;
								r.end_time = header.time;
								r.line = line;
								if (!properties.empty()) {
									r.first_property = thread.properties.size();
									r.num_properties = properties.size();
									for (i32 i = properties.size() - 1; i >= 0; --i) {
										const Property& prop = properties[i];
										if (prop.level == line + 1) {
											thread.properties.push(prop.offset);
											properties.pop();
										}
									}
								}
							}
							for (i32 i = properties.size() - 1; i >= 0; --i) {
								const Property& prop = properties[i];
								if (prop.level == line + 1) {
									properties.pop();
								}
								else break;
							}
							open_blocks.pop();
						}
						break;
					case profiler::EventType::BLOCK_COLOR:
						if (open_blocks.size() > 0) {
							read(ctx, p + sizeof(profiler::EventHeader), m_blocks[open_blocks.last().id].color);
						}
						break;
					default: break;
				}
				p += header.size;
			}

			// close all open blocks
			while (open_blocks.size() > 0) {
				--line;
				const Block& block = m_blocks[open_blocks.last().id];
				const bool in_view = open_blocks.last().start_time < to_time;
				if (in_view) {
					ThreadData::Rect& r = thread.rects.emplace();
					r.id = open_blocks.last().id;
					r.start_time = open_blocks.last().start_time;
					r.end_time = m_end;
					r.line = line;
				}
				open_blocks.pop();
			}

			thread.lines = lines;
		});
	}

	void CPUGPUProfilerUI() {
		profileStart();

		if (m_autopause > 0 && !m_is_paused && profiler::getLastFrameDuration() * 1000.f > m_autopause) {
			m_is_paused = true;
			profiler::pause(m_is_paused);
			onPause();
		}

		if (ImGui::Button(m_is_paused ? ICON_FA_PLAY : ICON_FA_PAUSE) || m_app.checkShortcut(m_play_pause)) {
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
				#ifdef _WIN32
					ImGui::Text("Context switch tracing not available.");
					ImGui::Text("Run the app as an administrator");
					ImGui::Text("and use -profile_cswitch command line option");
				#else
					ImGui::Text("Context switch tracing not available on this platform.");
				#endif
			}
			ImGui::EndPopup();
		}
		ImGui::SameLine();
		if (m_app.checkShortcut(m_focus_filter)) ImGui::SetKeyboardFocusHere();
		m_filter.gui("Filter", 150, false, &m_focus_filter);
		ImGui::SameLine();
		if (m_filter.isActive()) {
			ImGui::SameLine();
			ImGui::Text("%f ms (%d calls) / ", (float)m_filtered_time, m_filtered_count);
		}
		const u64 freq = profiler::frequency();
		ImGui::SameLine();
		ImGui::Text("%f ms", (float)1000 * float(m_range / double(freq)));

		if (m_data.empty()) return;

		const float timeline_y = ImGui::GetCursorScreenPos().y;
		ImGui::Dummy(ImVec2(-1, ImGui::GetTextLineHeightWithSpacing())); // reserve space for timeline
		const float from_y = ImGui::GetCursorScreenPos().y;
		const float from_x = ImGui::GetCursorScreenPos().x;
		const float to_x = from_x + ImGui::GetContentRegionAvail().x;
		const u64 view_start = m_end - m_range;
		u64 timeline_start_t = view_start;
		float before_gpu_y = from_y;
		if (ImGui::BeginChild("cpu_gpu")) {
			countersUI(from_x, to_x);
			auto get_view_x = [&](u64 time) {
				const float t = time > view_start
					? float((time - view_start) / double(m_range))
					: -float((view_start - time) / double(m_range));
				return from_x * (1 - t) + to_x * t;
			};

			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->ChannelsSplit(2);
			const float line_height = ImGui::GetTextLineHeightWithSpacing();

			m_filtered_time = 0;
			m_filtered_count = 0;

			forEachThread([&](const ThreadContextProxy& ctx) {
				if (ctx.thread_id == 0) return;
				auto thread_iter = m_threads.find(ctx.thread_id);
				if (!thread_iter.isValid()) {
					thread_iter = m_threads.insert(ctx.thread_id, ThreadData(m_allocator, ctx.thread_id, ctx.name, ctx.default_show));
				}

				ThreadData& thread_record = thread_iter.value();
				thread_record.y = ImGui::GetCursorScreenPos().y;
				
				if (!thread_record.show) return;
				if (!ImGui::TreeNode(ctx.buffer, "%s", ctx.name)) return;
				if (!thread_record.open) {
					thread_record.open = true;
					cacheVisibleBlocks();
				}

				ThreadData& thread = m_threads[ctx.thread_id];
				const float thread_base_y = ImGui::GetCursorScreenPos().y;
				
				// fiber wait
				for (ThreadData::FiberWait& wait : thread.fiber_waits) {
					const float x = get_view_x(wait.time);
					const float y = thread_base_y + wait.line * line_height;
					const u32 color = wait.is_mutex ? 0xff0000ff : wait.is_begin ? 0xff00ff00 : 0xffff0000;
					dl->AddRect(ImVec2(x - 2, y - 2), ImVec2(x + 2, y + 2), color);
					const bool mouse_hovered = ImGui::IsMouseHoveringRect(ImVec2(x - 2, y - 2), ImVec2(x + 2, y + 2));
					
					if (m_hovered_job.signal == wait.job_system_signal && m_hovered_job.frame > m_frame_idx - 2) {
						dl->ChannelsSetCurrent(1);
						dl->AddLine(ImVec2(x, y - 2), m_hovered_job.pos, 0xff0000ff);
						dl->ChannelsSetCurrent(0);
					}

					if (m_hovered_fiber_wait.id == wait.id && m_hovered_fiber_wait.frame > m_frame_idx - 2) {
						dl->ChannelsSetCurrent(1);
						dl->AddLine(ImVec2(x, y - 2), m_hovered_fiber_wait.pos, 0xff00ff00);
						dl->ChannelsSetCurrent(0);
					}

					if (m_hovered_signal_trigger.signal == wait.job_system_signal && m_hovered_signal_trigger.frame > m_frame_idx - 2) {
						dl->ChannelsSetCurrent(1);
						dl->AddLine(ImVec2(x, y - 2), m_hovered_signal_trigger.pos, 0xff0000ff);
						dl->ChannelsSetCurrent(0);
					}

					if (mouse_hovered) {
						m_hovered_fiber_wait.frame = m_frame_idx;
						m_hovered_fiber_wait.id = wait.id;
						m_hovered_fiber_wait.pos = ImVec2(x, y);
						m_hovered_fiber_wait.signal = wait.job_system_signal;

						ImGui::BeginTooltip();
						ImGui::Text("Fiber wait");
						ImGui::Text("  Wait ID: %d", wait.id);
						ImGui::Text("  Waiting for signal: %" PRIx64, (u64)wait.job_system_signal);
						ImGui::EndTooltip();
					}
				}

				// signals
				for (ThreadData::Signal& signal : thread.signals) {
					const float x = get_view_x(signal.time);
					const float y = thread_base_y + signal.line * line_height;
			
					if (m_hovered_fiber_wait.signal == signal.signal && m_hovered_fiber_wait.frame > m_frame_idx - 2) {
						dl->ChannelsSetCurrent(1);
						dl->AddLine(m_hovered_fiber_wait.pos, ImVec2(x, y), 0xff0000ff);
						dl->ChannelsSetCurrent(0);
					}

					dl->AddTriangle(ImVec2(x - 2, y), ImVec2(x + 2, y - 2), ImVec2(x + 2, y + 2), 0xffffff00);
					
					if (ImGui::IsMouseHoveringRect(ImVec2(x - 2, y - 2), ImVec2(x + 2, y + 2))) {
						ImGui::BeginTooltip();
						ImGui::Text("Signal triggered: %" PRIx64, (u64)signal.signal);
						ImGui::EndTooltip();

						m_hovered_signal_trigger.signal = signal.signal;
						m_hovered_signal_trigger.frame = m_frame_idx;
						m_hovered_signal_trigger.pos = ImVec2(x, y);
					}
				};
				
				// blocks
				for (ThreadData::Rect& r : thread.rects) {
					const Block& block = m_blocks[r.id];
					const float block_y = thread_base_y + r.line * line_height;
					const float text_width = ImGui::CalcTextSize(block.name).x;
					float x_start =  get_view_x(r.start_time);
					float x_end = get_view_x(r.end_time);
					if (x_end <= x_start + 0.999f) x_end += 1; 
					const ImVec2 ra(x_start, block_y);
					const ImVec2 rb(x_end, block_y + line_height - 1);
					const bool is_hovered = ImGui::IsMouseHoveringRect(ra, rb);
					const bool is_filtered = !m_filter.pass(block.name);
					const u32 alpha = is_filtered ? 0x2000'0000 : 0xff00'0000;
					
					u32 fill_color = block.color;
					if (is_filtered) {
						ImColor tmp_c(fill_color);
						tmp_c.Value.x *= 0.5f;
						tmp_c.Value.y *= 0.5f;
						tmp_c.Value.z *= 0.5f;
						fill_color = tmp_c;
					}
					if (m_hovered_link.link == block.link && m_hovered_link.frame > m_frame_idx - 2) fill_color = 0xff0000ff;

					if (m_hovered_fiber_wait.signal == block.job_info.signal_on_finish && m_hovered_fiber_wait.frame > m_frame_idx - 2) {
							dl->ChannelsSetCurrent(1);
						dl->AddLine(m_hovered_fiber_wait.pos, ImVec2(x_start, block_y), 0xff0000ff);
						dl->ChannelsSetCurrent(0);
					}

					// draw block
					dl->AddRectFilled(ra, rb, fill_color);
					if (x_end - x_start > 2) {
						const u32 hovered_color = ImGui::GetColorU32(ImGuiCol_ButtonActive);
						u32 border_color = ImGui::GetColorU32(ImGuiCol_Border);
						border_color = alpha | (border_color & 0x00ffffff);
						dl->AddRect(ra, rb, is_hovered ? hovered_color : border_color);
					}
					
					if (text_width + 2 < x_end - x_start) {
						dl->AddText(ImVec2(x_start + 2, block_y), 0x00000000 | alpha, block.name);
					}

					const float duration = 1000 * float((r.end_time - r.start_time) / double(freq));
					if (!is_filtered && m_filter.isActive()) {
						m_filtered_time += duration;
						++m_filtered_count;
					}

					// tooltip
					if (is_hovered) {
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
							copyString(m_filter.filter, block.name);
							m_filter.build();
						}
						m_hovered_block.id = r.id;
						m_hovered_block.frame = m_frame_idx;
						
						ImGui::BeginTooltip();
						ImGui::Text("%s (%.4f ms)", block.name, duration);
						if (block.link) {
							ImGui::Text("Link: %" PRId64, block.link);
							m_hovered_link.frame = m_frame_idx;
							m_hovered_link.link = block.link;
						}
						if (block.job_info.signal_on_finish) {
							ImGui::Text("Signal on finish: %" PRIx64, (u64)block.job_info.signal_on_finish);
							m_hovered_job.frame = m_frame_idx;
							m_hovered_job.pos = ImVec2(x_start, block_y);
							m_hovered_job.signal = block.job_info.signal_on_finish;
						}
						if (r.num_properties > 0) {
							for (u32 i = 0; i < r.num_properties; ++i) {
								const u32 offset = thread.properties[r.first_property + i];
								profiler::EventHeader prop;
								read(ctx, offset, prop);
								switch (prop.type) {
									case profiler::EventType::INT: {
										profiler::IntRecord int_record;
										read(ctx, offset + sizeof(profiler::EventHeader), int_record);
										ImGui::Text("%s: %d", int_record.key, int_record.value);
										break;
									}
									case profiler::EventType::STRING: {
										char tmp[128];
										const int tmp_size = prop.size - sizeof(profiler::EventHeader);
										read(ctx, offset + sizeof(profiler::EventHeader), (u8*)tmp, tmp_size);
										ImGui::TextUnformatted(tmp);
										break;
									}
									default: ASSERT(false); break;
								}
							}
						}
						ImGui::EndTooltip();
					}
					
				}
				ImGui::Dummy(ImVec2(to_x - from_x, thread.lines * line_height));
				ImGui::TreePop();
			});

			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_hovered_block.frame < m_frame_idx - 2) {
				m_filter.clear();
			}

			auto draw_cswitch = [&](float x, const profiler::ContextSwitchRecord& r, ThreadData& tr, bool is_enter) {
				const float y = tr.y + 10;
				dl->AddLine(ImVec2(x + (is_enter ? -2.f : 2.f), y - 5), ImVec2(x, y), 0xff00ff00);
				if (!is_enter) {
					const u64 prev_switch = tr.last_cswitch_time;
					if (prev_switch) {
						if (tr.last_cswitch_is_enter) {
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
				tr.last_cswitch_time = r.timestamp;
				tr.last_cswitch_is_enter = is_enter;
			};

			before_gpu_y = ImGui::GetCursorScreenPos().y;
			
			ThreadData& global = m_threads[0];
			if (m_show_context_switches) {
				const ThreadContextProxy& ctx = getGlobalThreadContextProxy();
				for (u32 cs_offset : global.context_switches) {
					profiler::EventHeader header;
					read(ctx, cs_offset, header);
					profiler::ContextSwitchRecord r;
					read(ctx, cs_offset + sizeof(profiler::EventHeader), r);

					auto new_iter = m_threads.find(r.new_thread_id);
					auto old_iter = m_threads.find(r.old_thread_id);
					const float x = get_view_x(header.time);
					if (new_iter.isValid()) draw_cswitch(x, r, new_iter.value(), true);
					if (old_iter.isValid()) draw_cswitch(x, r, old_iter.value(), false);
				}
			}
			if (m_show_frames) {
				for (u64 f : global.frames) {
					if (timeline_start_t <= view_start) timeline_start_t = f;
					const float x = get_view_x(f);
					dl->ChannelsSetCurrent(1);
					dl->AddLine(ImVec2(x, from_y), ImVec2(x, before_gpu_y), 0xffff0000);
					dl->ChannelsSetCurrent(0);
				}
			}

			{
				const ThreadContextProxy& ctx = getGlobalThreadContextProxy();
				if (ImGui::TreeNode(ctx.buffer, "GPU")) {
					const float y = ImGui::GetCursorScreenPos().y;

					for (const ThreadData::GPUBlock& block : global.gpu_blocks) {
						const float x_start = get_view_x(block.start);
						const float x_end = get_view_x(block.end);
						const float block_y = y + block.line * line_height;
						const ImVec2 ra(x_start, block_y);
						const ImVec2 rb(x_end, block_y + line_height - 1);
						u32 color = 0xffDDddDD;
						if (m_hovered_link.link == block.profiler_link && m_hovered_link.frame > m_frame_idx - 2) color = 0xff0000ff;
						dl->AddRectFilled(ra, rb, color);
						if (x_end - x_start > 2) {
							dl->AddRect(ra, rb, ImGui::GetColorU32(ImGuiCol_Border));
						}
						const float text_width = ImGui::CalcTextSize(block.name).x;
						if (text_width + 2 < x_end - x_start) {
							dl->AddText(ImVec2(x_start + 2, block_y), 0xff000000, block.name);
						}
						if (ImGui::IsMouseHoveringRect(ra, rb)) {
							const float t = 1000 * float((block.end - block.start) / double(freq));
							ImGui::BeginTooltip();
							ImGui::Text("%s (%.4f ms)", block.name.data, t);
							if (block.profiler_link) {
								ImGui::Text("Link: %" PRId64, block.profiler_link);
								m_hovered_link.frame = m_frame_idx;
								m_hovered_link.link = block.profiler_link;
							}
							ImGui::EndTooltip();
						}
					}
					if (global.lines > 0) ImGui::Dummy(ImVec2(to_x - from_x, global.lines * line_height));
					ImGui::TreePop();
				}
						
				if (ImGui::IsMouseHoveringRect(ImVec2(from_x, from_y), ImVec2(to_x, ImGui::GetCursorScreenPos().y))) {
					if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
						m_end -= i64((ImGui::GetIO().MouseDelta.x / (to_x - from_x)) * m_range);
						cacheVisibleBlocks();
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
						cacheVisibleBlocks();
					}
				}
			}

			for (ThreadData& tr : m_threads) {
				if (tr.last_cswitch_is_enter) {
					const float x = get_view_x(tr.last_cswitch_time);
					dl->AddLine(ImVec2(x + 10, tr.y + 10), ImVec2(x, tr.y + 10), 0xffffff00);
				}
				tr.last_cswitch_time = 0;
			}

			dl->ChannelsMerge();
		}
		ImGui::EndChild();

		timeline(from_x, to_x, timeline_y, before_gpu_y, timeline_start_t);
	}

	AllocationTag& getTag(const TagAllocator* tag_allocator) {
		IAllocator* parent = tag_allocator->getParent();
		if (parent && parent->isTagAllocator()) {
			AllocationTag& parent_tag = getTag((TagAllocator*)parent);
			for (AllocationTag& tag : parent_tag.m_child_tags) {
				if (tag.m_tag_allocator == (uintptr)tag_allocator) return tag;
			}
			return parent_tag.m_child_tags.emplace(tag_allocator, getGlobalAllocator());
		}

		for (AllocationTag& tag : m_allocation_tags) {
			if (tag.m_tag_allocator == (uintptr)tag_allocator) return tag;
		}
		return m_allocation_tags.emplace(tag_allocator, getGlobalAllocator());
	}

	void postprocess(AllocationTag& tag) {
		tag.m_exclusive_size = tag.m_size;
		for (AllocationTag& child : tag.m_child_tags) {
			postprocess(child);
			tag.m_size += child.m_size;
		}

		qsort(tag.m_child_tags.begin(), tag.m_child_tags.size(), sizeof(tag.m_child_tags[0]), [](const void* a, const void* b){
			size_t sa = ((AllocationTag*)a)->m_size;
			size_t sb = ((AllocationTag*)b)->m_size;

			if (sa > sb) return -1;
			if (sa < sb) return 1;
			return 0;
		});

		qsort(tag.m_allocations.begin(), tag.m_allocations.size(), sizeof(tag.m_allocations[0]), [](const void* a, const void* b){
			const void* sa = ((AllocationTag::Allocation*)a)->stack_node;
			const void* sb = ((AllocationTag::Allocation*)b)->stack_node;

			if (sa > sb) return -1;
			if (sa < sb) return 1;
			return 0;
		});

		for (i32 i = tag.m_allocations.size() - 1; i > 0; --i) {
			if (tag.m_allocations[i].stack_node != tag.m_allocations[i - 1].stack_node) continue;

			tag.m_allocations[i - 1].size += tag.m_allocations[i].size;
			tag.m_allocations[i - 1].count += tag.m_allocations[i].count;
			tag.m_allocations.swapAndPop(i);
		}

		qsort(tag.m_allocations.begin(), tag.m_allocations.size(), sizeof(tag.m_allocations[0]), [](const void* a, const void* b){
			const size_t sa = ((AllocationTag::Allocation*)a)->size;
			const size_t sb = ((AllocationTag::Allocation*)b)->size;

			if (sa > sb) return -1;
			if (sa < sb) return 1;
			return 0;
		});
	}

	void captureAllocations() {
		if (!m_debug_allocator) return;

		m_allocation_tags.clear();

		m_debug_allocator->lock();
		auto* current_info = m_debug_allocator->getFirstAllocationInfo();

		while (current_info) {
			if (current_info->stack_leaf) {
				AllocationTag& tag = getTag(current_info->tag);
				AllocationTag::Allocation& a =  tag.m_allocations.emplace();
				a.size = current_info->size;
				a.stack_node = current_info->stack_leaf;
				tag.m_size += a.size;
			}
			current_info = current_info->next;
		}
		m_debug_allocator->unlock();

		for (AllocationTag& tag : m_allocation_tags) postprocess(tag);
	}

	StudioApp& m_app;
	TagAllocator m_allocator;
	debug::Allocator* m_debug_allocator;
	Array<AllocationTag> m_allocation_tags;
	int m_current_frame;
	bool m_is_paused;
	u64 m_end;
	u64 m_range = DEFAULT_RANGE;
	TextFilter m_filter;
	double m_filtered_time = 0;
	u32 m_filtered_count = 0;
	TextFilter m_resource_filter;
	u64 m_resource_size_filter = 0;
	Engine& m_engine;
	HashMap<u32, ThreadData> m_threads;
	OutputMemoryStream m_data;
	os::Timer m_timer;
	float m_autopause = -33.3333f;
	bool m_show_context_switches = false;
	bool m_show_frames = true;
	Array<Counter> m_counters;
	bool m_is_open = false;
	Action m_toggle_ui{"Profiler", "Profiler - toggle UI", "profiler_toggle_ui", "", Action::WINDOW};
	Action m_play_pause{"Play/pause", "Profiler - play/pause", "profiler_play_pause", ""};
	Action m_focus_filter{"Focus filter", "Profiler - focus filter", "profiler_focus_filter", ""};
	HashMap<i32, Block> m_blocks;
	u32 m_frame_idx = 0;

	struct {
		u32 frame = 0;
		i64 link;
	} m_hovered_link;

	struct {
		u32 frame = 0;
		i32 id = 0;
		ImVec2 pos;
		i32 signal = 0;
	} m_hovered_fiber_wait;

	struct {
		u32 frame = 0;
		i32 signal = 0;
		ImVec2 pos;
	} m_hovered_signal_trigger;

	struct {
		u32 frame = 0;
		i32 signal = 0;
		ImVec2 pos;
	} m_hovered_job;

	struct {
		u32 frame = 0;
		i32 id = -1;
	} m_hovered_block;
};

} // anonymous namespace


UniquePtr<StudioApp::GUIPlugin> createProfilerUI(StudioApp& app) {
	Engine& engine = app.getEngine();
	debug::Allocator* debug_allocator = nullptr;
	IAllocator* allocator = &engine.getAllocator();
	do {
		if (allocator->isDebug()) {
			debug_allocator = (debug::Allocator*)allocator;
			break;
		}
		allocator = allocator->getParent();
	} while(allocator);

	return UniquePtr<ProfilerUIImpl>::create(app.getAllocator(), app, debug_allocator, engine);
}


} // namespace Lumix