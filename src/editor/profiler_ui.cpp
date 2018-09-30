#include "profiler_ui.h"
#include "engine/fs/file_events_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/os_file.h"
#include "engine/log.h"
#include "engine/math_utils.h"
#include "engine/mt/atomic.h"
#include "engine/mt/lock_free_fixed_queue.h"
#include "engine/profiler.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/timer.h"
#include "engine/debug/debug.h"
#include "engine/engine.h"
#include "imgui/imgui.h"
#include "utils.h"
#include <cstdlib>


namespace Lumix
{


static const int MAX_FRAMES = 200;


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


struct ProfilerUIImpl final : public ProfilerUI
{
	ProfilerUIImpl(Debug::Allocator& allocator, Engine& engine)
		: m_main_allocator(allocator)
		, m_resource_manager(engine.getResourceManager())
		, m_logs(allocator)
		, m_open_files(allocator)
		, m_device(allocator)
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

		m_timer = Timer::create(engine.getAllocator());
		m_device.OnEvent.bind<ProfilerUIImpl, &ProfilerUIImpl::onFileSystemEvent>(this);
		engine.getFileSystem().mount(&m_device);
		const auto& devices = engine.getFileSystem().getDefaultDevice();
		char tmp[200] = "";
		int count = 0;
		while (devices.m_devices[count] != nullptr) ++count;
		for (int i = count - 1; i >= 0; --i)
		{
			catString(tmp, ":");
			catString(tmp, devices.m_devices[i]->name());
			if (equalStrings(devices.m_devices[i]->name(), "memory"))
			{
				catString(tmp, ":events");
			}
		}
		engine.getFileSystem().setDefaultDevice(tmp);
		m_sort_order = NOT_SORTED;
		setMemory(m_transfer_rates, 0, sizeof(m_transfer_rates));
		m_current_transfer_rate = 0;
		m_bytes_read = 0;
		m_next_transfer_rate_time = 0;
	}


	~ProfilerUIImpl()
	{
		while (m_engine.getFileSystem().hasWork())
		{
			m_engine.getFileSystem().updateAsyncTransactions();
		}

		m_engine.getFileSystem().unMount(&m_device);
		const auto& devices = m_engine.getFileSystem().getDefaultDevice();
		char tmp[200] = "";
		int count = 0;
		while (devices.m_devices[count] != nullptr) ++count;
		for (int i = count - 1; i >= 0; --i)
		{
			if (!equalStrings(devices.m_devices[i]->name(), "events"))
			{
				if(i < count - 1) catString(tmp, ":");
				catString(tmp, devices.m_devices[i]->name());
			}
		}
		m_engine.getFileSystem().setDefaultDevice(tmp);

		m_allocation_root->clear(m_allocator);
		LUMIX_DELETE(m_allocator, m_allocation_root);
		Timer::destroy(m_timer);
	}


	void onFileSystemEvent(const FS::Event& event)
	{
		if (event.type == FS::EventType::OPEN_BEGIN)
		{
			auto& file = m_open_files.emplace();
			file.start = m_timer->getTimeSinceStart();
			file.last_read = file.start;
			file.bytes = 0;
			file.handle = event.handle;
			copyString(file.path, event.path);
		}
		else if (event.type == FS::EventType::OPEN_FINISHED && event.ret == 0)
		{
			for (int i = 0; i < m_open_files.size(); ++i)
			{
				if (m_open_files[i].handle == event.handle)
				{
					m_open_files.eraseFast(i);
					break;
				}
			}
		}
		else if (event.type == FS::EventType::READ_FINISHED)
		{
			for (int i = 0; i < m_open_files.size(); ++i)
			{
				if (m_open_files[i].handle == event.handle)
				{
					m_open_files[i].bytes += event.param;
					MT::atomicAdd(&m_bytes_read, event.param);
					m_open_files[i].last_read = m_timer->getTimeSinceStart();
					return;
				}
			}
			ASSERT(false);
		}
		else if (event.type == FS::EventType::CLOSE_FINISHED)
		{
			for (int i = 0; i < m_open_files.size(); ++i)
			{
				if (m_open_files[i].handle == event.handle)
				{
					auto* log = m_queue.alloc(false);
					if (!log)
					{
						m_open_files.eraseFast(i);
						break;
					}
					log->bytes = m_open_files[i].bytes;
					log->time = m_open_files[i].last_read - m_open_files[i].start;
					copyString(log->path, m_open_files[i].path);
					m_open_files.eraseFast(i);
					m_queue.push(log, true);
					break;
				}
			}
		}
	}


	void sortByDuration()
	{
		if (m_logs.empty()) return;

		m_sort_order = m_sort_order == TIME_ASC ? TIME_DESC : TIME_ASC;
		auto asc_comparer = [](const void* data1, const void* data2) -> int{
			float t1 = static_cast<const Log*>(data1)->time;
			float t2 = static_cast<const Log*>(data2)->time;
			return t1 < t2 ? -1 : (t1 > t2 ? 1 : 0);
		};
		auto desc_comparer = [](const void* data1, const void* data2) -> int{
			float t1 = static_cast<const Log*>(data1)->time;
			float t2 = static_cast<const Log*>(data2)->time;
			return t1 > t2 ? -1 : (t1 < t2 ? 1 : 0);
		};
		if (m_sort_order == TIME_ASC)
		{
			qsort(&m_logs[0], m_logs.size(), sizeof(m_logs[0]), asc_comparer);
		}
		else
		{
			qsort(&m_logs[0], m_logs.size(), sizeof(m_logs[0]), desc_comparer);
		}
	}


	void sortByBytesRead()
	{
		if (m_logs.empty()) return;

		m_sort_order = m_sort_order == BYTES_READ_ASC ? BYTES_READ_DESC : BYTES_READ_ASC;
		auto asc_comparer = [](const void* data1, const void* data2) -> int{
			return static_cast<const Log*>(data1)->bytes - static_cast<const Log*>(data2)->bytes;
		};
		auto desc_comparer = [](const void* data1, const void* data2) -> int{
			return static_cast<const Log*>(data2)->bytes - static_cast<const Log*>(data1)->bytes;
		};
		if (m_sort_order == BYTES_READ_ASC)
		{
			qsort(&m_logs[0], m_logs.size(), sizeof(m_logs[0]), asc_comparer);
		}
		else
		{
			qsort(&m_logs[0], m_logs.size(), sizeof(m_logs[0]), desc_comparer);
		}
	}


	void onGUIFileSystem()
	{
		if (!ImGui::CollapsingHeader("File system")) return;
		auto getter = [](void* data, int index) -> float {
			auto* ui = (ProfilerUIImpl*)data;
			int abs_index = (ui->m_current_transfer_rate + index) % lengthOf(ui->m_transfer_rates);
			return ui->m_transfer_rates[abs_index] / 1000.0f;
		};

		ImGui::PlotLines("kB/s",
			getter,
			this,
			lengthOf(m_transfer_rates),
			0,
			nullptr,
			FLT_MAX,
			FLT_MAX,
			ImVec2(0, 100));

		ImGui::InputText("filter###fs_filter", m_filter, lengthOf(m_filter));

		if(ImGui::Button("Clear")) m_logs.clear();

		if(ImGui::BeginChild("list"))
		{
			ImGui::Columns(3);
			ImGui::Text("File");
			ImGui::NextColumn();

			const char* duration_label = "Duration (ms)";
			if (m_sort_order == TIME_ASC)
			{
				duration_label = "Duration (ms) < ";
			}
			else if (m_sort_order == TIME_DESC)
			{
				duration_label = "Duration (ms) >";
			}
			if (ImGui::Selectable(duration_label))
			{
				sortByDuration();
			}
			ImGui::NextColumn();

			const char* bytes_read_label = "Bytes read (kB)";
			if(m_sort_order == BYTES_READ_ASC) bytes_read_label = "Bytes read (kB) <";
			else if(m_sort_order == BYTES_READ_DESC) bytes_read_label = "Bytes read (kB) >";
			if(ImGui::Selectable(bytes_read_label))
			{
				sortByBytesRead();
			}
			ImGui::NextColumn();
			ImGui::Separator();
			for (auto& log : m_logs)
			{
				if (m_filter[0] == 0 || stristr(log.path, m_filter) != 0)
				{
					ImGui::Text("%s", log.path);
					ImGui::NextColumn();
					ImGui::Text("%f", log.time * 1000.0f);
					ImGui::NextColumn();
					ImGui::Text("%.3f", (float)((double)log.bytes / 1000.0f));
					ImGui::NextColumn();
				}
			}
			ImGui::Columns(1);
		}
		ImGui::EndChild();
	}


	void onGUI() override
	{
		PROFILE_FUNCTION();
		while (!m_queue.isEmpty())
		{
			auto* log = m_queue.pop(false);
			if (!log) break;
			m_logs.push(*log);
			m_queue.dealoc(log);
			m_sort_order = NOT_SORTED;
		}

		m_next_transfer_rate_time -= m_engine.getLastTimeDelta();
		if (m_next_transfer_rate_time < 0)
		{
			m_next_transfer_rate_time = 0.3f;
			m_transfer_rates[m_current_transfer_rate] = m_bytes_read;
			m_current_transfer_rate =
				(m_current_transfer_rate + 1) % lengthOf(m_transfer_rates);
			m_bytes_read = 0;
		}

		if (ImGui::BeginDock("Profiler", &m_is_open))
		{
			onGUICPUProfiler();
			onGUIMemoryProfiler();
			onGUIResources();
			onGUIFileSystem();
		}
		ImGui::EndDock();
	}


	struct OpenedFile
	{
		uintptr handle;
		float start;
		float last_read;
		int bytes;
		char path[MAX_PATH_LENGTH];
	};


	struct Log
	{
		char path[MAX_PATH_LENGTH];
		float time;
		int bytes;
	};

	enum SortOrder
	{
		NOT_SORTED,
		TIME_ASC,
		TIME_DESC,
		BYTES_READ_ASC,
		BYTES_READ_DESC
	};


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
	void saveResourceList() const;

	DefaultAllocator m_allocator;
	Debug::Allocator& m_main_allocator;
	ResourceManagerHub& m_resource_manager;
	AllocationStackNode* m_allocation_root;
	int m_allocation_size_from;
	int m_allocation_size_to;
	int m_current_frame;
	bool m_is_paused;
	u64 m_paused_time;
	i64 m_view_offset = 0;
	u64 m_zoom = 100'000;
	char m_filter[100];
	char m_resource_filter[100];
	Array<OpenedFile> m_open_files;
	MT::LockFreeFixedQueue<Log, 512> m_queue;
	Array<Log> m_logs;
	FS::FileEventsDevice m_device;
	Engine& m_engine;
	Timer* m_timer;
	int m_transfer_rates[100];
	int m_current_transfer_rate;
	volatile int m_bytes_read;
	float m_next_transfer_rate_time;
	SortOrder m_sort_order;
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


void ProfilerUIImpl::saveResourceList() const
{
	FS::OsFile file;
	if (file.open("resources.csv", FS::Mode::CREATE_AND_WRITE))
	{
		auto& managers = m_resource_manager.getAll();
		for (auto* i : managers)
		{
			auto& resources = i->getResourceTable();
			for (auto& res : resources)
			{
				file.write(res->getPath().c_str(), res->getPath().length());
				file.write(", ", 2);
				char tmp[50];
				toCString(res->size() / 1024.0f, tmp, lengthOf(tmp), 3);
				file.write(tmp, stringLength(tmp));
				file.write("KB, ", 4);

				const char* state = getResourceStateString(res->getState());
				file.write(state, stringLength(state));

				file.write(", ", 4);
				toCString(res->getRefCount(), tmp, lengthOf(tmp));
				file.write(tmp, stringLength(tmp));
				file.write("\n", 4);
			}
		}
		file.close();
	}
	else
	{
		g_log_error.log("Editor") << "Failed to save resource list to resources.csv";
	}
}


void ProfilerUIImpl::onGUIResources()
{
	if (!ImGui::CollapsingHeader("Resources")) return;

	ImGui::LabellessInputText("Filter###resource_filter", m_resource_filter, lengthOf(m_resource_filter));

	static const ResourceType RESOURCE_TYPES[] = { ResourceType("animation"),
		ResourceType("material"),
		ResourceType("model"),
		ResourceType("physics"),
		ResourceType("shader"),
		ResourceType("texture") };
	static const char* manager_names[] = {
		"Animations",
		"Materials",
		"Models",
		"Physics",
		"Shaders",
		"Textures"
	};
	ASSERT(lengthOf(RESOURCE_TYPES) == lengthOf(manager_names));
	ImGui::Indent();
	for (int i = 0; i < lengthOf(RESOURCE_TYPES); ++i)
	{
		if (!ImGui::CollapsingHeader(manager_names[i])) continue;

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

	static int saved_displayed = 0;

	if (saved_displayed > 0)
	{
		--saved_displayed;
		ImGui::Text("Saved");
	}
	else if (ImGui::Button("Save"))
	{
		saved_displayed = 180;
		saveResourceList();
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
	int count = Debug::StackTree::getPath(info->stack_leaf, nodes, lengthOf(nodes));

	auto node = m_allocation_root;
	for (int i = count - 1; i >= 0; --i)
	{
		node = getOrCreate(node, nodes[i], info->size);
	}
	node->m_allocations.push(info);
}


void ProfilerUIImpl::refreshAllocations()
{
	m_allocation_root->clear(m_allocator);
	LUMIX_DELETE(m_allocator, m_allocation_root);
	m_allocation_root = LUMIX_NEW(m_allocator, AllocationStackNode)(nullptr, 0, m_allocator);

	m_main_allocator.lock();
	auto* current_info = m_main_allocator.getFirstAllocationInfo();

	while (current_info)
	{
		addToTree(current_info);
		current_info = current_info->next;
	}
	m_main_allocator.unlock();
}


void ProfilerUIImpl::showAllocationTree(AllocationStackNode* node, int column) const
{
	if (column == FUNCTION)
	{
		char fn_name[100];
		int line;
		if (Debug::StackTree::getFunction(node->m_stack_node, fn_name, sizeof(fn_name), &line))
		{
			if (line >= 0)
			{
				int len = stringLength(fn_name);
				if (len + 2 < sizeof(fn_name))
				{
					fn_name[len] = ' ';
					fn_name[len + 1] = '\0';
					++len;
					toCString(line, fn_name + len, sizeof(fn_name) - len);
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
		toCStringPretty(node->m_inclusive_size, size, sizeof(size));
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

	if (ImGui::Button("Refresh"))
	{
		refreshAllocations();
	}

	ImGui::SameLine();
	if (ImGui::Button("Check memory"))
	{
		m_main_allocator.checkGuards();
	}
	ImGui::Text("Total size: %.3fMB", (m_main_allocator.getTotalSize() / 1024) / 1024.0f);

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
static void read(Profiler::ThreadContext& ctx, uint p, T& value)
{
	u8* buf = ctx.buffer.begin();
	const uint buf_size = ctx.buffer.size();
	const uint l = p % buf_size;
	if (l + sizeof(value) <= buf_size) {
		memcpy(&value, buf + l, sizeof(value));
		return;
	}

	memcpy(&value, buf + l, buf_size - l);
	memcpy((u8*)&value + (buf_size - l), buf, sizeof(value) - (buf_size - l));
}


static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs)            { return ImVec2(lhs.x+rhs.x, lhs.y+rhs.y); }


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
	if (!ImGui::CollapsingHeader("CPU")) return;

	if(ImGui::Checkbox("Pause", &m_is_paused)) {
		Profiler::pause(m_is_paused);
		m_paused_time = Profiler::now();
	}

	auto& contexts = Profiler::lockContexts();
	
	const u64 view_end = m_is_paused ? m_paused_time + m_view_offset : Profiler::now();
	const u64 view_start = view_end - m_zoom;
	float h = 0;
	for(Profiler::ThreadContext* ctx : contexts) {
		h += ctx->rows * 20 + 20;
	}
	ImGui::InvisibleButton("x", ImVec2(-1, h));
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 a = ImGui::GetItemRectMin();
	const ImVec2 b = ImGui::GetItemRectMax();
	float y = a.y;

	const u64 cursor = u64(((ImGui::GetMousePos().x - a.x) / (b.x - a.x)) * m_zoom) + view_start;
	u64 cursor_to_end = view_end - cursor;

	if (ImGui::IsItemHovered()) {
		if (ImGui::IsMouseDragging()) {
			m_view_offset -= i64((ImGui::GetIO().MouseDelta.x / (b.x - a.x)) * m_zoom);
		}
		if (ImGui::GetIO().KeyCtrl) {
			if (ImGui::GetIO().MouseWheel > 0 && m_zoom > 1) {
				m_zoom >>= 1;
				cursor_to_end >>= 1;
			}
			else if (ImGui::GetIO().MouseWheel < 0) {
				m_zoom <<= 1;
				cursor_to_end <<= 1;
			}
			m_view_offset = cursor + cursor_to_end - m_paused_time;
		}
	}

	dl->ChannelsSplit(2);

	for(Profiler::ThreadContext* ctx : contexts) {
		MT::SpinLock lock(ctx->mutex);
		renderArrow(ImVec2(a.x, y), ctx->open ? ImGuiDir_Down : ImGuiDir_Right, 1, dl);
		dl->AddText(ImVec2(a.x + 20, y), ImGui::GetColorU32(ImGuiCol_Text), ctx->name);
		if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(ImVec2(a.x, y), ImVec2(a.x + 20, y+20))){
			ctx->open = !ctx->open;
		}
		y += 20;
		if (!ctx->open) continue;

		float h = Math::maximum(20.f, ctx->rows * 20.f);
		StaticString<256> name(ctx->name, (u64)ctx);
		ctx->rows = 0;

		uint open_blocks[64];
		int level = -1;
		uint p = ctx->begin;
		const uint end = ctx->end;

		auto draw_block = [&](u64 from, u64 to, const char* name, u32 color) {
			if(from <= view_end && to >= view_start) {
				const float t_start = float(int(from - view_start) / double(view_end - view_start));
				const float t_end = float(int(to - view_start) / double(view_end - view_start));
				const float x_start = a.x * (1 - t_start) + b.x * t_start;
				float x_end = a.x * (1 - t_end) + b.x * t_end;
				if (int(x_end) == int(x_start)) ++x_end;
				const float block_y = level * 20.f + y;
				const float w = ImGui::CalcTextSize(name).x;
							
				const ImVec2 ra(x_start, block_y);
				const ImVec2 rb(x_end, block_y + 19);
				dl->AddRectFilled(ra, rb, color);
				if(x_end - x_start > 2) {
					dl->AddRect(ra, rb, ImGui::GetColorU32(ImGuiCol_Border));
				}
				if (w + 2 < x_end - x_start) {
					dl->AddText(ImVec2(x_start + 2, block_y), 0xff000000, name);
				}
				if(ImGui::IsMouseHoveringRect(ra, rb)) {
					const u64 freq = Profiler::frequency();
					const float t = 1000 * float((to - from) / double(freq));
					ImGui::SetTooltip("%s (%.2f ms)", name, t);
				}
			}
		};

		while(p != end) {
			Profiler::EventHeader header;
			read(*ctx, p, header);
			switch(header.type) {
				case Profiler::EventType::BEGIN_BLOCK:
					++level;
					ASSERT(level < lengthOf(open_blocks));
					open_blocks[level] = p;
					break;
				case Profiler::EventType::END_BLOCK:
					if (level >= 0) {
						ctx->rows = Math::maximum(ctx->rows, level + 1);
						Profiler::EventHeader start_header;
						read(*ctx, open_blocks[level], start_header);
						const char* name;
						read(*ctx, open_blocks[level] + sizeof(Profiler::EventHeader), name);
						u32 color;
						read(*ctx, open_blocks[level] + sizeof(Profiler::EventHeader) + sizeof(name), color);
						draw_block(start_header.time, header.time, name, color);
						--level;
					}
					break;
				case Profiler::EventType::FRAME: {
					if (header.time >= view_start && header.time <= view_end) {
						const float t = float((header.time - view_start) / double(view_end - view_start));
						const float x = a.x * (1 - t) + b.x * t;
						dl->ChannelsSetCurrent(1);
						dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), 0xffff0000);
						dl->ChannelsSetCurrent(0);
					}
					break;
				}
				default: ASSERT(false); break;
			}
			p+= header.size;
		}
		ctx->rows = Math::maximum(ctx->rows, level + 1);
		while(level >= 0) {
			Profiler::EventHeader start_header;
			read(*ctx, open_blocks[level], start_header);
			const char* name;
			read(*ctx, open_blocks[level] + sizeof(Profiler::EventHeader), name);
			draw_block(start_header.time, m_paused_time, name, ImGui::GetColorU32(ImGuiCol_PlotHistogram));
			--level;
		}
		y += ctx->rows * 20;
	}

	dl->ChannelsMerge();
	Profiler::unlockContexts();

	/*
	auto thread_getter = [](void* data, int index, const char** out) -> bool {
		auto id = Profiler::getThreadID(index);
		*out = Profiler::getThreadName(id);
		return true;
	};

	if (m_threads.empty()) return;

	ImGui::Columns(5, "cpuc");
	ImGui::Text("Name"); ImGui::NextColumn();
	ImGui::Text("Inclusive time"); ImGui::NextColumn();
	ImGui::Text("Exclusive time"); ImGui::NextColumn();
	ImGui::Text("Hit count"); ImGui::NextColumn();
	ImGui::Text("Hits"); ImGui::NextColumn();
	ImGui::Separator();

	showThreadColumn(*this, NAME);
	showThreadColumn(*this, TIME);
	showThreadColumn(*this, EXCLUSIVE_TIME);
	showThreadColumn(*this, HIT_COUNT);
	showThreadColumn(*this, HITS);
	ImGui::Columns(1);

	auto* block = m_current_block ? m_current_block : m_threads[0].root;
	if(!block) return;

	float width = ImGui::GetWindowContentRegionWidth();
	int count = Math::minimum(int(width / 5), block->m_int_values.size());
	int offset = block->m_int_values.size() - count;
	struct PlotData
	{
		Block* block;
		int offset;
	};
	auto getter = [](void* data, int idx) -> float
	{
		auto* plot_data = (PlotData*)data;
		switch (plot_data->block->m_type)
		{
			case Profiler::BlockType::TIME:
				return plot_data->block->m_frames[plot_data->offset + idx];
			case Profiler::BlockType::INT:
				return (float)plot_data->block->m_int_values[plot_data->offset + idx];
		}
		return 0;
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
	if (i != -1) m_current_frame = i + offset;*/
}


ProfilerUI* ProfilerUI::create(Engine& engine)
{
	auto& allocator = static_cast<Debug::Allocator&>(engine.getAllocator());
	return LUMIX_NEW(engine.getAllocator(), ProfilerUIImpl)(allocator, engine);
}


void ProfilerUI::destroy(ProfilerUI& ui)
{
	auto& ui_impl = static_cast<ProfilerUIImpl&>(ui);
	LUMIX_DELETE(ui_impl.m_engine.getAllocator(), &ui);
}


} // namespace Lumix