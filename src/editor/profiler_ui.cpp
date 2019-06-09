#include "profiler_ui.h"
#include "engine/fibers.h"
#include "engine/fs/file_events_device.h"
#include "engine/fs/file_system.h"
#include "engine/fs/os_file.h"
#include "engine/job_system.h"
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
#include <cmath>
#include <inttypes.h>


namespace Lumix
{


static constexpr int MAX_FRAMES = 200;
static constexpr int DEFAULT_ZOOM = 100'000;


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
	if (reason >= lengthOf(reasons)) return "Unknown";
	return reasons[reason];
}


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

		if (!m_is_open) return;
		if (ImGui::Begin("Profiler", &m_is_open))
		{
			onGUICPUProfiler();
			onGUIMemoryProfiler();
			onGUIResources();
			onGUIFileSystem();
		}
		ImGui::End();
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
	char m_cpu_block_filter[256] = {};
	u64 m_zoom = DEFAULT_ZOOM;
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
	float m_autopause = -33.3333f;
	bool m_show_context_switches = false;
	bool m_gpu_open = false;
	struct {
		u32 signal;
		float x, y;
		bool is_current_pos = false;
	} hovered_signal;
	i64 hovered_link = 0;
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


static void read(Profiler::ThreadContext& ctx, uint p, u8* ptr, int size)
{
	u8* buf = ctx.buffer.begin();
	const uint buf_size = ctx.buffer.size();
	const uint l = p % buf_size;
	if (l + size <= buf_size) {
		memcpy(ptr, buf + l, size);
		return;
	}

	memcpy(ptr, buf + l, buf_size - l);
	memcpy(ptr + (buf_size - l), buf, size - (buf_size - l));
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


struct VisibleBlock
{
	const char* name;
};


struct ThreadRecord
{
	float y;
	struct {
		u64 time;
		bool is_enter;
	}last_context_switch;
};


static const char* getThreadName(const Array<Profiler::ThreadContext*>& contexts, u32 thread_id)
{
	for (Profiler::ThreadContext* ctx : contexts) {
		if (ctx->thread_id == thread_id) return ctx->name;
	}
	return "Unknown";
}


void ProfilerUIImpl::onGUICPUProfiler()
{
	if (!ImGui::CollapsingHeader("CPU/GPU")) return;

	if (ImGui::Checkbox("Pause", &m_is_paused)) {
		Profiler::pause(m_is_paused);
		m_view_offset = 0;
		m_paused_time = Timer::getRawTimestamp();
	}

	const Array<Profiler::ThreadContext*>& contexts = Profiler::lockContexts();
	if (ImGui::BeginMenu("Advanced")) {
		ImGui::Text("Zoom: %f", m_zoom / double(DEFAULT_ZOOM));
		if (ImGui::MenuItem("Reset zoom")) m_zoom = DEFAULT_ZOOM;
		bool do_autopause = m_autopause > 0;
		if (ImGui::Checkbox("Autopause enabled", &do_autopause)) {
			m_autopause = -m_autopause;
		}
		if (m_autopause > 0) {
			ImGui::InputFloat("Autopause limit (ms)", &m_autopause, 1.f, 10.f, 2);
		}
		if (ImGui::BeginMenu("Threads")) {
			for (Profiler::ThreadContext* ctx : contexts) {
				ImGui::Checkbox(ctx->name.data, &ctx->show_in_profiler);
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


	const u64 view_end = m_is_paused ? m_paused_time + m_view_offset : Timer::getRawTimestamp();
	const u64 view_start = view_end < m_zoom ? 0 : view_end - m_zoom;
	float h = 0;
	for (Profiler::ThreadContext* ctx : contexts) {
		if (ctx->show_in_profiler) {
			h += ctx->rows * 20 + 20;
		}
	}
	h += 20; // gpu header
	if (m_gpu_open) {
		h += 40;
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

	HashMap<const char*, const char*> visible_blocks(1024, m_allocator);
	HashMap<u32, ThreadRecord> threads_records(64, m_allocator);
	bool any_hovered_signal = false;
	bool any_hovered_link = false;
	bool hovered_signal_current_pos = false;

	for (Profiler::ThreadContext* ctx : contexts) {
		if (!ctx->show_in_profiler) continue;

		MT::SpinLock lock(ctx->mutex);
		threads_records.insert(ctx->thread_id, { y, 0 });
		renderArrow(ImVec2(a.x, y), ctx->open ? ImGuiDir_Down : ImGuiDir_Right, 1, dl);
		dl->AddText(ImVec2(a.x + 20, y), ImGui::GetColorU32(ImGuiCol_Text), ctx->name);
		dl->AddLine(ImVec2(a.x, y + 20), ImVec2(b.x, y + 20), ImGui::GetColorU32(ImGuiCol_Border));
		if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(ImVec2(a.x, y), ImVec2(a.x + 20, y + 20))) {
			ctx->open = !ctx->open;
		}
		y += 20;
		if (!ctx->open) continue;

		float h = Math::maximum(20.f, ctx->rows * 20.f);
		StaticString<256> name(ctx->name, (u64)ctx);
		ctx->rows = 0;

		struct {
			uint offset;
			i32 switch_id;
			u32 color;
			i64 link;
			Profiler::JobRecord job_info;
		} open_blocks[64];
		int level = -1;
		uint p = ctx->begin;
		const uint end = ctx->end;

		struct Property {
			Profiler::EventHeader header;
			int level;
			int offset;
		} properties[64];
		int properties_count = 0;

		auto draw_block = [&](u64 from, u64 to, const char* name, u32 color) {
			if (from <= view_end && to >= view_start) {
				const float t_start = float(int(from - view_start) / double(view_end - view_start));
				const float t_end = float(int(to - view_start) / double(view_end - view_start));
				const float x_start = a.x * (1 - t_start) + b.x * t_start;
				float x_end = a.x * (1 - t_end) + b.x * t_end;
				if (int(x_end) == int(x_start)) ++x_end;
				const float block_y = level * 20.f + y;
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
							read(*ctx, properties[i].offset, (u8*)&r, sizeof(r));
							ImGui::Text("%s: %d", r.key, r.value);
							break;
						}
						case Profiler::EventType::STRING: {
							char tmp[128];
							const int tmp_size = properties[i].header.size - sizeof(properties[i].header);
							read(*ctx, properties[i].offset, (u8*)tmp, tmp_size);
							ImGui::Text("%s", tmp);
							break;
						}
						default: ASSERT(false); break;
						}
					}
					ImGui::EndTooltip();
				}
			}
		};

		while (p != end) {
			Profiler::EventHeader header;
			read(*ctx, p, header);
			switch (header.type) {
			case Profiler::EventType::END_FIBER_WAIT:
			case Profiler::EventType::BEGIN_FIBER_WAIT: {
				const bool is_begin = header.type == Profiler::EventType::BEGIN_FIBER_WAIT;
				Profiler::FiberWaitRecord r;
				read(*ctx, p + sizeof(Profiler::EventHeader), r);
				if (r.job_system_signal == hovered_signal.signal) {
					float t = float((header.time - view_start) / double(view_end - view_start));
					if (header.time < view_start) {
						t = -float((view_start - header.time) / double(view_end - view_start));
					}
					const float x = a.x * (1 - t) + b.x * t;
					if (hovered_signal.is_current_pos && (x != hovered_signal.x || y != hovered_signal.y)) {
						dl->AddLine(ImVec2(x, y - 2), ImVec2(hovered_signal.x, hovered_signal.y - 2), 0xff00ff00);
					}
				}
				if (header.time >= view_start && header.time <= view_end || is_begin && hovered_signal.signal == r.job_system_signal) {
					const float t = view_start <= header.time
						? float((header.time - view_start) / double(view_end - view_start))
						: -float((view_start - header.time) / double(view_end - view_start));
					const float x = a.x * (1 - t) + b.x * t;
					const u32 color = header.type == Profiler::EventType::END_FIBER_WAIT ? 0xffff0000 : 0xff00ff00;
					dl->AddRect(ImVec2(x - 2, y - 2), ImVec2(x + 2, y + 2), color);
					const bool mouse_hovered = ImGui::IsMouseHoveringRect(ImVec2(x - 2, y - 2), ImVec2(x + 2, y + 2));
					if (mouse_hovered || is_begin && hovered_signal.signal == r.job_system_signal) {
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
					read(*ctx, p + sizeof(Profiler::EventHeader), open_blocks[level].link);
				}
				break;
			case Profiler::EventType::BEGIN_BLOCK:
				++level;
				ASSERT(level < lengthOf(open_blocks));
				open_blocks[level].link = 0;
				open_blocks[level].offset = p;
				open_blocks[level].color = 0xffDDddDD;
				open_blocks[level].job_info.signal_on_finish = JobSystem::INVALID_HANDLE;
				open_blocks[level].job_info.precondition = JobSystem::INVALID_HANDLE;
				break;
			case Profiler::EventType::END_BLOCK:
				if (level >= 0) {
					ctx->rows = Math::maximum(ctx->rows, level + 1);
					Profiler::EventHeader start_header;
					read(*ctx, open_blocks[level].offset, start_header);
					const char* name;
					read(*ctx, open_blocks[level].offset + sizeof(Profiler::EventHeader), name);
					if (!m_cpu_block_filter[0] || stristr(name, m_cpu_block_filter)) {
						u32 color = open_blocks[level].color;
						if (open_blocks[level].job_info.signal_on_finish != JobSystem::INVALID_HANDLE
							&& hovered_signal.signal == open_blocks[level].job_info.signal_on_finish
							|| hovered_link == open_blocks[level].link 
							&& hovered_link != 0)
						{
							color = 0xff0000ff;
						}
						draw_block(start_header.time, header.time, name, color);
						if (!visible_blocks.find(name).isValid()) {
							visible_blocks.insert(name, name);
						}
					}
					while (properties_count > 0 && properties[properties_count - 1].level == level) {
						--properties_count;
					}
					--level;
				}
				break;
			case Profiler::EventType::FRAME:
				ASSERT(false);
				/*should be in global context*/
				break;
			case Profiler::EventType::INT:
			case Profiler::EventType::STRING: {
				if (properties_count < lengthOf(properties) && level >= 0) {
					properties[properties_count].header = header;
					properties[properties_count].level = level;
					properties[properties_count].offset = sizeof(Profiler::EventHeader) + p;
					++properties_count;
				}
				else {
					ASSERT(false);
				}
				break;
			}
			case Profiler::EventType::JOB_INFO:
				if (level >= 0) {
					read(*ctx, p + sizeof(Profiler::EventHeader), open_blocks[level].job_info);
				}
				break;
			case Profiler::EventType::BLOCK_COLOR:
				if (level >= 0) {
					read(*ctx, p + sizeof(Profiler::EventHeader), open_blocks[level].color);
				}
				break;
			default: ASSERT(false); break;
			}
			p += header.size;
		}
		ctx->rows = Math::maximum(ctx->rows, level + 1);
		while (level >= 0) {
			Profiler::EventHeader start_header;
			read(*ctx, open_blocks[level].offset, start_header);
			const char* name;
			read(*ctx, open_blocks[level].offset + sizeof(Profiler::EventHeader), name);
			if (!m_cpu_block_filter[0] || stristr(name, m_cpu_block_filter)) {
				draw_block(start_header.time, m_paused_time, name, ImGui::GetColorU32(ImGuiCol_PlotHistogram));
			}
			--level;
		}
		y += ctx->rows * 20;
	}

	if (!any_hovered_link) hovered_link = 0;
	if (!any_hovered_signal) hovered_signal.signal = JobSystem::INVALID_HANDLE;
	if (!hovered_signal_current_pos) hovered_signal.is_current_pos = false;

	auto get_view_x = [&](u64 time) {
		const float t = time > view_start
			? float((time - view_start) / double(view_end - view_start))
			: -float((view_start - time) / double(view_end - view_start));
		return a.x * (1 - t) + b.x * t;
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
			ImGui::Text("  from: %s (%d)", getThreadName(contexts, r.old_thread_id), r.old_thread_id);
			ImGui::Text("  to: %s (%d)", getThreadName(contexts, r.new_thread_id), r.new_thread_id);
			ImGui::Text("  reason: %s", getContexSwitchReasonString(r.reason));
			ImGui::EndTooltip();
		}
		tr.last_context_switch.time = r.timestamp;
		tr.last_context_switch.is_enter = is_enter;
	};

	{
		Profiler::ThreadContext& ctx = Profiler::getGlobalContext();
		MT::SpinLock lock(ctx.mutex);
		renderArrow(ImVec2(a.x, y), m_gpu_open ? ImGuiDir_Down : ImGuiDir_Right, 1, dl);
		dl->AddText(ImVec2(a.x + 20, y), ImGui::GetColorU32(ImGuiCol_Text), "GPU");
		dl->AddLine(ImVec2(a.x, y + 20), ImVec2(b.x, y + 20), ImGui::GetColorU32(ImGuiCol_Border));
		if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(ImVec2(a.x, y), ImVec2(a.x + 20, y + 20))) {
			m_gpu_open = !m_gpu_open;
		}
		y += 20;

		uint open_blocks[64];
		int level = -1;

		uint p = ctx.begin;
		const uint end = ctx.end;
		while (p != end) {
			Profiler::EventHeader header;
			read(ctx, p, header);
			switch (header.type) {
				case Profiler::EventType::BEGIN_GPU_BLOCK:
					++level;
					ASSERT(level < lengthOf(open_blocks));
					open_blocks[level] = p;
					break;
				case Profiler::EventType::END_GPU_BLOCK:
					if (level >= 0 && m_gpu_open) {
						Profiler::EventHeader start_header;
						read(ctx, open_blocks[level], start_header);
						Profiler::GPUBlock data;
						read(ctx, open_blocks[level] + sizeof(Profiler::EventHeader), data);
						u64 to;
						read(ctx, p + sizeof(Profiler::EventHeader), to);
						/***/
						const u64 from = data.timestamp;
						const float t_start = float(int(from - view_start) / double(view_end - view_start));
						const float t_end = float(int(to - view_start) / double(view_end - view_start));
						const float x_start = a.x * (1 - t_start) + b.x * t_start;
						float x_end = a.x * (1 - t_end) + b.x * t_end;
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
							ImGui::Text("%s (%.3f ms)", data.name.data, t);
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
				case Profiler::EventType::FRAME:
					if (header.time >= view_start && header.time <= view_end) {
						const float t = float((header.time - view_start) / double(view_end - view_start));
						const float x = a.x * (1 - t) + b.x * t;
						dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), 0xffff0000);
					}
					break;
				case Profiler::EventType::CONTEXT_SWITCH:
					if (m_show_context_switches && header.time >= view_start && header.time <= view_end) {
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
	}

	Profiler::unlockContexts();

	for (const ThreadRecord& tr : threads_records) {
		if (tr.last_context_switch.is_enter) {
			const float x = get_view_x(tr.last_context_switch.time);
			dl->AddLine(ImVec2(b.x, tr.y + 10), ImVec2(x, tr.y + 10), 0xff00ff00);
		}
	}

	if (m_autopause > 0 && !m_is_paused && Profiler::getLastFrameDuration() * 1000.f > m_autopause) {
		m_is_paused = true;
		Profiler::pause(m_is_paused);
		m_view_offset = 0;
		m_paused_time = Timer::getRawTimestamp();
	}

	if (ImGui::CollapsingHeader("Visible blocks")) {
		ImGui::LabellessInputText("Filter", m_cpu_block_filter, sizeof(m_cpu_block_filter));
		int dummy = -1;
		if (ImGui::BeginChild("Visible blocks", ImVec2(0, 150))) {
			for (const char* b : visible_blocks) {
				if (!m_cpu_block_filter[0] || stristr(b, m_cpu_block_filter) != nullptr) {
					ImGui::Text("%s", b);
				}
			}
		}
		ImGui::EndChild();
	}
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