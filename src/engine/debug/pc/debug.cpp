#include "debug/debug.h"
#include "core/mt/atomic.h"
#include "core/string.h"
#include "core/system.h"
#include "debug/debug.h"
#include "core/pc/simple_win.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <mapi.h>
#include <cstdlib>
#include <cstdio>



#pragma comment(lib, "DbgHelp.lib")


static bool g_is_crash_reporting_enabled = true;


namespace Lumix
{


namespace Debug
{


void debugOutput(const char* message)
{
	OutputDebugString(message);
}


void debugBreak()
{
	DebugBreak();
}


int StackTree::s_instances = 0;


class StackNode
{
public:
	~StackNode()
	{
		delete m_next;
		delete m_first_child;
	}

	void* m_instruction;
	StackNode* m_next;
	StackNode* m_first_child;
	StackNode* m_parent;
};


StackTree::StackTree()
{
	m_root = nullptr;
	if (MT::atomicIncrement(&s_instances) == 1)
	{
		HANDLE process = GetCurrentProcess();
		SymInitialize(process, nullptr, TRUE);
	}
}


StackTree::~StackTree()
{
	delete m_root;
	if (MT::atomicDecrement(&s_instances) == 0)
	{
		HANDLE process = GetCurrentProcess();
		SymCleanup(process);
	}
}


void StackTree::refreshModuleList()
{
	ASSERT(s_instances > 0);
	SymRefreshModuleList(GetCurrentProcess());
}


int StackTree::getPath(StackNode* node, StackNode** output, int max_size)
{
	int i = 0;
	while (i < max_size && node)
	{
		output[i] = node;
		i++;
		node = node->m_parent;
	}
	return i;
}


StackNode* StackTree::getParent(StackNode* node)
{
	return node ? node->m_parent : nullptr;
}


bool StackTree::getFunction(StackNode* node, char* out, int max_size, int* line)
{
	HANDLE process = GetCurrentProcess();
	uint8 symbol_mem[sizeof(SYMBOL_INFO) + 256 * sizeof(char)];
	SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_mem);
	memset(symbol_mem, 0, sizeof(symbol_mem));
	symbol->MaxNameLen = 255;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	BOOL success = SymFromAddr(process, (DWORD64)(node->m_instruction), 0, symbol);
	IMAGEHLP_LINE64 line_info;
	DWORD displacement;
	if (SymGetLineFromAddr64(process, (DWORD64)(node->m_instruction), &displacement, &line_info))
	{
		*line = line_info.LineNumber;
	}
	else
	{
		*line = -1;
	}
	if (success) Lumix::copyString(out, max_size, symbol->Name);

	return success == TRUE;
}


void StackTree::printCallstack(StackNode* node)
{
	while (node)
	{
		HANDLE process = GetCurrentProcess();
		uint8 symbol_mem[sizeof(SYMBOL_INFO) + 256 * sizeof(char)];
		SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_mem);
		memset(symbol_mem, 0, sizeof(symbol_mem));
		symbol->MaxNameLen = 255;
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		BOOL success = SymFromAddr(process, (DWORD64)(node->m_instruction), 0, symbol);
		if (success)
		{
			IMAGEHLP_LINE line;
			DWORD offset;
			if (SymGetLineFromAddr(process, (DWORD64)(node->m_instruction), &offset, &line))
			{
				OutputDebugString("\t");
				OutputDebugString(line.FileName);
				OutputDebugString("(");
				char tmp[20];
				toCString((uint32)line.LineNumber, tmp, sizeof(tmp));
				OutputDebugString(tmp);
				OutputDebugString("):");
			}
			OutputDebugString("\t");
			OutputDebugString(symbol->Name);
			OutputDebugString("\n");
		}
		else
		{
			OutputDebugString("\tN/A\n");
		}
		node = node->m_parent;
	}
}


StackNode* StackTree::insertChildren(StackNode* root_node, void** instruction, void** stack)
{
	StackNode* node = root_node;
	while (instruction >= stack)
	{
		StackNode* new_node = new StackNode();
		node->m_first_child = new_node;
		new_node->m_parent = node;
		new_node->m_next = nullptr;
		new_node->m_first_child = nullptr;
		new_node->m_instruction = *instruction;
		node = new_node;
		--instruction;
	}
	return node;
}


StackNode* StackTree::record()
{
	static const int frames_to_capture = 256;
	void* stack[frames_to_capture];
	USHORT captured_frames_count = CaptureStackBackTrace(2, frames_to_capture, stack, 0);

	void** ptr = stack + captured_frames_count - 1;
	if (!m_root)
	{
		m_root = new StackNode();
		m_root->m_instruction = *ptr;
		m_root->m_first_child = nullptr;
		m_root->m_next = nullptr;
		m_root->m_parent = nullptr;
		--ptr;
		return insertChildren(m_root, ptr, stack);
	}

	StackNode* node = m_root;
	while (ptr >= stack)
	{
		while (node->m_instruction != *ptr && node->m_next)
		{
			node = node->m_next;
		}
		if (node->m_instruction != *ptr)
		{
			node->m_next = new StackNode;
			node->m_next->m_parent = node->m_parent;
			node->m_next->m_instruction = *ptr;
			node->m_next->m_next = nullptr;
			node->m_next->m_first_child = nullptr;
			--ptr;
			return insertChildren(node->m_next, ptr, stack);
		}
		else if (node->m_first_child)
		{
			--ptr;
			node = node->m_first_child;
		}
		else if (ptr != stack)
		{
			--ptr;
			return insertChildren(node, ptr, stack);
		}
		else
		{
			return node;
		}
	}

	return node;
}


static const uint32 UNINITIALIZED_MEMORY_PATTERN = 0xCD;
static const uint32 FREED_MEMORY_PATTERN = 0xDD;
static const uint32 ALLOCATION_GUARD = 0xFDFDFDFD;


Allocator::Allocator(IAllocator& source)
	: m_source(source)
	, m_root(nullptr)
	, m_mutex(false)
	, m_stack_tree(LUMIX_NEW(m_source, Debug::StackTree))
	, m_total_size(0)
	, m_is_fill_enabled(true)
	, m_are_guards_enabled(true)
{
	m_sentinels[0].m_next = &m_sentinels[1];
	m_sentinels[0].m_previous = nullptr;
	m_sentinels[0].m_stack_leaf = nullptr;
	m_sentinels[0].m_size = 0;

	m_sentinels[1].m_next = nullptr;
	m_sentinels[1].m_previous = &m_sentinels[0];
	m_sentinels[1].m_stack_leaf = nullptr;
	m_sentinels[1].m_size = 0;

	m_root = &m_sentinels[1];
}


Allocator::~Allocator()
{
	AllocationInfo* last_sentinel = &m_sentinels[1];
	if (m_root != last_sentinel)
	{
		OutputDebugString("Memory leaks detected!\n");
		AllocationInfo* info = m_root;
		while (info != last_sentinel)
		{
			char tmp[2048];
			sprintf(tmp, "\nAllocation size : %Iu, memory %p\n", info->m_size, info + sizeof(info));
			OutputDebugString(tmp);
			m_stack_tree->printCallstack(info->m_stack_leaf);
			info = info->m_next;
		}
		ASSERT(false);
	}
	LUMIX_DELETE(m_source, m_stack_tree);
}


void Allocator::lock()
{
	m_mutex.lock();
}


void Allocator::unlock()
{
	m_mutex.unlock();
}


void Allocator::checkGuards()
{
	if (m_are_guards_enabled) return;

	auto* info = m_root;
	while (info)
	{
		auto user_ptr = getUserPtrFromAllocationInfo(info);
		void* system_ptr = getSystemFromUser(user_ptr);
		ASSERT(*(uint32*)system_ptr == ALLOCATION_GUARD);
		ASSERT(*(uint32*)((uint8*)user_ptr + info->m_size) == ALLOCATION_GUARD);

		info = info->m_next;
	}
}


size_t Allocator::getAllocationOffset()
{
	return sizeof(AllocationInfo) + (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0);
}


size_t Allocator::getNeededMemory(size_t size)
{
	return size + sizeof(AllocationInfo) +
		   (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) << 1 : 0);
}


Allocator::AllocationInfo* Allocator::getAllocationInfoFromSystem(void* system_ptr)
{
	return (AllocationInfo*)(m_are_guards_enabled ? (uint8*)system_ptr + sizeof(ALLOCATION_GUARD)
												  : system_ptr);
}


void* Allocator::getUserPtrFromAllocationInfo(AllocationInfo* info)
{
	return ((uint8*)info + sizeof(AllocationInfo));
}


Allocator::AllocationInfo* Allocator::getAllocationInfoFromUser(void* user_ptr)
{
	return (AllocationInfo*)((uint8*)user_ptr - sizeof(AllocationInfo));
}


void* Allocator::getUserFromSystem(void* system_ptr)
{
	return (uint8*)system_ptr + (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0) +
		   sizeof(AllocationInfo);
}


void* Allocator::getSystemFromUser(void* user_ptr)
{
	return (uint8*)user_ptr - (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0) -
		   sizeof(AllocationInfo);
}


void* Allocator::reallocate(void* user_ptr, size_t size)
{
#ifndef _DEBUG
	return m_source.reallocate(user_ptr, size);
#else
	if (user_ptr == nullptr) return allocate(size);
	if (size == 0) return nullptr;

	void* new_data = allocate(size);
	if (!new_data) return nullptr;

	AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
	copyMemory(new_data, user_ptr, info->m_size < size ? info->m_size : size);

	deallocate(user_ptr);

	return new_data;
#endif
}


void* Allocator::allocate_aligned(size_t size, size_t align)
{
	return m_source.allocate_aligned(size, align);
}


void Allocator::deallocate_aligned(void* ptr)
{
	m_source.deallocate_aligned(ptr);
}


void* Allocator::reallocate_aligned(void* ptr, size_t size, size_t align)
{
	return m_source.reallocate_aligned(ptr, size, align);
}


void* Allocator::allocate(size_t size)
{
#ifndef _DEBUG
	return m_source.allocate(size);
#else
	void* system_ptr;
	AllocationInfo* info;
	size_t system_size = getNeededMemory(size);
	{
		MT::SpinLock lock(m_mutex);
		system_ptr = m_source.allocate(system_size);
		info = new (NewPlaceholder(), getAllocationInfoFromSystem(system_ptr)) AllocationInfo();

		info->m_previous = m_root->m_previous;
		m_root->m_previous->m_next = info;

		info->m_next = m_root;
		m_root->m_previous = info;

		m_root = info;

		m_total_size += size;
	} // because of the SpinLock

	void* user_ptr = getUserFromSystem(system_ptr);
	info->m_stack_leaf = m_stack_tree->record();
	info->m_size = size;
	if (m_is_fill_enabled)
	{
		memset(user_ptr, UNINITIALIZED_MEMORY_PATTERN, size);
	}

	if (m_are_guards_enabled)
	{
		*(uint32*)system_ptr = ALLOCATION_GUARD;
		*(uint32*)((uint8*)system_ptr + system_size - sizeof(ALLOCATION_GUARD)) = ALLOCATION_GUARD;
	}

	return user_ptr;
#endif
}

void Allocator::deallocate(void* user_ptr)
{
#ifndef _DEBUG
	m_source.deallocate(user_ptr);
#else
	if (user_ptr)
	{
		AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
		void* system_ptr = getSystemFromUser(user_ptr);
		if (m_is_fill_enabled)
		{
			memset(user_ptr, FREED_MEMORY_PATTERN, info->m_size);
		}

		if (m_are_guards_enabled)
		{
			ASSERT(*(uint32*)system_ptr == ALLOCATION_GUARD);
			ASSERT(*(uint32*)((uint8*)user_ptr + info->m_size) == ALLOCATION_GUARD);
		}

		{
			MT::SpinLock lock(m_mutex);
			if (info == m_root)
			{
				m_root = info->m_next;
			}
			info->m_previous->m_next = info->m_next;
			info->m_next->m_previous = info->m_previous;

			m_total_size -= info->m_size;
		} // because of the SpinLock

		info->~AllocationInfo();

		m_source.deallocate((void*)system_ptr);
	}
#endif
}


} // namespace Debug


BOOL SendFile(LPCSTR lpszSubject,
	LPCSTR lpszTo,
	LPCSTR lpszName,
	LPCSTR lpszText,
	LPCSTR lpszFullFileName)
{
	HINSTANCE hMAPI = ::LoadLibrary("mapi32.dll");
	if (!hMAPI) return FALSE;
	LPMAPISENDMAIL lpfnMAPISendMail = (LPMAPISENDMAIL)::GetProcAddress(hMAPI, "MAPISendMail");

	char szDrive[_MAX_DRIVE] = {0};
	char szDir[_MAX_DIR] = {0};
	char szName[_MAX_FNAME] = {0};
	char szExt[_MAX_EXT] = {0};
	_splitpath_s(lpszFullFileName, szDrive, szDir, szName, szExt);

	char szFileName[MAX_PATH] = {0};
	strcat_s(szFileName, szName);
	strcat_s(szFileName, szExt);

	char szFullFileName[MAX_PATH] = {0};
	strcat_s(szFullFileName, lpszFullFileName);

	MapiFileDesc MAPIfile = {0};
	ZeroMemory(&MAPIfile, sizeof(MapiFileDesc));
	MAPIfile.nPosition = 0xFFFFFFFF;
	MAPIfile.lpszPathName = szFullFileName;
	MAPIfile.lpszFileName = szFileName;

	char szTo[MAX_PATH] = {0};
	strcat_s(szTo, lpszTo);

	char szNameTo[MAX_PATH] = {0};
	strcat_s(szNameTo, lpszName);

	MapiRecipDesc recipient = {0};
	recipient.ulRecipClass = MAPI_TO;
	recipient.lpszAddress = szTo;
	recipient.lpszName = szNameTo;

	char szSubject[MAX_PATH] = {0};
	strcat_s(szSubject, lpszSubject);

	char szText[MAX_PATH] = {0};
	strcat_s(szText, lpszText);

	MapiMessage MAPImsg = {0};
	MAPImsg.lpszSubject = szSubject;
	MAPImsg.lpRecips = &recipient;
	MAPImsg.nRecipCount = 1;
	MAPImsg.lpszNoteText = szText;
	MAPImsg.nFileCount = 1;
	MAPImsg.lpFiles = &MAPIfile;

	ULONG nSent = lpfnMAPISendMail(0, 0, &MAPImsg, 0, 0);

	FreeLibrary(hMAPI);
	return (nSent == SUCCESS_SUCCESS || nSent == MAPI_E_USER_ABORT);
}


static void getStack(CONTEXT& context, char* out, int max_size)
{
	BOOL result;
	HANDLE process;
	HANDLE thread;
	STACKFRAME64 stack;
	char symbol_mem[sizeof(IMAGEHLP_SYMBOL64) + 256];
	IMAGEHLP_SYMBOL64* symbol = (IMAGEHLP_SYMBOL64*)symbol_mem;
	DWORD64 displacement;
	char name[256];
	copyString(out, max_size, "Crash callstack:\n");
	memset(&stack, 0, sizeof(STACKFRAME64));

	process = GetCurrentProcess();
	thread = GetCurrentThread();
	displacement = 0;
	DWORD machineType;
#ifdef _WIN64
	machineType = IMAGE_FILE_MACHINE_IA64;
	stack.AddrPC.Offset = context.Rip;
	stack.AddrPC.Mode = AddrModeFlat;
	stack.AddrStack.Offset = context.Rsp;
	stack.AddrStack.Mode = AddrModeFlat;
	stack.AddrFrame.Offset = context.Rbp;
	stack.AddrFrame.Mode = AddrModeFlat;
#else
	machineType = IMAGE_FILE_MACHINE_I386;
	stack.AddrPC.Offset = context.Eip;
	stack.AddrPC.Mode = AddrModeFlat;
	stack.AddrStack.Offset = context.Esp;
	stack.AddrStack.Mode = AddrModeFlat;
	stack.AddrFrame.Offset = context.Ebp;
	stack.AddrFrame.Mode = AddrModeFlat;
#endif

	do
	{
		result = StackWalk64(machineType,
			process,
			thread,
			&stack,
			&context,
			NULL,
			SymFunctionTableAccess64,
			SymGetModuleBase64,
			NULL);

		symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
		symbol->MaxNameLength = 255;

		SymGetSymFromAddr64(process, (ULONG64)stack.AddrPC.Offset, &displacement, symbol);
		UnDecorateSymbolName(symbol->Name, (PSTR)name, 256, UNDNAME_COMPLETE);

		catString(out, max_size, symbol->Name);
		catString(out, max_size, "\n");

	} while (result);
}


static LONG WINAPI unhandledExceptionHandler(LPEXCEPTION_POINTERS info)
{
	if (!g_is_crash_reporting_enabled) return EXCEPTION_CONTINUE_SEARCH;

	struct CrashInfo
	{
		LPEXCEPTION_POINTERS info;
		DWORD thread_id;
	};

	auto dumper = [](void* data) -> DWORD {

		auto info = ((CrashInfo*)data)->info;
		char message[4096];
		getStack(*info->ContextRecord, message, sizeof(message));
		messageBox(message);

		char minidump_path[Lumix::MAX_PATH_LENGTH];
		GetCurrentDirectory(sizeof(minidump_path), minidump_path);
		Lumix::catString(minidump_path, "\\minidump.dmp");

		HANDLE process = GetCurrentProcess();
		DWORD process_id = GetProcessId(process);
		HANDLE file = CreateFile(
			minidump_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		MINIDUMP_TYPE minidump_type = (MINIDUMP_TYPE)(
			MiniDumpWithFullMemoryInfo | MiniDumpFilterMemory | MiniDumpWithHandleData |
			MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);

		MINIDUMP_EXCEPTION_INFORMATION minidump_exception_info;
		minidump_exception_info.ThreadId = ((CrashInfo*)data)->thread_id;
		minidump_exception_info.ExceptionPointers = info;
		minidump_exception_info.ClientPointers = FALSE;

		MiniDumpWriteDump(process,
			process_id,
			file,
			minidump_type,
			info ? &minidump_exception_info : nullptr,
			nullptr,
			nullptr);
		CloseHandle(file);

		SendFile("Lumix Studio crash",
			"SMTP:mikulas.florek@gamedev.sk",
			"Lumix Studio",
			message,
			minidump_path);

		minidump_type = (MINIDUMP_TYPE)(MiniDumpWithFullMemory | MiniDumpWithFullMemoryInfo |
			MiniDumpFilterMemory | MiniDumpWithHandleData |
			MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
		file = CreateFile(
			"fulldump.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		MiniDumpWriteDump(process,
			process_id,
			file,
			minidump_type,
			info ? &minidump_exception_info : nullptr,
			nullptr,
			nullptr);
		CloseHandle(file);
		return 0;
	};

	DWORD thread_id;
	CrashInfo crash_info = { info, GetCurrentThreadId() };
	auto handle = CreateThread(0, 0x8000, dumper, &crash_info, 0, &thread_id);
	WaitForSingleObject(handle, INFINITE);

	return EXCEPTION_CONTINUE_SEARCH;
}


void enableCrashReporting(bool enable)
{
	g_is_crash_reporting_enabled = enable;
}


void installUnhandledExceptionHandler()
{
	SetUnhandledExceptionFilter(unhandledExceptionHandler);
}


} // namespace Lumix