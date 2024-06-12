#include <imgui/imgui.h>

#include "core/command_line_parser.h"
#include "core/crt.h"
#include "core/debug.h"
#include "engine/file_system.h"
#include "core/arena_allocator.h"
#include "core/atomic.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/math.h"
#include "core/page_allocator.h"
#include "core/profiler.h"
#include "core/stack_array.h"
#include "core/string.h"
#include "core/tag_allocator.h"

#include "profiler_ui.h"
#include "editor/asset_browser.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "engine/engine.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"


namespace Lumix {

namespace {

static constexpr i32 DEFAULT_RANGE = 100'000;

struct ThreadRecord {
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

		m_toggle_ui.init("Profiler", "Toggle profiler UI", "profiler", "", Action::IMGUI_PRIORITY);
		m_toggle_ui.func.bind<&ProfilerUIImpl::toggleUI>(this);
		m_toggle_ui.is_selected.bind<&ProfilerUIImpl::isOpen>(this);

		m_app.addWindowAction(&m_toggle_ui);
	}

	~ProfilerUIImpl() {
		m_app.removeAction(&m_toggle_ui);
		while (m_engine.getFileSystem().hasWork()) {
			m_engine.getFileSystem().processCallbacks();
		}
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

		if (!m_is_open) return;
		if (ImGui::Begin(ICON_FA_CHART_AREA "Profiler##profiler", &m_is_open))
		{
			if (ImGui::BeginTabBar("tb")) {
				if (ImGui::BeginTabItem("GPU/CPU")) {
					onGUICPUProfiler();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Memory")) {
					onGUIMemoryProfiler();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Resources")) {
					onGUIResources();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
			++m_frame_idx;
		}
		ImGui::End();
	}

	const char* getName() const override { return "profiler"; }
	void onSettingsLoaded() override { m_is_open = m_app.getSettings().m_is_profiler_open; }
	void onBeforeSettingsSaved() override { m_app.getSettings().m_is_profiler_open  = m_is_open; }
	void toggleUI() { m_is_open = !m_is_open; }
	bool isOpen() { return m_is_open; }

	const char* getThreadName(u32 thread_id) const {
		auto iter = m_threads.find(thread_id);
		return iter.isValid() ? iter.value().name : "Unknown";
	}

	void onGUIResources() {
		m_resource_filter.gui("Filter");
	
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
				for (auto iter = resources.begin(), end = resources.end(); iter != end; ++iter) {
					if (!m_resource_filter.pass(iter.value()->getPath())) continue;
					if (m_resource_size_filter > iter.value()->getFileSize() / 1024) continue;
				
					ImGui::TableNextColumn();
					ImGui::PushID(iter.value());
					if (ImGuiEx::IconButton(ICON_FA_BULLSEYE, "Go to")) {
						m_app.getAssetBrowser().openEditor(iter.value()->getPath());
					}
					ImGui::PopID();
					ImGui::SameLine();
					ImGuiEx::TextUnformatted(iter.value()->getPath());
					ImGui::TableNextColumn();
					ImGui::Text("%.3fKB", iter.value()->getFileSize() / 1024.0f);
					sum += iter.value()->getFileSize();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(toString(iter.value()->getState()));
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

	void onGUIMemoryProfiler() {
		if (!m_debug_allocator) {
			ImGui::TextUnformatted("Debug allocator not used, can't print memory stats.");
			return;
		}

		if (ImGui::Button("Capture")) captureAllocations();
		ImGui::SameLine();
		if (ImGui::Button("Check memory")) m_debug_allocator->checkGuards();
		ImGui::SameLine();

		size_t total = 0;
		m_filter.gui("Filter", 150, false);
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

	void onGUICPUProfiler() {
		profileStart();

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
		m_filter.gui("Filter", 150, false);
		ImGui::SameLine();
		if (m_filter.isActive()) {
			ImGui::SameLine();
			ImGui::Text("%f ms (%d calls) / ", (float)m_filtered_time, m_filtered_count);
		}
		const u64 freq = profiler::frequency();
		ImGui::SameLine();
		ImGui::Text("%f ms", (float)1000 * float(m_range / double(freq)));

		if (m_data.empty()) return;
		if (!m_is_paused) return;

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
		
				struct Property {
					profiler::EventHeader header;
					int level;
					int offset;
				};
				StackArray<Property, 64> properties(m_allocator);

				auto draw_triggered_signal = [&](u64 time, i32 signal) {
					const float x = get_view_x(time);
			
					if (m_hovered_fiber_wait.signal == signal && m_hovered_fiber_wait.frame > m_frame_idx - 2) {
						dl->ChannelsSetCurrent(1);
						dl->AddLine(m_hovered_fiber_wait.pos, ImVec2(x, y), 0xff0000ff);
						dl->ChannelsSetCurrent(0);
					}

					if (time > m_end || time < view_start) return;
					dl->AddTriangle(ImVec2(x - 2, y), ImVec2(x + 2, y - 2), ImVec2(x + 2, y + 2), 0xffffff00);
					if (ImGui::IsMouseHoveringRect(ImVec2(x - 2, y - 2), ImVec2(x + 2, y + 2))) {
						ImGui::BeginTooltip();
						ImGui::Text("Signal triggered: %" PRIx64, (u64)signal);
						ImGui::EndTooltip();

						m_hovered_signal_trigger.signal = signal;
						m_hovered_signal_trigger.frame = m_frame_idx;
						m_hovered_signal_trigger.pos = ImVec2(x, y);
					}
				};

				auto draw_block = [&](u64 from, u64 to, const char* name, u32 color, i32 block_id) {
					const Block& block = m_blocks[open_blocks.last().id];
					if (m_hovered_fiber_wait.signal == block.job_info.signal_on_finish && m_hovered_fiber_wait.frame > m_frame_idx - 2) {
						const float x_start = get_view_x(from);
						dl->ChannelsSetCurrent(1);
						dl->AddLine(m_hovered_fiber_wait.pos, ImVec2(x_start, y), 0xff0000ff);
						dl->ChannelsSetCurrent(0);
					}
					auto draw_block_tooltip = [&](bool header){
						const float x_start = get_view_x(from);
						const float t = 1000 * float((to - from) / double(freq));
						ImGui::BeginTooltip();
						if (header) {
							ImGui::Text("%s (%.4f ms)", name, t);
							if (block.link) {
								ImGui::Text("Link: %" PRId64, block.link);
								m_hovered_link.frame = m_frame_idx;
								m_hovered_link.link = block.link;
							}
							if (block.job_info.signal_on_finish) {
								ImGui::Text("Signal on finish: %" PRIx64, (u64)block.job_info.signal_on_finish);
								m_hovered_job.frame = m_frame_idx;
								m_hovered_job.pos = ImVec2(x_start, y);
								m_hovered_job.signal = block.job_info.signal_on_finish;
							}
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
									ImGui::TextUnformatted(tmp);
									break;
								}
								default: ASSERT(false); break;
							}
						}
						ImGui::EndTooltip();
					};

					if (from > m_end || to < view_start) {
						if (m_hovered_block.id == block_id && m_hovered_block.frame > m_frame_idx - 2) {
							draw_block_tooltip(false);
						}
						return;
					}

					const float x_start = get_view_x(from);
					float x_end = get_view_x(to);
					if (int(x_end) == int(x_start)) ++x_end;
					const float block_y = y;
					const float w = ImGui::CalcTextSize(name).x;

					const bool is_filtered = !m_filter.pass(name);
					const u32 alpha = is_filtered ? 0x2000'0000 : 0xff00'0000;
					if (m_hovered_link.link == block.link && m_hovered_link.frame > m_frame_idx - 2) color = 0xff0000ff;
					if (is_filtered) {
						ImColor tmp_c(color);
						tmp_c.Value.x *= 0.5f;
						tmp_c.Value.y *= 0.5f;
						tmp_c.Value.z *= 0.5f;
						color = tmp_c;
					}
					const u32 hovered_color = ImGui::GetColorU32(ImGuiCol_ButtonActive);
					u32 border_color = ImGui::GetColorU32(ImGuiCol_Border);
					border_color = alpha | (border_color & 0x00ffffff);

					const ImVec2 ra(x_start, block_y);
					const ImVec2 rb(x_end, block_y + line_height - 1);

					bool is_hovered = false;
					if (ImGui::IsMouseHoveringRect(ra, rb)) {
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
							copyString(m_filter.filter, name);
							m_filter.build();
						}
						is_hovered = true;
						m_hovered_block.id = block_id;
						m_hovered_block.frame = m_frame_idx;
						draw_block_tooltip(true);
					}
					else if (m_hovered_block.id == block_id && m_hovered_block.frame > m_frame_idx - 2) {
						draw_block_tooltip(false);
					}

					dl->AddRectFilled(ra, rb, color);
					if (x_end - x_start > 2) {
						dl->AddRect(ra, rb, is_hovered ? hovered_color : border_color);
					}
					if (w + 2 < x_end - x_start) {
						dl->AddText(ImVec2(x_start + 2, block_y), 0x00000000 | alpha, name);
					}
					const float t = 1000 * float((to - from) / double(freq));
					if (!is_filtered && m_filter.isActive()) {
						m_filtered_time += t;
						++m_filtered_count;
					}
				};

				u32 p = ctx.begin;
				while (p != ctx.end) {
					profiler::EventHeader header;
					read(ctx, p, header);
					switch (header.type) {
						case profiler::EventType::END_FIBER_WAIT:
						case profiler::EventType::BEGIN_FIBER_WAIT: {
							const bool is_begin = header.type == profiler::EventType::BEGIN_FIBER_WAIT;
							profiler::FiberWaitRecord r;
							read(ctx, p + sizeof(profiler::EventHeader), r);
				
							const float x = get_view_x(header.time);

							if (m_hovered_job.signal == r.job_system_signal && m_hovered_job.frame > m_frame_idx - 2) {
								dl->ChannelsSetCurrent(1);
								dl->AddLine(ImVec2(x, y - 2), m_hovered_job.pos, 0xff0000ff);
								dl->ChannelsSetCurrent(0);
							}

							if (m_hovered_fiber_wait.id == r.id && m_hovered_fiber_wait.frame > m_frame_idx - 2) {
								dl->ChannelsSetCurrent(1);
								dl->AddLine(ImVec2(x, y - 2), m_hovered_fiber_wait.pos, 0xff00ff00);
								dl->ChannelsSetCurrent(0);
							}

							if (m_hovered_signal_trigger.signal == r.job_system_signal && m_hovered_signal_trigger.frame > m_frame_idx - 2) {
								dl->ChannelsSetCurrent(1);
								dl->AddLine(ImVec2(x, y - 2), m_hovered_signal_trigger.pos, 0xff0000ff);
								dl->ChannelsSetCurrent(0);
							}

							if (header.time >= view_start && header.time <= m_end) {
								const u32 color = r.is_mutex ? 0xff0000ff : is_begin ? 0xff00ff00 : 0xffff0000;
								dl->AddRect(ImVec2(x - 2, y - 2), ImVec2(x + 2, y + 2), color);
								const bool mouse_hovered = ImGui::IsMouseHoveringRect(ImVec2(x - 2, y - 2), ImVec2(x + 2, y + 2));
								if (mouse_hovered) {
									m_hovered_fiber_wait.frame = m_frame_idx;
									m_hovered_fiber_wait.id = r.id;
									m_hovered_fiber_wait.pos = ImVec2(x, y);
									m_hovered_fiber_wait.signal = r.job_system_signal;

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
								draw_block(open_blocks.last().start_time, header.time, block.name, color, open_blocks.last().id);
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
					draw_block(open_blocks.last().start_time, m_end, b.name, ImGui::GetColorU32(ImGuiCol_PlotHistogram), open_blocks.last().id);
					open_blocks.pop();
				}

				ImGui::Dummy(ImVec2(to_x - from_x, lines * line_height));

				ImGui::TreePop();
			});

			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_hovered_block.frame < m_frame_idx - 2) {
				m_filter.clear();
			}

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

			before_gpu_y = ImGui::GetCursorScreenPos().y;
			{
				const ThreadContextProxy& ctx = getGlobalThreadContextProxy();
				const bool gpu_open = ImGui::TreeNode(ctx.buffer, "GPU");
				const float y = ImGui::GetCursorScreenPos().y;

				StackArray<u32, 32> open_blocks(m_allocator);
				u32 lines = 0;
				bool has_gpu_stats = false;
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
								const float x_start = get_view_x(from);
								float x_end = get_view_x(to);
								if (int(x_end) == int(x_start)) ++x_end;
								const float block_y = (open_blocks.size() - 1) * line_height + y;
								const float w = ImGui::CalcTextSize(data.name).x;

								const ImVec2 ra(x_start, block_y);
								const ImVec2 rb(x_end, block_y + line_height - 1);
								u32 color = 0xffDDddDD;
								if (m_hovered_link.link == data.profiler_link && m_hovered_link.frame > m_frame_idx - 2) color = 0xff0000ff;
								dl->AddRectFilled(ra, rb, color);
								if (x_end - x_start > 2) {
									dl->AddRect(ra, rb, ImGui::GetColorU32(ImGuiCol_Border));
								}
								if (w + 2 < x_end - x_start) {
									dl->AddText(ImVec2(x_start + 2, block_y), 0xff000000, data.name);
								}
								if (ImGui::IsMouseHoveringRect(ra, rb)) {
									const float t = 1000 * float((to - from) / double(freq));
									ImGui::BeginTooltip();
									ImGui::Text("%s (%.4f ms)", data.name, t);
									if (data.profiler_link) {
										ImGui::Text("Link: %" PRId64, data.profiler_link);
										m_hovered_link.frame = m_frame_idx;
										m_hovered_link.link = data.profiler_link;
									}
									if (has_gpu_stats) {
										char tmp[32];
										toCStringPretty(primitives_generated, Span(tmp));
										ImGui::Text("Primitives: %s", tmp);
									}
									ImGui::EndTooltip();
								}
							}
							if (open_blocks.size() > 0) open_blocks.pop();
							has_gpu_stats = false;
							break;
						case profiler::EventType::GPU_STATS:
							read(ctx, p + sizeof(profiler::EventHeader), primitives_generated);
							has_gpu_stats = true;
							break;
						case profiler::EventType::FRAME:
							if (header.time < view_start) timeline_start_t = header.time;
							else {
								if (header.time <= m_end && m_show_frames) {
									if (timeline_start_t <= view_start) timeline_start_t = header.time;
									const float x = get_view_x(header.time);
									dl->ChannelsSetCurrent(1);
									dl->AddLine(ImVec2(x, from_y), ImVec2(x, before_gpu_y), 0xffff0000);
									dl->ChannelsSetCurrent(0);
								}
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
	HashMap<u32, ThreadRecord> m_threads;
	OutputMemoryStream m_data;
	os::Timer m_timer;
	float m_autopause = -33.3333f;
	bool m_show_context_switches = false;
	bool m_show_frames = true;
	Array<Counter> m_counters;
	bool m_is_open = false;
	Action m_toggle_ui;
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