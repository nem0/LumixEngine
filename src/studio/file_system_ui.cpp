#include "file_system_ui.h"
#include "core/fs/file_events_device.h"
#include "core/fs/file_system.h"
#include "core/mt/lock_free_fixed_queue.h"
#include "core/string.h"
#include "core/timer.h"
#include "engine/engine.h"
#include "ocornut-imgui/imgui.h"




struct FileSystemUIImpl : public FileSystemUI
{
	FileSystemUIImpl(Lumix::Engine& engine)
		: m_engine(engine)
		, m_device(engine.getAllocator())
		, m_opened_files(engine.getAllocator())
		, m_logs(engine.getAllocator())
	{
		m_is_opened = false;
		m_filter[0] = 0;
		m_timer = Lumix::Timer::create(engine.getAllocator());
		m_device.OnEvent.bind<FileSystemUIImpl, &FileSystemUIImpl::onFileSystemEvent>(this);
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
	}


	~FileSystemUIImpl()
	{
		m_engine.getFileSystem().unMount(&m_device);
		Lumix::Timer::destroy(m_timer);
	}


	void sortByDuration()
	{
		if (m_logs.empty()) return;

		m_sort_order = m_sort_order == ASC ? DESC : ASC;
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
		if (m_sort_order == ASC)
		{
			qsort(&m_logs[0], m_logs.size(), sizeof(m_logs[0]), asc_comparer);
		}
		else
		{
			qsort(&m_logs[0], m_logs.size(), sizeof(m_logs[0]), desc_comparer);
		}
	}


	void onGUI() override
	{
		while (!m_queue.isEmpty())
		{
			auto* log = m_queue.pop(false);
			if (!log) break;
			m_logs.push(*log);
			m_queue.dealoc(log);
			m_sort_order = NOT_SORTED;
		}

		if (!m_is_opened) return;
		if (ImGui::Begin("File system", &m_is_opened))
		{
			ImGui::InputText("filter", m_filter, Lumix::lengthOf(m_filter));
			
			if (ImGui::Button("Clear")) m_logs.clear();

			if (ImGui::BeginChild("list"))
			{
				ImGui::Columns(3);
				ImGui::Text("File");
				ImGui::NextColumn();
				const char* duration_label = "Duration (ms)";
				if (m_sort_order == ASC) duration_label = "Duration (ms) < ";
				else if (m_sort_order == DESC) duration_label = "Duration (ms) >";
				if (ImGui::Selectable(duration_label))
				{
					sortByDuration();
				}
				ImGui::NextColumn();
				ImGui::Text("Bytes read (kB)");
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
		ImGui::End();
	}


	void onFileSystemEvent(const Lumix::FS::Event& event)
	{
		if (event.type == Lumix::FS::EventType::OPEN_BEGIN)
		{
			auto& file = m_opened_files.pushEmpty();
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
					auto& log = *m_queue.alloc(true);
					log.bytes = m_opened_files[i].bytes;
					log.time = m_opened_files[i].last_read - m_opened_files[i].start;
					Lumix::copyString(log.path, m_opened_files[i].path);
					m_opened_files.eraseFast(i);
					m_queue.push(&log, true);
					break;
				}
			}
		}
	}


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
		ASC,
		DESC
	};
	char m_filter[100];
	Lumix::Array<OpenedFile> m_opened_files;
	Lumix::MT::LockFreeFixedQueue<Log, 64> m_queue;
	Lumix::Array<Log> m_logs;
	Lumix::FS::FileEventsDevice m_device;
	Lumix::Engine& m_engine;
	Lumix::Timer* m_timer;
	SortOrder m_sort_order;
};


FileSystemUI* FileSystemUI::create(Lumix::Engine& engine)
{
	return LUMIX_NEW(engine.getAllocator(), FileSystemUIImpl)(engine);
}


void FileSystemUI::destroy(FileSystemUI& ui)
{
	auto& ui_impl = static_cast<FileSystemUIImpl&>(ui);
	LUMIX_DELETE(ui_impl.m_engine.getAllocator(), &ui_impl);
}
