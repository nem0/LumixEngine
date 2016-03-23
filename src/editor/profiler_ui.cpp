#include "profiler_ui.h"
#include "core/fs/file_events_device.h"
#include "core/fs/file_system.h"
#include "core/fs/os_file.h"
#include "core/log.h"
#include "core/math_utils.h"
#include "core/mt/atomic.h"
#include "core/mt/lock_free_fixed_queue.h"
#include "core/profiler.h"
#include "core/resource.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/timer.h"
#include "debug/debug.h"
#include "engine/engine.h"
#include "imgui/imgui.h"
#include "utils.h"
#include <cstdlib>


static const int MAX_FRAMES = 200;


enum Column
{
	NAME,
	TIME,
	HIT_COUNT,
	HITS
};


enum MemoryColumn
{
	FUNCTION,
	SIZE
};


struct ProfilerUIImpl : public ProfilerUI
{
	ProfilerUIImpl(Lumix::Debug::Allocator& allocator, Lumix::Engine& engine)
		: m_main_allocator(allocator)
		, m_resource_manager(engine.getResourceManager())
		, m_logs(allocator)
		, m_opened_files(allocator)
		, m_device(allocator)
		, m_engine(engine)
		, m_threads(allocator)
	{
		m_allocation_size_from = 0;
		m_allocation_size_to = 1024 * 1024;
		m_current_frame = -1;
		m_is_opened = false;
		m_is_paused = true;
		m_current_block = nullptr;
		m_frame_start = m_frame_end = 0;
		Lumix::Profiler::getFrameListeners().bind<ProfilerUIImpl, &ProfilerUIImpl::onFrame>(this);
		m_allocation_root = LUMIX_NEW(m_allocator, AllocationStackNode)(m_allocator);
		m_allocation_root->m_stack_node = nullptr;
		m_filter[0] = 0;
		m_resource_filter[0] = 0;

		m_timer = Lumix::Timer::create(engine.getAllocator());
		m_device.OnEvent.bind<ProfilerUIImpl, &ProfilerUIImpl::onFileSystemEvent>(this);
		engine.getFileSystem().mount(&m_device);
		const auto& devices = engine.getFileSystem().getDefaultDevice();
		char tmp[200] = "";
		int count = 0;
		while (devices.m_devices[count] != nullptr) ++count;
		for (int i = count - 1; i >= 0; --i)
		{
			Lumix::catString(tmp, ":");
			Lumix::catString(tmp, devices.m_devices[i]->name());
			if (Lumix::compareString(devices.m_devices[i]->name(), "memory") == 0)
			{
				Lumix::catString(tmp, ":events");
			}
		}
		engine.getFileSystem().setDefaultDevice(tmp);
		m_sort_order = NOT_SORTED;
		Lumix::setMemory(m_transfer_rates, 0, sizeof(m_transfer_rates));
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
			if (Lumix::compareString(devices.m_devices[i]->name(), "events") != 0)
			{
				if(i < count - 1) Lumix::catString(tmp, ":");
				Lumix::catString(tmp, devices.m_devices[i]->name());
			}
		}
		m_engine.getFileSystem().setDefaultDevice(tmp);

		m_allocation_root->clear(m_allocator);
		LUMIX_DELETE(m_allocator, m_allocation_root);
		Lumix::Profiler::getFrameListeners().unbind<ProfilerUIImpl, &ProfilerUIImpl::onFrame>(this);
		Lumix::Timer::destroy(m_timer);
	}


	void onFileSystemEvent(const Lumix::FS::Event& event)
	{
		if (event.type == Lumix::FS::EventType::OPEN_BEGIN)
		{
			auto& file = m_opened_files.emplace();
			file.start = m_timer->getTimeSinceStart();
			file.last_read = file.start;
			file.bytes = 0;
			file.handle = event.handle;
			Lumix::copyString(file.path, event.path);
		}
		else if (event.type == Lumix::FS::EventType::OPEN_FINISHED && event.ret == 0)
		{
			for (int i = 0; i < m_opened_files.size(); ++i)
			{
				if (m_opened_files[i].handle == event.handle)
				{
					m_opened_files.eraseFast(i);
					break;
				}
			}
		}
		else if (event.type == Lumix::FS::EventType::READ_FINISHED)
		{
			for (int i = 0; i < m_opened_files.size(); ++i)
			{
				if (m_opened_files[i].handle == event.handle)
				{
					m_opened_files[i].bytes += event.param;
					Lumix::MT::atomicAdd(&m_bytes_read, event.param);
					m_opened_files[i].last_read = m_timer->getTimeSinceStart();
					return;
				}
			}
			ASSERT(false);
		}
		else if (event.type == Lumix::FS::EventType::CLOSE_FINISHED)
		{
			for (int i = 0; i < m_opened_files.size(); ++i)
			{
				if (m_opened_files[i].handle == event.handle)
				{
					auto* log = m_queue.alloc(false);
					if (!log)
					{
						m_opened_files.eraseFast(i);
						break;
					}
					log->bytes = m_opened_files[i].bytes;
					log->time = m_opened_files[i].last_read - m_opened_files[i].start;
					Lumix::copyString(log->path, m_opened_files[i].path);
					m_opened_files.eraseFast(i);
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
			int abs_index = (ui->m_current_transfer_rate + index) % Lumix::lengthOf(ui->m_transfer_rates);
			return ui->m_transfer_rates[abs_index] / 1000.0f;
		};

		ImGui::PlotLines("kB/s",
			getter,
			this,
			Lumix::lengthOf(m_transfer_rates),
			0,
			nullptr,
			FLT_MAX,
			FLT_MAX,
			ImVec2(0, 100));

		ImGui::InputText("filter###fs_filter", m_filter, Lumix::lengthOf(m_filter));

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
				if (m_filter[0] == 0 || Lumix::stristr(log.path, m_filter) != 0)
				{
					ImGui::Text(log.path);
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
				(m_current_transfer_rate + 1) % Lumix::lengthOf(m_transfer_rates);
			m_bytes_read = 0;
		}

		if (ImGui::BeginDock("Profiler", &m_is_opened))
		{
			onGUICPUProfiler();
			onGUIMemoryProfiler();
			onGUIResources();
			onGUIFileSystem();
		}
		ImGui::EndDock();
	}


	enum class Values
	{
		NAME,
		LENGTH,
		LENGTH_EXCLUSIVE,
		HIT_COUNT,
		COUNT
	};


	struct OpenedFile
	{
		Lumix::uintptr handle;
		float start;
		float last_read;
		int bytes;
		char path[Lumix::MAX_PATH_LENGTH];
	};


	struct Log
	{
		char path[Lumix::MAX_PATH_LENGTH];
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


	struct Block
	{
		explicit Block(Lumix::IAllocator& allocator);
		~Block() {}

		const char* m_name;
		Block* m_parent;
		Block* m_first_child;
		Block* m_next;
		bool m_is_opened;
		Lumix::Profiler::BlockType m_type;
		Lumix::Array<float> m_frames;
		struct Hit
		{
			Lumix::uint64 start;
			Lumix::uint64 length;
		};
		Lumix::Array<Hit> m_hits;
		Lumix::Array<int> m_int_values; // hit count in case of m_type == TIME
	};


	struct AllocationStackNode
	{
		explicit AllocationStackNode(Lumix::IAllocator& allocator)
			: m_children(allocator)
			, m_allocations(allocator)
		{
		}


		~AllocationStackNode()
		{
			ASSERT(m_children.empty());
		}


		void clear(Lumix::IAllocator& allocator)
		{
			for (auto* child : m_children)
			{
				child->clear(allocator);
				LUMIX_DELETE(allocator, child);
			}
			m_children.clear();
		}


		size_t m_inclusive_size;
		bool m_opened;
		Lumix::Debug::StackNode* m_stack_node;
		Lumix::Array<AllocationStackNode*> m_children;
		Lumix::Array<Lumix::Debug::Allocator::AllocationInfo*> m_allocations;
	};


	void onGUICPUProfiler();
	void onGUIMemoryProfiler();
	void onGUIResources();
	void onFrame();
	void showProfileBlock(Block* block, int column);
	void cloneBlock(Block* my_block, Lumix::Profiler::Block* remote_block);
	void addToTree(Lumix::Debug::Allocator::AllocationInfo* info);
	void refreshAllocations();
	void showAllocationTree(AllocationStackNode* node, int column);
	AllocationStackNode* getOrCreate(AllocationStackNode* my_node,
		Lumix::Debug::StackNode* external_node, size_t size);
	void saveResourceList();

	struct Thread
	{
		Block* root;
		bool opened;
	};

	Lumix::DefaultAllocator m_allocator;
	Lumix::Array<Thread> m_threads;
	Block* m_current_block;
	Lumix::Debug::Allocator& m_main_allocator;
	Lumix::ResourceManager& m_resource_manager;
	AllocationStackNode* m_allocation_root;
	int m_allocation_size_from;
	int m_allocation_size_to;
	int m_current_frame;
	bool m_is_paused;
	char m_filter[100];
	char m_resource_filter[100];
	Lumix::Array<OpenedFile> m_opened_files;
	Lumix::MT::LockFreeFixedQueue<Log, 512> m_queue;
	Lumix::Array<Log> m_logs;
	Lumix::FS::FileEventsDevice m_device;
	Lumix::Engine& m_engine;
	Lumix::Timer* m_timer;
	int m_transfer_rates[100];
	int m_current_transfer_rate;
	volatile int m_bytes_read;
	float m_next_transfer_rate_time;
	SortOrder m_sort_order;
};


void ProfilerUIImpl::cloneBlock(Block* my_block, Lumix::Profiler::Block* remote_block)
{
	ASSERT(my_block->m_name == Lumix::Profiler::getBlockName(remote_block));
	my_block->m_hits.clear();
	my_block->m_type = Lumix::Profiler::getBlockType(remote_block);
	switch (my_block->m_type)
	{
		case Lumix::Profiler::BlockType::TIME:
			my_block->m_frames.push(Lumix::Profiler::getBlockLength(remote_block));
			my_block->m_int_values.push(Lumix::Profiler::getBlockHitCount(remote_block));
			break;
		case Lumix::Profiler::BlockType::INT:
			my_block->m_int_values.push(Lumix::Profiler::getBlockInt(remote_block));
			break;
		default:
			ASSERT(false);
			break;
	}
	my_block->m_hits.resize(Lumix::Profiler::getBlockHitCount(remote_block));
	for(int i = 0, c = my_block->m_hits.size(); i < c; ++i)
	{
		auto& hit = my_block->m_hits[i];
		hit.start = Lumix::Profiler::getBlockHitStart(remote_block, i);
		hit.length = Lumix::Profiler::getBlockHitLength(remote_block, i);
	}

	if (my_block->m_frames.size() > MAX_FRAMES)
	{
		my_block->m_frames.erase(0);
	}
	if (my_block->m_int_values.size() > MAX_FRAMES)
	{
		my_block->m_int_values.erase(0);
	}

	auto* remote_child = Lumix::Profiler::getBlockFirstChild(remote_block);
	if (!my_block->m_first_child && remote_child)
	{
		Block* my_child = new Block(m_allocator);
		my_child->m_name = Lumix::Profiler::getBlockName(remote_child);
		my_child->m_parent = my_block;
		my_child->m_next = nullptr;
		my_child->m_first_child = nullptr;
		my_block->m_first_child = my_child;
		cloneBlock(my_child, remote_child);
	}
	else if (my_block->m_first_child)
	{
		Block* my_child = my_block->m_first_child;
		if (my_child->m_name != Lumix::Profiler::getBlockName(remote_child))
		{
			Block* my_new_child = new Block(m_allocator);
			my_new_child->m_name = Lumix::Profiler::getBlockName(remote_child);
			my_new_child->m_parent = my_block;
			my_new_child->m_next = my_child;
			my_new_child->m_first_child = nullptr;
			my_block->m_first_child = my_new_child;
			my_child = my_new_child;
		}
		cloneBlock(my_child, remote_child);
	}

	auto* remote_next = Lumix::Profiler::getBlockNext(remote_block);
	if (!my_block->m_next && remote_next)
	{
		Block* my_next = new Block(m_allocator);
		my_next->m_name = Lumix::Profiler::getBlockName(remote_next);
		my_next->m_parent = my_block->m_parent;
		my_next->m_next = nullptr;
		my_next->m_first_child = nullptr;
		my_block->m_next = my_next;
		cloneBlock(my_next, remote_next);
	}
	else if (my_block->m_next)
	{
		if (my_block->m_next->m_name != Lumix::Profiler::getBlockName(remote_next))
		{
			Block* my_next = new Block(m_allocator);
			my_next->m_name = Lumix::Profiler::getBlockName(remote_next);
			my_next->m_parent = my_block->m_parent;
			my_next->m_next = my_block->m_next;
			my_next->m_first_child = nullptr;
			my_block->m_next = my_next;
		}
		cloneBlock(my_block->m_next, remote_next);
	}
}


void ProfilerUIImpl::onFrame()
{
	if (!m_is_opened) return;
	if (m_is_paused) return;

	m_frame_start = m_frame_end;
	m_frame_end = Lumix::Profiler::now();
	int thread_count = Lumix::Profiler::getThreadCount();
	for (int i = 0; i < thread_count; ++i)
	{
		Lumix::uint32 thread_id = Lumix::Profiler::getThreadID(i);
		auto* root = Lumix::Profiler::getRootBlock(thread_id);
		if (m_threads.size() <= i)
		{
			if (!root)
			{
				auto& thread = m_threads.emplace();
				thread.root = nullptr;
				thread.opened = false;
			}
			else
			{
				auto* my_root = new Block(m_allocator);
				my_root->m_name = Lumix::Profiler::getBlockName(root);
				my_root->m_parent = nullptr;
				my_root->m_next = nullptr;
				my_root->m_first_child = nullptr;
				auto& thread = m_threads.emplace();
				thread.root = my_root;
				thread.opened = false;
			}
		}
		if (!m_threads[i].root && root)
		{
			auto* my_root = new Block(m_allocator);
			my_root->m_name = Lumix::Profiler::getBlockName(root);
			my_root->m_parent = nullptr;
			my_root->m_next = nullptr;
			my_root->m_first_child = nullptr;
			m_threads[i].root = my_root;
		}
		ASSERT(!root || m_threads[i].root->m_name == Lumix::Profiler::getBlockName(root));

		if (m_threads[i].root) cloneBlock(m_threads[i].root, root);
	}
}


void ProfilerUIImpl::showProfileBlock(Block* block, int column)
{
	if (!block) return;

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
				switch (block->m_type)
				{
					case Lumix::Profiler::BlockType::TIME:
					{
						auto frame = m_current_frame < 0 ? block->m_frames.back() : block->m_frames[m_current_frame];
						if (ImGui::Selectable(StringBuilder<50>("") << frame * 1000.0f << "###t" << (Lumix::int64)block,
								m_current_block == block))
						{
							m_current_block = block;
						}
						if (block->m_is_opened)
						{
							showProfileBlock(block->m_first_child, column);
						}
					}
					break;
					case Lumix::Profiler::BlockType::INT:
					{
						int int_value =
							m_current_frame < 0 ? block->m_int_values.back() : block->m_int_values[m_current_frame];
						if (ImGui::Selectable(StringBuilder<50>("") << int_value << "###c" << (Lumix::int64)block,
								m_current_block == block,
								ImGuiSelectableFlags_SpanAllColumns))
						{
							m_current_block = block;
						}
					}
					break;
					default: ASSERT(false); break;
				}

				block = block->m_next;
			}
			return;
		case HIT_COUNT:
			if (block->m_type == Lumix::Profiler::BlockType::TIME)
			{
				while (block)
				{
					int hit_count =
						m_current_frame < 0 ? block->m_int_values.back() : block->m_int_values[m_current_frame];

					ImGui::Text("%d", hit_count);
					if (block->m_is_opened)
					{
						showProfileBlock(block->m_first_child, column);
					}

					block = block->m_next;
				}
			}
			return;
		case HITS:
			while(block)
			{
				if (block->m_hits.empty())
				{
					ImGui::Dummy(ImVec2(10, ImGui::GetTextLineHeight()));
				}
				else
				{
					ImGui::IntervalGraph(&block->m_hits[0].start, block->m_hits.size(), m_frame_start, m_frame_end);
				}
				if(block->m_is_opened)
				{
					showProfileBlock(block->m_first_child, column);
				}

				block = block->m_next;
			}
			return;
		default: ASSERT(false); return;
	}
}


static const char* getResourceStateString(Lumix::Resource::State state)
{
	switch (state)
	{
		case Lumix::Resource::State::EMPTY: return "Empty"; break;
		case Lumix::Resource::State::FAILURE: return "Failure"; break;
		case Lumix::Resource::State::READY: return "Ready"; break;
	}

	return "Unknown";
}


void ProfilerUIImpl::saveResourceList()
{
	Lumix::FS::OsFile file;
	if (file.open("resources.csv", Lumix::FS::Mode::CREATE_AND_WRITE, m_allocator))
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
				Lumix::toCString(res->size() / 1024.0f, tmp, Lumix::lengthOf(tmp), 3);
				file.write(tmp, Lumix::stringLength(tmp));
				file.write("KB, ", 4);

				const char* state = getResourceStateString(res->getState());
				file.write(state, Lumix::stringLength(state));

				file.write(", ", 4);
				Lumix::toCString(res->getRefCount(), tmp, Lumix::lengthOf(tmp));
				file.write(tmp, Lumix::stringLength(tmp));
				file.write("\n", 4);
			}
		}
		file.close();
	}
	else
	{
		Lumix::g_log_error.log("Editor") << "Failed to save resource list to resources.csv";
	}
}


void ProfilerUIImpl::onGUIResources()
{
	if (!ImGui::CollapsingHeader("Resources")) return;

	ImGui::InputText("filter###resource_filter", m_resource_filter, Lumix::lengthOf(m_resource_filter));

	Lumix::uint32 manager_types[] = { Lumix::ResourceManager::ANIMATION,
		Lumix::ResourceManager::MATERIAL,
		Lumix::ResourceManager::MODEL,
		Lumix::ResourceManager::PHYSICS,
		Lumix::ResourceManager::SHADER,
		Lumix::ResourceManager::TEXTURE};
	const char* manager_names[] = {
		"Animations",
		"Materials",
		"Models",
		"Physics",
		"Shaders",
		"Textures"
	};
	ASSERT(Lumix::lengthOf(manager_types) == Lumix::lengthOf(manager_names));
	ImGui::Indent();
	for (int i = 0; i < Lumix::lengthOf(manager_types); ++i)
	{
		if (!ImGui::CollapsingHeader(manager_names[i])) continue;

		auto* material_manager = m_resource_manager.get(manager_types[i]);
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
			if (m_resource_filter[0] != '\0' &&
				Lumix::stristr(iter.value()->getPath().c_str(), m_resource_filter) == nullptr)
			{
				continue;
			}

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

	auto new_node = LUMIX_NEW(m_allocator, AllocationStackNode)(m_allocator);
	my_node->m_children.push(new_node);
	new_node->m_stack_node = external_node;
	new_node->m_inclusive_size = size;
	return new_node;
}


void ProfilerUIImpl::addToTree(Lumix::Debug::Allocator::AllocationInfo* info)
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


void ProfilerUIImpl::refreshAllocations()
{
	m_allocation_root->clear(m_allocator);
	LUMIX_DELETE(m_allocator, m_allocation_root);
	m_allocation_root = LUMIX_NEW(m_allocator, AllocationStackNode)(m_allocator);
	m_allocation_root->m_stack_node = nullptr;

	m_main_allocator.lock();
	auto* current_info = m_main_allocator.getFirstAllocationInfo();

	while (current_info)
	{
		addToTree(current_info);
		current_info = current_info->m_next;
	}
	m_main_allocator.unlock();
}


void ProfilerUIImpl::showAllocationTree(AllocationStackNode* node, int column)
{
	if (column == FUNCTION)
	{
		char fn_name[100];
		int line;
		if (Lumix::Debug::StackTree::getFunction(node->m_stack_node, fn_name, sizeof(fn_name), &line))
		{
			if (line >= 0)
			{
				int len = Lumix::stringLength(fn_name);
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


static void showThreadColumn(ProfilerUIImpl& profiler, Column column)
{
	for (int i = 0; i < profiler.m_threads.size(); ++i)
	{
		auto* root = profiler.m_threads[i].root;
		Lumix::uint32 thread_id = Lumix::Profiler::getThreadID(i);
		const char* thread_name = Lumix::Profiler::getThreadName(thread_id);
		if (column != NAME)
		{
			ImGui::Dummy(ImVec2(10, ImGui::GetTextLineHeight()));
			if (profiler.m_threads[i].opened)
			{
				profiler.showProfileBlock(root, column);
			}
		}
		else
		{
			if (ImGui::TreeNode(root, thread_name))
			{
				profiler.m_threads[i].opened = true;
				profiler.showProfileBlock(root, column);
				ImGui::TreePop();
			}
			else
			{
				profiler.m_threads[i].opened = false;
			}
		}
	}
	ImGui::NextColumn();
}


void ProfilerUIImpl::onGUICPUProfiler()
{
	if (!ImGui::CollapsingHeader("CPU")) return;

	ImGui::Checkbox("Pause", &m_is_paused);

	auto thread_getter = [](void* data, int index, const char** out) -> bool {
		auto id = Lumix::Profiler::getThreadID(index);
		*out = Lumix::Profiler::getThreadName(id);
		return true;
	};

	if (m_threads.empty()) return;

	ImGui::Columns(4, "cpuc");
	showThreadColumn(*this, NAME);
	showThreadColumn(*this, TIME);
	showThreadColumn(*this, HIT_COUNT);
	showThreadColumn(*this, HITS);
	ImGui::Columns(1);

	auto* block = m_current_block ? m_current_block : m_threads[0].root;
	if(!block) return;

	float width = ImGui::GetWindowContentRegionWidth();
	int count = Lumix::Math::minimum(int(width / 5), block->m_int_values.size());
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
			case Lumix::Profiler::BlockType::TIME:
				return plot_data->block->m_frames[plot_data->offset + idx];
			case Lumix::Profiler::BlockType::INT:
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
	if (i != -1) m_current_frame = i + offset;
}



ProfilerUIImpl::Block::Block(Lumix::IAllocator& allocator)
	: m_frames(allocator)
	, m_hits(allocator)
	, m_int_values(allocator)
	, m_is_opened(false)
{
}


ProfilerUI* ProfilerUI::create(Lumix::Engine& engine)
{
	auto& allocator = static_cast<Lumix::Debug::Allocator&>(engine.getAllocator());
	return LUMIX_NEW(engine.getAllocator(), ProfilerUIImpl)(allocator, engine);
}


void ProfilerUI::destroy(ProfilerUI& ui)
{
	auto& ui_impl = static_cast<ProfilerUIImpl&>(ui);
	LUMIX_DELETE(ui_impl.m_engine.getAllocator(), &ui);
}

