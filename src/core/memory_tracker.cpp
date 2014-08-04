#include "core/memory_tracker.h"
#include "core/log.h"
#include "core/math_utils.h"
#include "core/mt/spin_mutex.h"
#include "core/stack_allocator.h"
#include "core/string.h"

//#include <new>
#include <stdio.h>
#include <vadefs.h>
#include <Windows.h>

#ifdef MEM_TRACK

#undef min

namespace Lumix
{
	void memTrackerLog(const char*, const char* message, ...)
	{
		char tmp[1024];
		va_list args;
		va_start(args, message);
		vsnprintf(tmp, 1021, message, args);
		va_end(args);

		catCString(tmp, sizeof(tmp), "\n");
		OutputDebugString(tmp);
	}

	// struct FILE_LINE_REPORT
	struct FileLineReport
	{
		const char *file;
		int32_t line;

		LUMIX_FORCE_INLINE bool operator == (const FileLineReport &other) const { return file == other.file && line == other.line; }
		LUMIX_FORCE_INLINE bool operator != (const FileLineReport &other) const { return !(*this == other); }

		LUMIX_FORCE_INLINE bool operator < (const FileLineReport &other) const
		{
			if(file == NULL)
				return other.file != NULL;
			if(other.file == NULL)
				return false;
			int cmp = strcmp(file, other.file);
			if(cmp != 0)
				return cmp < 0;
			return line < other.line;
		}

		LUMIX_FORCE_INLINE bool operator > (const FileLineReport &other) const
		{
			if(file == NULL)
				return other.file != NULL;
			if(other.file == NULL)
				return false;
			int cmp = strcmp(file, other.file);
			if(cmp != 0)
				return cmp > 0;
			return line > other.line;
		}
	};

	typedef Map<uint32_t, MemoryTracker::Entry*, MemTrackAllocator> map_alloc_order;
	typedef Map<FileLineReport, intptr_t, MemTrackAllocator> file_line_map;
	typedef Map<const char *, intptr_t, MemTrackAllocator> file_map;
	typedef Map<FileLineReport, uint32_t, MemTrackAllocator> alloc_count_map;

	#pragma init_seg(compiler)
	MemoryTracker MemoryTracker::s_instance;
	uint32_t MemoryTracker::s_alloc_counter = 0;

	MemoryTracker& MemoryTracker::getInstance()
	{
		return s_instance;
	}

	void MemoryTracker::add(void* p, const intptr_t size, const char* file, const int line)
	{
		if(!p) return;

		MT::SpinLock lock(m_spin_mutex);

		m_map.insert(p, Entry(file, line, size));
		m_allocated_memory += size;
	}

	void MemoryTracker::remove(void* p)
	{
		if(!p) return;

		MT::SpinLock lock(m_spin_mutex);

		EntryTable::iterator it = m_map.find(p);
		ASSERT(it != m_map.end() && "Allocated/Dealocataed from different places?");
		if(it != m_map.end())
		{
			m_allocated_memory -= (*it).size();
			m_map.erase(it);
		}
	}

	static void getEntryLog(MemoryTracker::Entry& entry, void* address, base_string<char, StackAllocator<512> >& string)
	{
		if (entry.file() != NULL)
		{
			string.cat(entry.file(), "(", entry.line(), ") : ");
		}
		string.cat("{", entry.allocID(), " } normal block");
		if (address)
		{
			string.cat(" at ", (int64_t)address);
		}
		string.cat(", ", entry.size(), " bytes long.");
	}

	void MemoryTracker::dumpDetailed()
	{
		// Detected memory leaks!
		// Dumping objects ->
		// {147} normal block at 0x003AF7C0, 10 bytes long.
		// Data: <          > CD CD CD CD CD CD CD CD CD CD
		// d:\temp\zmazat\tt\tt\mainfrm.cpp(34) : {145} normal block at 0x003AF760, 30 bytes long.
		// Data: <               > CD CD CD CD CD CD CD CD CD CD CD CD CD CD CD CD
		//    Object dump complete.

		MT::SpinLock lock(m_spin_mutex);
		int32_t count = m_map.size();

		if (count)
		{
			memTrackerLog("MemoryTracker", "MemoryTracker Detected memory leaks!");
			memTrackerLog("MemoryTracker", "Dumping objects ->");
		}
		else
		{
			memTrackerLog("MemoryTracker", "MemoryTracker No leaks detected!");
		}

		for (EntryTable::iterator it = m_map.begin(); it != m_map.end(); ++it)
		{
			base_string<char, StackAllocator<512> > string;

			Entry& entry = *it;
			void* adr = it.key();

			getEntryLog(entry, adr, string);
			memTrackerLog("MemoryTracker", string.c_str());

			int32_t str_len = Math::min(16, (int32_t)entry.size());
			char asci_buf[17];
			memset(asci_buf, 0, 17);
			memcpy(asci_buf, adr, str_len);

			string = "";
			string.cat("Data: <", asci_buf, ">");
			for (int j = 0; j < str_len; j++)
			{
				char hex[4];
				hex[0] = ' ';
				toCStringHex(*((uint8_t*)adr + j), hex+1, 2);
				hex[3] = 0;
				string.cat(hex);
			}
			memTrackerLog("MemoryTracker", "%s", string.c_str());
		}
		if(count)
		{
			memTrackerLog("MemoryTracker", "	  Object dump complete.");
		}
	}

	void MemoryTracker::dumpSortedByAllocationOrder()
	{
		// Detected memory leaks!
		// Dumping objects ->
		// {147} normal block at 0x003AF7C0, 10 bytes long.
		// Data: <          > CD CD CD CD CD CD CD CD CD CD
		// d:\temp\zmazat\tt\tt\mainfrm.cpp(34) : {145} normal block at 0x003AF760, 30 bytes long.
		// Data: <               > CD CD CD CD CD CD CD CD CD CD CD CD CD CD CD CD
		//    Object dump complete.

		MT::SpinLock lock(m_spin_mutex);
		int count = m_map.size();

		if (count)
		{
			memTrackerLog("MemoryTracker", "MemoryTracker Detected memory leaks!");
			memTrackerLog("MemoryTracker", "Dumping objects ->");
		}
		else
		{
			memTrackerLog("MemoryTracker", "MemoryTracker No leaks detected!");
		}

		map_alloc_order alloc_order_map;
		for (EntryTable::iterator it = m_map.begin(); it != m_map.end(); ++it)
		{
			Entry& entry = *it;
			alloc_order_map.insert(entry.allocID(), &entry);
		}

		for (map_alloc_order::iterator it = alloc_order_map.begin(); it != alloc_order_map.end(); ++it)
		{
			base_string<char, StackAllocator<512> > string;
			Entry& entry = *(it.second());
			getEntryLog(entry, NULL, string);

			memTrackerLog("MemoryTracker", string.c_str());
		}

		if(count)
		{
			memTrackerLog("MemoryTracker", "	  Object dump complete.");
		}
	}

	void MemoryTracker::dumpTruncatedPerFileLine()
	{
		memTrackerLog("MemoryTracker", "Dumping objects ->");

		file_line_map report_map;
		{
			MT::SpinLock lock(m_spin_mutex);
			for (EntryTable::iterator it = m_map.begin(); it != m_map.end(); ++it)
			{
				Entry& entry = *it;

				FileLineReport r;
				r.file = entry.file();
				r.line = entry.line();

				file_line_map::iterator rit = report_map.find(r);
				if(rit != report_map.end())
					rit.second() += entry.size();
				else
					report_map.insert(r, entry.size());
			}
		}

		for (file_line_map::iterator it = report_map.begin(); it != report_map.end(); ++it)
		{
			base_string<char, StackAllocator<512> > string;

			const FileLineReport &rep = it.first();
			intptr_t size = it.second();

			const char *file = rep.file ? rep.file : "unknown";

			string = file;
			string += "(";
			string.cat(rep.line);
			string += ") : ";
			string.cat(size);

			memTrackerLog("MemoryTracker", string.c_str());
		}

		memTrackerLog("MemoryTracker", "Object dump complete.");
	}

	void MemoryTracker::dumpTruncatedPerFile()
	{
		memTrackerLog("MemoryTracker", "Dumping objects ->");

		file_map report_map;
		{
			MT::SpinLock lock(m_spin_mutex);
			for (EntryTable::iterator it = m_map.begin(); it != m_map.end(); ++it)
			{
				Entry& entry = *it;


				file_map::iterator rit = report_map.find(entry.file());
				if(rit != report_map.end())
					rit.second() += entry.size();
				else
					report_map.insert(entry.file(), entry.size());
			}
		}

		for (file_map::iterator it = report_map.begin(); it != report_map.end(); ++it)
		{
			base_string<char, StackAllocator<512> > string;

			intptr_t size = it.second();
			const char *file = it.first();

			
			string = file;
			string += " : ";
			string.cat(size);

			memTrackerLog("MemoryTracker", string.c_str());
		}

		memTrackerLog("MemoryTracker", "Object dump complete.");
	}

	void MemoryTracker::markAll()
	{
		MT::SpinLock lock(m_spin_mutex);

		for (EntryTable::iterator it = m_map.begin(); it != m_map.end(); ++it)
		{
			(*it).mark();
		}
		++m_mark;
	}

	void MemoryTracker::dumpUnmarked()
	{
		// Detected memory leaks!
		// Dumping objects ->
		// {147} normal block at 0x003AF7C0, 10 bytes long.
		// Data: <          > CD CD CD CD CD CD CD CD CD CD
		// d:\temp\zmazat\tt\tt\mainfrm.cpp(34) : {145} normal block at 0x003AF760, 30 bytes long.
		// Data: <               > CD CD CD CD CD CD CD CD CD CD CD CD CD CD CD CD
		//    Object dump complete.

		//	Lock lock(m_lock);

		MT::SpinLock lock(m_spin_mutex);

		std::size_t size = 0;

		memTrackerLog("MemoryTracker", "Dumping objects ->");

		for (EntryTable::iterator it = m_map.begin(); it != m_map.end(); ++it)
		{
			base_string<char, StackAllocator<512> > string;

			Entry& entry = *it;
			void* adr = it.key();

			if (0 == entry.getMark() || m_mark == entry.getMark())
				continue;

			size += entry.size();

			getEntryLog(entry, adr, string);

			memTrackerLog("MemoryTracker", "%s", string.c_str());

			int str_len = Math::min(16, (int)entry.size());
			char asci_buf[17];
			memset(asci_buf, 0, 17);
			memcpy(asci_buf, adr, str_len);

			string.cat("Data: <", asci_buf, ">");
			for (int j = 0; j < str_len; j++)
			{
				char hex[4];
				hex[0] = ' ';
				toCStringHex(*((uint8_t*)adr + j), hex + 1, 2);
				hex[3] = 0;
				string.cat(hex);
			}

			memTrackerLog("MemoryTracker", "%s", string.c_str());
		}

		if (0 < size) {
			memTrackerLog("MemoryTracker", "Size of all objects: %u", size);
		}
	}

	MemoryTracker::MemoryTracker()
		: m_spin_mutex(false)
		, m_mark(0)
		, m_allocated_memory(0)
	{
	}

	MemoryTracker::~MemoryTracker()
	{
		Lumix::MemoryTracker::getInstance().dumpDetailed();
	}
} //~namespace Lumix

#endif //~MEM_TRACK
