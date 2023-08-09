#define NOGDI
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma warning (push)
#pragma warning (disable: 4091) // declaration of 'xx' hides previous local declaration
#include <DbgHelp.h>
#pragma warning (pop)
#include <mapi.h>

#include "engine/allocators.h"
#include "engine/crt.h"
#include "engine/debug.h"
#include "engine/log.h"
#include "engine/atomic.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/string.h"


#pragma comment(lib, "DbgHelp.lib")


static bool g_is_crash_reporting_enabled = false;


namespace Lumix
{


namespace debug
{


void enableFloatingPointTraps(bool enable)
{
	unsigned int cw = _control87(0, 0) & MCW_EM;
	if (enable)
	{
		cw &= ~(_EM_OVERFLOW | _EM_ZERODIVIDE | _EM_INVALID | _EM_DENORMAL); // can not enable _EM_INEXACT because it is common in QT
	}
	else
	{
		cw |= _EM_OVERFLOW | _EM_INVALID | _EM_DENORMAL; // can not enable _EM_INEXACT because it is common in QT
	}
	_control87(cw, MCW_EM);
}


void debugOutput(const char* message)
{
	OutputDebugString(message);
}


void debugBreak()
{
	DebugBreak();
}


int StackTree::s_instances = 0;


struct StackNode
{
	~StackNode()
	{
		LUMIX_DELETE(getGlobalAllocator(), m_next);
		LUMIX_DELETE(getGlobalAllocator(), m_first_child);
	}

	void* m_instruction;
	StackNode* m_next = nullptr;
	StackNode* m_first_child = nullptr;
	StackNode* m_parent;
};


StackTree::StackTree()
{
	m_root = nullptr;
	if (atomicIncrement(&s_instances) == 1)
	{
		HANDLE process = GetCurrentProcess();
		SymInitialize(process, nullptr, TRUE);
	}
}


StackTree::~StackTree()
{
	LUMIX_DELETE(getGlobalAllocator(), m_root);
	if (atomicDecrement(&s_instances) == 0)
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


int StackTree::getPath(StackNode* node, Span<StackNode*> output)
{
	u32 i = 0;
	while (i < output.length() && node)
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


bool StackTree::getFunction(StackNode* node, Span<char> out, int& line)
{
	HANDLE process = GetCurrentProcess();
	alignas(SYMBOL_INFO) u8 symbol_mem[sizeof(SYMBOL_INFO) + 256 * sizeof(char)] = {};
	SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_mem);
	symbol->MaxNameLen = 255;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	BOOL success = SymFromAddr(process, (DWORD64)(node->m_instruction), 0, symbol);
	IMAGEHLP_LINE64 line_info;
	DWORD displacement;
	if (SymGetLineFromAddr64(process, (DWORD64)(node->m_instruction), &displacement, &line_info))
	{
		line = line_info.LineNumber;
	}
	else
	{
		line = -1;
	}
	if (success) copyString(out, symbol->Name);

	return success != FALSE;
}


void StackTree::printCallstack(StackNode* node)
{
	while (node)
	{
		HANDLE process = GetCurrentProcess();
		alignas(SYMBOL_INFO) u8 symbol_mem[sizeof(SYMBOL_INFO) + 256 * sizeof(char)];
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
				toCString((u32)line.LineNumber, Span(tmp));
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
		StackNode* new_node = LUMIX_NEW(getGlobalAllocator(), StackNode)();
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
	if (!m_root) {
		m_root = LUMIX_NEW(getGlobalAllocator(), StackNode)();
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
			node->m_next = LUMIX_NEW(getGlobalAllocator(), StackNode);
			node->m_next->m_parent = node->m_parent;
			node->m_next->m_instruction = *ptr;
			node->m_next->m_next = nullptr;
			node->m_next->m_first_child = nullptr;
			--ptr;
			return insertChildren(node->m_next, ptr, stack);
		}
		
		if (node->m_first_child)
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


static const u32 UNINITIALIZED_MEMORY_PATTERN = 0xCD;
static const u32 FREED_MEMORY_PATTERN = 0xDD;
static const u32 ALLOCATION_GUARD = 0xFDFDFDFD;

void* GuardAllocator::allocate_aligned(size_t size, size_t align) {
	const size_t pages = 1 + ((size + 4095) >> 12);
	void* mem = VirtualAlloc(nullptr, pages * 4096, MEM_RESERVE, PAGE_READWRITE);
	VirtualAlloc(mem, (pages - 1) * 4096, MEM_COMMIT, PAGE_READWRITE);
	
	if (align == 4096) return mem;

	u8* ptr = (u8*)mem;
	return (void*)(uintptr_t(ptr + (pages - 1) * 4096 - size) & ~size_t(align - 1));
}

void GuardAllocator::deallocate_aligned(void* ptr) {
	VirtualFree((void*)((uintptr_t)ptr & ~(size_t)4095), 0, MEM_RELEASE);
}

Allocator::Allocator(IAllocator& source)
	: m_source(source)
	, m_root(nullptr)
	, m_total_size(0)
	, m_is_fill_enabled(true)
	, m_are_guards_enabled(true)
{
	m_sentinels[0].next = &m_sentinels[1];
	m_sentinels[0].previous = nullptr;
	m_sentinels[0].stack_leaf = nullptr;
	m_sentinels[0].size = 0;
	m_sentinels[0].align = 0;

	m_sentinels[1].next = nullptr;
	m_sentinels[1].previous = &m_sentinels[0];
	m_sentinels[1].stack_leaf = nullptr;
	m_sentinels[1].size = 0;
	m_sentinels[1].align = 0;

	m_root = &m_sentinels[1];
}


void Allocator::checkLeaks()
{
	AllocationInfo* last_sentinel = &m_sentinels[1];
	if (m_root != last_sentinel)
	{
		OutputDebugString("Memory leaks detected!\n");
		AllocationInfo* info = m_root;
		while (info != last_sentinel)
		{
			StaticString<2048> tmp("\nAllocation size : ", info->size, " , memory ", (u64)(info + sizeof(info)), "\n");
			OutputDebugString(tmp);
			m_stack_tree.printCallstack(info->stack_leaf);
			info = info->next;
		}
		debugBreak();
	}
}


Allocator::~Allocator()
{
	checkLeaks();
}


void Allocator::lock()
{
	m_mutex.enter();
}


void Allocator::unlock()
{
	m_mutex.exit();
}


void Allocator::checkGuards()
{
	if (m_are_guards_enabled) return;

	auto* info = m_root;
	while (info)
	{
		auto user_ptr = getUserPtrFromAllocationInfo(info);
		void* system_ptr = getSystemFromUser(user_ptr);
		ASSERT(*(u32*)system_ptr == ALLOCATION_GUARD);
		ASSERT(*(u32*)((u8*)user_ptr + info->size) == ALLOCATION_GUARD);

		info = info->next;
	}
}


size_t Allocator::getAllocationOffset()
{
	return sizeof(AllocationInfo) + (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0);
}


size_t Allocator::getNeededMemory(size_t size)
{
	return size + sizeof(AllocationInfo) + (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) << 1 : 0);
}


size_t Allocator::getNeededMemory(size_t size, size_t align)
{
	return size + sizeof(AllocationInfo) + (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) << 1 : 0) +
		   align;
}


Allocator::AllocationInfo* Allocator::getAllocationInfoFromSystem(void* system_ptr)
{
	return (AllocationInfo*)(m_are_guards_enabled ? (u8*)system_ptr + sizeof(ALLOCATION_GUARD)
												  : system_ptr);
}


void* Allocator::getUserPtrFromAllocationInfo(AllocationInfo* info)
{
	return ((u8*)info + sizeof(AllocationInfo));
}


Allocator::AllocationInfo* Allocator::getAllocationInfoFromUser(void* user_ptr)
{
	return (AllocationInfo*)((u8*)user_ptr - sizeof(AllocationInfo));
}


u8* Allocator::getUserFromSystem(void* system_ptr, size_t align)
{
	size_t diff = (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0) + sizeof(AllocationInfo);

	if (align) diff += (align - diff % align) % align;
	return (u8*)system_ptr + diff;
}


u8* Allocator::getSystemFromUser(void* user_ptr)
{
	AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
	size_t diff = (m_are_guards_enabled ? sizeof(ALLOCATION_GUARD) : 0) + sizeof(AllocationInfo);
	if (info->align) diff += (info->align - diff % info->align) % info->align;
	return (u8*)user_ptr - diff;
}


void* Allocator::reallocate(void* user_ptr, size_t new_size, size_t old_size)
{
	if (user_ptr == nullptr) return allocate(new_size);
	if (new_size == 0) return nullptr;

	void* new_data = allocate(new_size);
	if (!new_data) return nullptr;

	AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
	memcpy(new_data, user_ptr, info->size < new_size ? info->size : new_size);

	deallocate(user_ptr);

	return new_data;
}


void* Allocator::allocate_aligned(size_t size, size_t align)
{
	void* system_ptr;
	AllocationInfo* info;
	u8* user_ptr;

	size_t system_size = getNeededMemory(size, align);

	m_mutex.enter();
	system_ptr = m_source.allocate_aligned(system_size, align);
	user_ptr = getUserFromSystem(system_ptr, align);
	info = new (NewPlaceholder(), getAllocationInfoFromUser(user_ptr)) AllocationInfo();

	info->previous = m_root->previous;
	m_root->previous->next = info;

	info->next = m_root;
	m_root->previous = info;

	m_root = info;

	m_total_size += size;
	m_mutex.exit();

	info->tag = TagAllocator::active_allocator;
	info->align = u16(align);
	info->stack_leaf = m_stack_tree.record();
	info->size = size;
	if (m_is_fill_enabled)
	{
		memset(user_ptr, UNINITIALIZED_MEMORY_PATTERN, size);
	}

	if (m_are_guards_enabled)
	{
		*(u32*)system_ptr = ALLOCATION_GUARD;
		*(u32*)((u8*)system_ptr + system_size - sizeof(ALLOCATION_GUARD)) = ALLOCATION_GUARD;
	}

	return user_ptr;
}


void Allocator::deallocate_aligned(void* user_ptr)
{
	if (user_ptr)
	{
		AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
		void* system_ptr = getSystemFromUser(user_ptr);
		if (m_are_guards_enabled)
		{
			ASSERT(*(u32*)system_ptr == ALLOCATION_GUARD);
			size_t system_size = getNeededMemory(info->size, info->align);
			ASSERT(*(u32*)((u8*)system_ptr + system_size - sizeof(ALLOCATION_GUARD)) == ALLOCATION_GUARD);
		}

		if (m_is_fill_enabled)
		{
			memset(user_ptr, FREED_MEMORY_PATTERN, info->size);
		}

		{
			MutexGuard lock(m_mutex);
			if (info == m_root)
			{
				m_root = info->next;
			}
			info->previous->next = info->next;
			info->next->previous = info->previous;

			m_total_size -= info->size;
		} // because of the lock

		info->~AllocationInfo();

		m_source.deallocate_aligned((void*)system_ptr);
	}
}


void* Allocator::reallocate_aligned(void* user_ptr, size_t new_size, size_t old_size, size_t align)
{
	if (user_ptr == nullptr) return allocate_aligned(new_size, align);
	if (new_size == 0) return nullptr;

	void* new_data = allocate_aligned(new_size, align);
	if (!new_data) return nullptr;

	AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
	memcpy(new_data, user_ptr, info->size < new_size ? info->size : new_size);

	deallocate_aligned(user_ptr);

	return new_data;
}


void* Allocator::allocate(size_t size)
{
	void* system_ptr;
	AllocationInfo* info;
	size_t system_size = getNeededMemory(size);
	{
		MutexGuard lock(m_mutex);
		system_ptr = m_source.allocate(system_size);
		info = new (NewPlaceholder(), getAllocationInfoFromSystem(system_ptr)) AllocationInfo();

		info->previous = m_root->previous;
		m_root->previous->next = info;

		info->next = m_root;
		m_root->previous = info;

		m_root = info;

		m_total_size += size;
	} // because of the lock

	void* user_ptr = getUserFromSystem(system_ptr, 0);
	info->stack_leaf = m_stack_tree.record();
	info->size = size;
	info->align = 0;
	info->tag = TagAllocator::active_allocator;
	if (m_is_fill_enabled)
	{
		memset(user_ptr, UNINITIALIZED_MEMORY_PATTERN, size);
	}

	if (m_are_guards_enabled)
	{
		*(u32*)system_ptr = ALLOCATION_GUARD;
		*(u32*)((u8*)system_ptr + system_size - sizeof(ALLOCATION_GUARD)) = ALLOCATION_GUARD;
	}

	return user_ptr;
}

void Allocator::deallocate(void* user_ptr)
{
	if (user_ptr)
	{
		AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
		void* system_ptr = getSystemFromUser(user_ptr);
		if (m_is_fill_enabled)
		{
			memset(user_ptr, FREED_MEMORY_PATTERN, info->size);
		}

		if (m_are_guards_enabled)
		{
			ASSERT(*(u32*)system_ptr == ALLOCATION_GUARD);
			size_t system_size = getNeededMemory(info->size);
			ASSERT(*(u32*)((u8*)system_ptr + system_size - sizeof(ALLOCATION_GUARD)) == ALLOCATION_GUARD);
		}

		{
			MutexGuard lock(m_mutex);
			if (info == m_root)
			{
				m_root = info->next;
			}
			info->previous->next = info->next;
			info->next->previous = info->previous;

			m_total_size -= info->size;
		} // because of the lock

		info->~AllocationInfo();

		m_source.deallocate((void*)system_ptr);
	}
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

	PathInfo fi(lpszFullFileName);

	StaticString<MAX_PATH> szFileName(fi.basename, ".", fi.extension);

	char szFullFileName[MAX_PATH] = {0};
	strcat_s(szFullFileName, lpszFullFileName);

	MapiFileDesc MAPIfile = {0};
	ZeroMemory(&MAPIfile, sizeof(MapiFileDesc));
	MAPIfile.nPosition = 0xFFFFFFFF;
	MAPIfile.lpszPathName = szFullFileName;
	MAPIfile.lpszFileName = szFileName.data;

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

static void getStack(CONTEXT& context, Span<char> out)
{
	BOOL result;
	STACKFRAME64 stack;
	alignas(IMAGEHLP_SYMBOL64) char symbol_mem[sizeof(IMAGEHLP_SYMBOL64) + 256];
	IMAGEHLP_SYMBOL64* symbol = (IMAGEHLP_SYMBOL64*)symbol_mem;
	char name[256];
	copyString(out, "Crash callstack:\n");
	memset(&stack, 0, sizeof(STACKFRAME64));

	HANDLE process = GetCurrentProcess();
	HANDLE thread = GetCurrentThread();
	DWORD64 displacement = 0;
	DWORD machineType;

#ifdef _M_X64
	machineType = IMAGE_FILE_MACHINE_AMD64;
	stack.AddrPC.Offset = context.Rip;
	stack.AddrPC.Mode = AddrModeFlat;
	stack.AddrStack.Offset = context.Rsp;
	stack.AddrStack.Mode = AddrModeFlat;
	stack.AddrFrame.Offset = context.Rbp;
	stack.AddrFrame.Mode = AddrModeFlat;
#elif defined _M_IA64
	#error not supported
	machineType = IMAGE_FILE_MACHINE_IA64;
	stack.AddrPC.Offset = context.StIIP;
	stack.AddrPC.Mode = AddrModeFlat;
	stack.AddrFrame.Offset = context.IntSp;
	stack.AddrFrame.Mode = AddrModeFlat;
	stack.AddrBStore.Offset = context.RsBSP;
	stack.AddrBStore.Mode = AddrModeFlat;
	stack.AddrStack.Offset = context.IntSp;
	stack.AddrStack.Mode = AddrModeFlat;
#else
	#error not supported
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

		BOOL symbol_valid = SymGetSymFromAddr64(process, (ULONG64)stack.AddrPC.Offset, &displacement, symbol);
		if (!symbol_valid) return;
		DWORD num_char = UnDecorateSymbolName(symbol->Name, (PSTR)name, 256, UNDNAME_COMPLETE);

		if (num_char == 0) return;
		catString(out, symbol->Name);
		catString(out, "\n");

	} while (result);
}


static LONG WINAPI unhandledExceptionHandler(LPEXCEPTION_POINTERS info)
{
	if (!g_is_crash_reporting_enabled) return EXCEPTION_CONTINUE_SEARCH;

	HANDLE process = GetCurrentProcess();
	SymInitialize(process, nullptr, TRUE);
	debug::StackTree::refreshModuleList();

	struct CrashInfo
	{
		LPEXCEPTION_POINTERS info;
		DWORD thread_id;
	};

	auto dumper = [](void* data) -> DWORD {
		LPEXCEPTION_POINTERS info = ((CrashInfo*)data)->info;
		uintptr base = (uintptr)GetModuleHandle(NULL);
		StaticString<4096> message;
		if(info)
		{
			getStack(*info->ContextRecord, Span(message.data));
			message.append("\nCode: ", (u32)info->ExceptionRecord->ExceptionCode);
			message.append("\nAddress: ", (uintptr)info->ExceptionRecord->ExceptionAddress);
			message.append("\nBase: ", (uintptr)base);
			os::messageBox(message);
		}
		else
		{
			message = "";
		}

		char minidump_path[MAX_PATH];
		GetCurrentDirectory(sizeof(minidump_path), minidump_path);
		catString(minidump_path, "\\minidump.dmp");

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

		SendFile("Lumix Studio crash",
			"SMTP:mikulas.florek@gamedev.sk",
			"Lumix Studio",
			message,
			minidump_path);
		return 0;
	};

	DWORD thread_id;
	CrashInfo crash_info = { info, GetCurrentThreadId() };
	auto handle = CreateThread(0, 0x8000, dumper, &crash_info, 0, &thread_id);
	WaitForSingleObject(handle, INFINITE);

	StaticString<4096> message;
	getStack(*info->ContextRecord, Span(message.data));
	logError(message);

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


void clearHardwareBreakpoint(u32 breakpoint_idx) {
	HANDLE thread = GetCurrentThread();
	CONTEXT ctx = {};
	ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
 
	if (GetThreadContext(thread, &ctx)) {
		switch(breakpoint_idx) {
			case 0: ctx.Dr0 = 0; break;
			case 1: ctx.Dr1 = 0; break;
			case 2: ctx.Dr2 = 0; break;
			case 3: ctx.Dr3 = 0; break;
			default: ASSERT(false); break;
		}
	}
	ctx.Dr7 = ctx.Dr7 & ~(0b11 << (breakpoint_idx * 2));
	ctx.Dr7 = ctx.Dr7 & ~(0b1111 << (breakpoint_idx * 2 + 16));
	BOOL res = SetThreadContext(thread, &ctx);
	ASSERT(res);
}

void setHardwareBreakpoint(u32 breakpoint_idx, const void* mem, u32 size) {
	ASSERT(breakpoint_idx < 4);

	HANDLE thread = GetCurrentThread();
	CONTEXT ctx = {};
	ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
 
	if (GetThreadContext(thread, &ctx)) {
		switch(breakpoint_idx) {
			case 0: ctx.Dr0 = DWORD64(mem); break;
			case 1: ctx.Dr1 = DWORD64(mem); break;
			case 2: ctx.Dr2 = DWORD64(mem); break;
			case 3: ctx.Dr3 = DWORD64(mem); break;
			default: ASSERT(false); break;
		}
	}
	
	ctx.Dr7 = (1 << (breakpoint_idx * 2)) 
		| (0b01 << (breakpoint_idx * 4 + 16)) 
		| (0b11 << (breakpoint_idx * 4 + 18));
	BOOL res = SetThreadContext(thread, &ctx);
	ASSERT(res);
}


} // namespace Lumix
