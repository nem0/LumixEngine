#define NOGDI
#define WIN32_LEAN_AND_MEAN
#define _AMD64_
#include <windows.h>
#include <windef.h>
#include <winbase.h>
#include <winver.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <cstdio>
#pragma warning (push)
#pragma warning (disable: 4091) // declaration of 'xx' hides previous local declaration
#include <DbgHelp.h>
#pragma warning (pop)

#include "core/atomic.h"
#include "core/crt.h"
#include "core/debug.h"
#include "core/defer.h"
#include "core/log.h"
#include "core/os.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/stack_tree.h"
#include "core/string.h"
#include "core/tag_allocator.h"


#pragma comment(lib, "DbgHelp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")


#define LUMIX_ALLOC_GUARDS



namespace Lumix {
	
static CrashReportFlags g_crash_report_flags = CrashReportFlags::ENABLED;

namespace sentry {

struct SentryData {
	IAllocator* s_allocator = nullptr;
	StaticString<256*1024> envelope;
	char log_data[131072];
};

static SentryData* s_sentry_data = nullptr;

static void sendToSentry(const char* message) {
	// https://sentry.io/settings/lumixengine/projects/native/keys/
	static const char* key = "b3fa911595fa4631eabf0ec1f8618ad9";
	static const char* host = "o4510992885809152.ingest.de.sentry.io";
	static const char* project = "4510992907501648";

	auto readLogFile = [](char* data, u32 max_size) -> u32 {
		auto readFromPath = [data, max_size](const char* path) -> u32 {
			HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) return 0;

			LARGE_INTEGER size;
			if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0) {
				CloseHandle(file);
				return 0;
			}

			DWORD to_read = size.QuadPart > max_size ? max_size : (DWORD)size.QuadPart;
			DWORD read = 0;
			if (!ReadFile(file, data, to_read, &read, nullptr)) {
				CloseHandle(file);
				return 0;
			}
			CloseHandle(file);
			return read;
		};

		auto readFromDir = [&readFromPath](const char* dir, const char* relative) -> u32 {
			StaticString<MAX_PATH> path(dir, "/", relative);
			return readFromPath(path.data);
		};

		if (u32 read = readFromPath("lumix.log")) return read;

		char current_dir[MAX_PATH];
		os::getCurrentDirectory(Span(current_dir));
		if (u32 read = readFromDir(current_dir, "lumix.log")) return read;
		if (u32 read = readFromDir(current_dir, "data/lumix.log")) return read;

		char exe_path[MAX_PATH];
		os::getExecutablePath(Span(exe_path));
		StaticString<MAX_PATH> exe_dir(Path::getDir(exe_path));
		if (u32 read = readFromDir(exe_dir.data, "lumix.log")) return read;
		if (u32 read = readFromDir(exe_dir.data, "data/lumix.log")) return read;

		StringView d = Path::getDir(Path::getDir(Path::getDir(Path::getDir(Path::getDir(exe_dir.data)))));
		if (d.size() > 0) {
			StaticString<MAX_PATH> repo_data(d, "/data/lumix.log");
			if (u32 read = readFromPath(repo_data.data)) return read;
		}

		return 0;
	};

	// Build event id as UUIDv4 without dashes (32 lowercase hex chars)
	u8 uuid[16];
	if (BCryptGenRandom(nullptr, uuid, sizeof(uuid), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return;
	uuid[6] = (uuid[6] & 0x0F) | 0x40;
	uuid[8] = (uuid[8] & 0x3F) | 0x80;
	char event_id[33];
	for (int i = 0; i < 16; ++i) {
		u8 v = uuid[i];
		event_id[i * 2] = "0123456789abcdef"[(v >> 4) & 0xF];
		event_id[i * 2 + 1] = "0123456789abcdef"[v & 0xF];
	}
	event_id[32] = '\0';

	// Get current timestamp in seconds since epoch
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	LARGE_INTEGER li;
	li.LowPart = ft.dwLowDateTime;
	li.HighPart = ft.dwHighDateTime;
	u64 timestamp = (li.QuadPart - 116444736000000000ULL) / 10000000ULL;

	auto& envelope = s_sentry_data->envelope;
	envelope = "";
	envelope.append("{\"event_id\":\"", event_id, "\",\"dsn\":\"https://", key, "@", host, "/", project, "\"}\n");
	envelope.append("{\"type\":\"event\"}\n");
	envelope.append("{\"event_id\":\"", event_id, "\",\"timestamp\":", (u64)timestamp, ",\"platform\":\"native\",\"message\":\"");

	const char* src = message;
	while (*src) {
		switch (*src) {
			case '"': envelope.append("\\\""); break;
			case '\\': envelope.append("\\\\"); break;
			case '\b': envelope.append("\\b"); break;
			case '\f': envelope.append("\\f"); break;
			case '\n': envelope.append("\\n"); break;
			case '\r': envelope.append("\\r"); break;
			case '\t': envelope.append("\\t"); break;
			default:
				if ((u8)*src < 0x20) {
					char hex[3];
					toCStringHex((u8)*src, Span(hex));
					hex[2] = '\0';
					envelope.append("\\u00", hex);
				}
				else {
					envelope.append(*src);
				}
				break;
		}
		++src;
	}
	envelope.append("\",\"level\":\"fatal\"}\n");

	const u32 log_data_size = readLogFile(s_sentry_data->log_data, 131072);
	if (log_data_size > 0) {
		envelope.append("{\"type\":\"attachment\",\"length\":", log_data_size, ",\"filename\":\"lumix.log\",\"content_type\":\"text/plain\"}\n");
		envelope.append(StringView(s_sentry_data->log_data, s_sentry_data->log_data + log_data_size), "\n");
	}

	// Convert to wide
	wchar_t whost[256];
	if (!MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, 256)) return;

	// Build URL
	StaticString<1024> narrow_url;
	narrow_url.append("/api/", project, "/envelope/?sentry_key=", key, "&sentry_version=7");
	wchar_t url[1024];
	if (!MultiByteToWideChar(CP_UTF8, 0, narrow_url.data, -1, url, 1024)) return;

	// Send HTTP POST
	HINTERNET hSession = WinHttpOpen(L"LumixEngine/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) return;
	WinHttpSetTimeouts(hSession, 2000, 2000, 2000, 2000);
	HINTERNET hConnect = WinHttpConnect(hSession, whost, INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) {
		WinHttpCloseHandle(hSession);
		return;
	}
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", url, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest) {
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return;
	}
	wchar_t wheaders[] = L"Content-Type: application/x-sentry-envelope\r\n";
	if (!WinHttpSendRequest(hRequest, wheaders, -1, (LPVOID)envelope.data, (DWORD)strlen(envelope.data), (DWORD)strlen(envelope.data), 0)) {
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return;
	}
	if (!WinHttpReceiveResponse(hRequest, NULL)) {
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return;
	}
	DWORD status_code = 0;
	DWORD status_code_size = sizeof(status_code);
	if (!WinHttpQueryHeaders(hRequest,
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX,
		&status_code,
		&status_code_size,
		WINHTTP_NO_HEADER_INDEX)) {
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return;
	}
	if (status_code < 200 || status_code >= 300) {
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return;
	}
	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
}

} // namespace sentry

namespace debug {


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


AtomicI32 StackTree::s_instances = 0;


struct StackNode {
	void* m_instruction;
	StackNode* m_next = nullptr;
	StackNode* m_first_child = nullptr;
	StackNode* m_parent;
};


StackTree::StackTree(IAllocator& allocator)
	: m_allocator(256 * 1024 * 1024, allocator, "Stack tree")
{
	m_root = nullptr;
	if (s_instances.inc() == 0)
	{
		HANDLE process = GetCurrentProcess();
		SymInitialize(process, nullptr, TRUE);
	}
}


StackTree::~StackTree() {
	m_allocator.reset();
	if (s_instances.dec() == 1) {
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
	if (!node) return false;

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
		if (success) {
			IMAGEHLP_LINE64 line;
			DWORD offset;
			line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
			if (SymGetLineFromAddr64(process, (DWORD64)(node->m_instruction), &offset, &line)) {
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
		StackNode* new_node = LUMIX_NEW(m_allocator, StackNode)();
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

StackNode* StackTree::find(void** stack, u32 num) {
	m_srw_lock.enterShared();
	defer { m_srw_lock.exitShared(); };

	void** ptr = stack + num - 1;
	StackNode* node = m_root;

	while (node) {
		if (node->m_instruction != *ptr) {
			node = node->m_next;
			continue;
		}

		if (ptr != stack) {
			--ptr;
			node = node->m_first_child;
			continue;
		}
		
		return node;
	}
	
	return nullptr;
}

StackNode* StackTree::record()
{
	thread_local AtomicI32 is_recording = 0;
	if (!is_recording.compareExchange(1, 0)) {
		// Avoid recursive record(), since this function allocates and that in turn calls record() again.
		return nullptr;
	}
	defer { is_recording = 0; };

	static const int frames_to_capture = 256;
	void* stack[frames_to_capture];
	USHORT captured_frames_count = CaptureStackBackTrace(2, frames_to_capture, stack, 0);
	void** ptr = stack + captured_frames_count - 1;

	StackNode* found = find(stack, captured_frames_count);
	if (found) return found;

	m_srw_lock.enterExclusive();
	defer { m_srw_lock.exitExclusive(); };
	if (!m_root) {
		m_root = LUMIX_NEW(m_allocator, StackNode)();
		m_root->m_instruction = *ptr;
		m_root->m_first_child = nullptr;
		m_root->m_next = nullptr;
		m_root->m_parent = nullptr;
		--ptr;
		StackNode* res = insertChildren(m_root, ptr, stack);
		return res;
	}

	StackNode* node = m_root;
	while (ptr >= stack) {
		while (node->m_instruction != *ptr && node->m_next) {
			node = node->m_next;
		}

		if (node->m_instruction != *ptr) {
			node->m_next = LUMIX_NEW(m_allocator, StackNode);
			node->m_next->m_parent = node->m_parent;
			node->m_next->m_instruction = *ptr;
			node->m_next->m_next = nullptr;
			node->m_next->m_first_child = nullptr;
			--ptr;
			StackNode* res = insertChildren(node->m_next, ptr, stack);
			return res;
		}
		
		if (node->m_first_child) {
			--ptr;
			node = node->m_first_child;
		}
		else if (ptr != stack) {
			--ptr;
			StackNode* res = insertChildren(node, ptr, stack);
			return res;
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

void* GuardAllocator::allocate(size_t size, size_t align) {
	const size_t pages = 1 + ((size + 4095) >> 12);
	void* mem = VirtualAlloc(nullptr, pages * 4096, MEM_RESERVE, PAGE_READWRITE);
	VirtualAlloc(mem, (pages - 1) * 4096, MEM_COMMIT, PAGE_READWRITE);
	
	if (align == 4096) return mem;

	u8* ptr = (u8*)mem;
	return (void*)(uintptr_t(ptr + (pages - 1) * 4096 - size) & ~size_t(align - 1));
}

void GuardAllocator::deallocate(void* ptr) {
	VirtualFree((void*)((uintptr_t)ptr & ~(size_t)4095), 0, MEM_RELEASE);
}


static size_t getAllocationOffset()
{
	#ifdef LUMIX_ALLOC_GUARDS
		return sizeof(AllocationInfo) + sizeof(ALLOCATION_GUARD);
	#else
		return sizeof(AllocationInfo);
	#endif
}


static size_t getNeededMemory(size_t size) {
	#ifdef LUMIX_ALLOC_GUARDS
		return size + sizeof(AllocationInfo) + sizeof(ALLOCATION_GUARD) * 2;
	#else
		return size + sizeof(AllocationInfo);
	#endif
}


static size_t getNeededMemory(size_t size, size_t align) {
	#ifdef LUMIX_ALLOC_GUARDS
		return size + sizeof(AllocationInfo) + sizeof(ALLOCATION_GUARD) * 2 + align;
	#else
		return size + sizeof(AllocationInfo) + align;
	#endif
}


static AllocationInfo* getAllocationInfoFromUser(void* user_ptr) {
	return (AllocationInfo*)((u8*)user_ptr - sizeof(AllocationInfo));
}


static u8* getUserFromSystem(void* system_ptr, size_t align)
{
	size_t diff = getAllocationOffset();

	if (align) diff += (align - diff % align) % align;
	return (u8*)system_ptr + diff;
}


static u8* getSystemFromUser(void* user_ptr)
{
	AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
	#ifdef LUMIX_ALLOC_GUARDS
		size_t diff = sizeof(ALLOCATION_GUARD) + sizeof(AllocationInfo);
	#else
		size_t diff = sizeof(AllocationInfo);
	#endif
	if (info->align) diff += (info->align - diff % info->align) % info->align;
	return (u8*)user_ptr - diff;
}

Allocator::Allocator(IAllocator& source)
	: m_source(source)
	, m_is_fill_enabled(true)
{
}

static Local<StackTree> s_stack_tree;

static struct AllocationDebugSystem {
	AllocationInfo* m_root = nullptr;
	Mutex m_mutex;
	AtomicI64 m_total_size = 0;
} s_allocation_debug;

void init(IAllocator& allocator) {
	s_stack_tree.create(allocator);
	sentry::s_sentry_data = LUMIX_NEW(allocator, sentry::SentryData)();
	sentry::s_sentry_data->s_allocator = &allocator;
}

void shutdown() {
	if (sentry::s_sentry_data) {
		LUMIX_DELETE(*sentry::s_sentry_data->s_allocator, sentry::s_sentry_data);
	}
	sentry::s_sentry_data = nullptr;
	s_stack_tree.destroy();
}

void checkLeaks() {
	#ifdef LUMIX_DEBUG
		if (s_allocation_debug.m_root) {
			bool first = true;
			AllocationInfo* info = s_allocation_debug.m_root;
			while (info) {
				 // s_stack_tree uses arena and we can't deallocate it because we might need it to print leak's callstack
				 // so we ignore it "leaking"
				if (info != &s_stack_tree->getAllocator().getAllocationInfo() && !info->is(AllocationInfo::IS_MISC)) {
					if (first) OutputDebugString("Memory leaks detected!\n");
					first = false;
					StaticString<2048> tmp("\nAllocation size : ", info->size, " , memory ", (u64)(info + sizeof(info)), "\n");
					if (info->is(AllocationInfo::IS_VRAM)) tmp.append("VRAM\n");
					OutputDebugString(tmp);
					s_stack_tree->printCallstack(info->stack_leaf);
				}
				info = info->next;
			}
			if (!first) debugBreak();
		}
	#endif
}


static void* getUserPtrFromAllocationInfo(AllocationInfo* info)
{
	return ((u8*)info + sizeof(AllocationInfo));
}

void checkGuards() {
	#ifdef LUMIX_ALLOC_GUARDS
		MutexGuard lock(s_allocation_debug.m_mutex);
		auto* info = s_allocation_debug.m_root;
		while (info) {
			const bool is_vram = info->is(AllocationInfo::IS_VRAM);
			const bool is_paged = info->is(AllocationInfo::IS_PAGED);
			const bool is_misc = info->is(AllocationInfo::IS_MISC);
			const bool is_arena = info->is(AllocationInfo::IS_ARENA);

			if (!is_vram && !is_paged && !is_misc && !is_arena) {
				auto user_ptr = getUserPtrFromAllocationInfo(info);
				void* system_ptr = getSystemFromUser(user_ptr);
				if (*(u32*)system_ptr != ALLOCATION_GUARD) {
					ASSERT(false);
					debugOutput("Error: Memory was overwritten\n");
					s_stack_tree->printCallstack(info->stack_leaf);
				}
				if (*(u32*)((u8*)user_ptr + info->size) != ALLOCATION_GUARD) {
					ASSERT(false);
					debugOutput("Error: Memory was overwritten\n");
					s_stack_tree->printCallstack(info->stack_leaf);
				}
			}

			info = info->next;
		}
	#else
		ASSERT(false);
	#endif
}

const AllocationInfo* lockAllocationInfos() {
	s_allocation_debug.m_mutex.enter();
	return s_allocation_debug.m_root;
}

void unlockAllocationInfos() {
	s_allocation_debug.m_mutex.exit();
}

u64 getRegisteredAllocsSize() {
	return s_allocation_debug.m_total_size;
}

void resizeAlloc(AllocationInfo& info, u64 new_size) {
	MutexGuard guard(s_allocation_debug.m_mutex);
	s_allocation_debug.m_total_size.subtract(info.size);
	info.size = new_size;
	s_allocation_debug.m_total_size.add(info.size);
}

void registerAlloc(AllocationInfo& info) {
	info.stack_leaf = s_stack_tree->record();
	info.previous = nullptr;
	
	MutexGuard guard(s_allocation_debug.m_mutex);
	info.next = s_allocation_debug.m_root;
	if (s_allocation_debug.m_root) {
		s_allocation_debug.m_root->previous = &info;
	}
	s_allocation_debug.m_root = &info;
	if (!info.is(AllocationInfo::IS_VRAM)) {
		s_allocation_debug.m_total_size.add(info.size);
	}
}

void unregisterAlloc(const AllocationInfo& info) {
	MutexGuard lock(s_allocation_debug.m_mutex);
	if (&info == s_allocation_debug.m_root) {
		s_allocation_debug.m_root = info.next;
	}
	if (info.previous) info.previous->next = info.next;
	if (info.next) info.next->previous = info.previous;
	if (!info.is(AllocationInfo::IS_VRAM)) {
		s_allocation_debug.m_total_size.subtract(info.size);
	}
}

void* Allocator::allocate(size_t size, size_t align) {
	const size_t system_size = getNeededMemory(size, align);

	void* const system_ptr = m_source.allocate(system_size, align);
	u8* const user_ptr = getUserFromSystem(system_ptr, align);

	auto* info = new (NewPlaceholder(), getAllocationInfoFromUser(user_ptr)) AllocationInfo();
	info->tag = TagAllocator::getActiveAllocator();
	info->align = u16(align);
	info->size = size;

	registerAlloc(*info);

	m_total_size.add(size);
	if (m_is_fill_enabled) {
		memset(user_ptr, UNINITIALIZED_MEMORY_PATTERN, size);
	}

	#ifdef LUMIX_ALLOC_GUARDS
		*(u32*)system_ptr = ALLOCATION_GUARD;
		*(u32*)((u8*)user_ptr + info->size) = ALLOCATION_GUARD;
	#endif

	return user_ptr;
}

void Allocator::deallocate(void* user_ptr) {
	if (!user_ptr)  return;
	
	AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
	m_total_size.subtract(info->size);
	void* system_ptr = getSystemFromUser(user_ptr);
	#ifdef LUMIX_ALLOC_GUARDS
		ASSERT(*(u32*)system_ptr == ALLOCATION_GUARD);
		ASSERT(*(u32*)((u8*)user_ptr + info->size) == ALLOCATION_GUARD);
	#endif

	if (m_is_fill_enabled) {
		memset(user_ptr, FREED_MEMORY_PATTERN, info->size);
	}

	unregisterAlloc(*info);

	info->~AllocationInfo();

	m_source.deallocate((void*)system_ptr);
}


void* Allocator::reallocate(void* user_ptr, size_t new_size, size_t old_size, size_t align)
{
	if (user_ptr == nullptr) return allocate(new_size, align);
	if (new_size == 0) return nullptr;

	void* new_data = allocate(new_size, align);
	if (!new_data) return nullptr;

	AllocationInfo* info = getAllocationInfoFromUser(user_ptr);
	memcpy(new_data, user_ptr, info->size < new_size ? info->size : new_size);

	deallocate(user_ptr);

	return new_data;
}

} // namespace Debug

static void getStack(CONTEXT& context, Span<char> out)
{
	BOOL result;
	STACKFRAME64 stack;
	alignas(IMAGEHLP_SYMBOL64) char symbol_mem[sizeof(IMAGEHLP_SYMBOL64) + 256];
	IMAGEHLP_SYMBOL64* symbol = (IMAGEHLP_SYMBOL64*)symbol_mem;
	char name[256];
	catString(out, "Crash callstack:\n");
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

		// Get file and line information
		IMAGEHLP_LINE64 line_info = {};
		line_info.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
		DWORD line_displacement;
		if (SymGetLineFromAddr64(process, (DWORD64)stack.AddrPC.Offset, &line_displacement, &line_info))
		{
			catString(out, line_info.FileName);
			catString(out, "(");
			char line_num[16];
			toCString((u32)line_info.LineNumber, Span(line_num));
			catString(out, line_num);
			catString(out, "): ");
		}

		catString(out, name);
		catString(out, "\n");

	} while (result);
}


template <int SIZE>
static void describeException(PEXCEPTION_RECORD exception_record, StaticString<SIZE>& message) {
	const char* exception_name = "Unknown Exception";
	
	switch (exception_record->ExceptionCode) {
		case EXCEPTION_ACCESS_VIOLATION: exception_name = "Access Violation"; break;
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: exception_name = "Array Bounds Exceeded"; break;
		case EXCEPTION_BREAKPOINT: exception_name = "Breakpoint"; break;
		case EXCEPTION_DATATYPE_MISALIGNMENT: exception_name = "Data Type Misalignment"; break;
		case EXCEPTION_FLT_DENORMAL_OPERAND: exception_name = "Float Denormal Operand"; break;
		case EXCEPTION_FLT_DIVIDE_BY_ZERO: exception_name = "Float Divide by Zero"; break;
		case EXCEPTION_FLT_INEXACT_RESULT: exception_name = "Float Inexact Result"; break;
		case EXCEPTION_FLT_INVALID_OPERATION: exception_name = "Float Invalid Operation"; break;
		case EXCEPTION_FLT_OVERFLOW: exception_name = "Float Overflow"; break;
		case EXCEPTION_FLT_STACK_CHECK: exception_name = "Float Stack Check"; break;
		case EXCEPTION_FLT_UNDERFLOW: exception_name = "Float Underflow"; break;
		case EXCEPTION_ILLEGAL_INSTRUCTION: exception_name = "Illegal Instruction"; break;
		case EXCEPTION_IN_PAGE_ERROR: exception_name = "In Page Error"; break;
		case EXCEPTION_INT_DIVIDE_BY_ZERO: exception_name = "Integer Divide by Zero"; break;
		case EXCEPTION_INT_OVERFLOW: exception_name = "Integer Overflow"; break;
		case EXCEPTION_PRIV_INSTRUCTION: exception_name = "Privileged Instruction"; break;
		case EXCEPTION_SINGLE_STEP: exception_name = "Single Step"; break;
		case EXCEPTION_STACK_OVERFLOW: exception_name = "Stack Overflow"; break;
	}
	
	message.append(exception_name);
	char tmp[19];
	toCStringHex((u32)exception_record->ExceptionCode, Span(tmp));
	message.append(" (0x", tmp, ")");
	
	// For access violations, provide additional details
	if (exception_record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && 
		exception_record->NumberParameters >= 2) {
		message.append(" - ");
		if (exception_record->ExceptionInformation[0] == 0) {
			message.append("read at address ");
		} else if (exception_record->ExceptionInformation[0] == 1) {
			message.append("write at address ");
		} else {
			message.append("execute at address ");
		}
		char addr_buf[17];
		toCStringHex((u64)exception_record->ExceptionInformation[1], Span(addr_buf, 17));
		message.append("0x", addr_buf);
	}
}

static LONG WINAPI unhandledExceptionHandler(LPEXCEPTION_POINTERS info)
{
	if (!isFlagSet(g_crash_report_flags, CrashReportFlags::ENABLED)) return EXCEPTION_CONTINUE_SEARCH;

	HANDLE process = GetCurrentProcess();
	BOOL sym_init_success = SymInitialize(process, nullptr, TRUE);
	debug::StackTree::refreshModuleList();

	struct CrashInfo {
		LPEXCEPTION_POINTERS info;
		DWORD thread_id;
		StaticString<16386> message;
	};

	CrashInfo crash_info = { info, GetCurrentThreadId() };

	// add profiler stack to message for casses where we don't have PDBs
	// we can't do this on dumper thread, because it does not have access to other thread's data
	const char* open_blocks[16];
	u32 num_open_blocks = profiler::getOpenBlocks(Span(open_blocks));
	if (num_open_blocks > lengthOf(open_blocks)) num_open_blocks = lengthOf(open_blocks);
	crash_info.message.append("Profiler stack:\n");
	for (u32 i = 0; i < num_open_blocks; ++i) {
		crash_info.message.append(open_blocks[i], "\n");
	}
	crash_info.message.append("\n");
	
	if (!sym_init_success) {
		DWORD error = GetLastError();
		crash_info.message.append("Warning: Failed to initialize symbols (Error: ", (u32)error, ")\n");
	}
	
	auto dumper = [](void* data) -> DWORD {
		LPEXCEPTION_POINTERS info = ((CrashInfo*)data)->info;
		auto& message = ((CrashInfo*)data)->message;
		uintptr base = (uintptr)GetModuleHandle(NULL);

		if (info) {
			getStack(*info->ContextRecord, Span(message.data));
			message.append("\nException: ");
			describeException(info->ExceptionRecord, message);
			char addr_buf[17];
			toCStringHex((u64)info->ExceptionRecord->ExceptionAddress, Span(addr_buf));
			message.append("\nAddress: 0x", addr_buf);
			toCStringHex((u64)base, Span(addr_buf));
			message.append("\nBase: 0x", addr_buf, "\n");
			if (isFlagSet(g_crash_report_flags, CrashReportFlags::MESSAGE_BOX)) {
				os::messageBox(message);
			}
		}

		if (isFlagSet(g_crash_report_flags, CrashReportFlags::SENTRY)) {
			sentry::sendToSentry(message.data);
		}

		char minidump_path[MAX_PATH];
		GetCurrentDirectory(sizeof(minidump_path), minidump_path);
		catString(minidump_path, "\\minidump.dmp");

		HANDLE process = GetCurrentProcess();
		DWORD process_id = GetProcessId(process);
		HANDLE file = CreateFile(minidump_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		MINIDUMP_TYPE minidump_type = (MINIDUMP_TYPE)(
			MiniDumpWithFullMemoryInfo | MiniDumpFilterMemory | MiniDumpWithHandleData |
			MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);

		MINIDUMP_EXCEPTION_INFORMATION minidump_exception_info;
		minidump_exception_info.ThreadId = ((CrashInfo*)data)->thread_id;
		minidump_exception_info.ExceptionPointers = info;
		minidump_exception_info.ClientPointers = FALSE;

		BOOL dump_success = MiniDumpWriteDump(process,
			process_id,
			file,
			minidump_type,
			info ? &minidump_exception_info : nullptr,
			nullptr,
			nullptr);
		if (!dump_success) {
			DWORD error = GetLastError();
			message.append("Warning: Failed to write minidump (Error: ", (u32)error, ")\n");
		}
		CloseHandle(file);

		minidump_type = (MINIDUMP_TYPE)(MiniDumpWithFullMemory | MiniDumpWithFullMemoryInfo |
			MiniDumpFilterMemory | MiniDumpWithHandleData |
			MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
		file = CreateFile("fulldump.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		BOOL full_dump_success = MiniDumpWriteDump(process,
			process_id,
			file,
			minidump_type,
			info ? &minidump_exception_info : nullptr,
			nullptr,
			nullptr);
		if (!full_dump_success) {
			DWORD error = GetLastError();
			message.append("Warning: Failed to write full dump (Error: ", (u32)error, ")\n");
		}
		CloseHandle(file);
		return 0;
	};

	DWORD thread_id;
	HANDLE handle = CreateThread(0, 0x8000, dumper, &crash_info, 0, &thread_id);
	WaitForSingleObject(handle, INFINITE);

	if (isFlagSet(g_crash_report_flags, CrashReportFlags::LOG)) {
		logError(crash_info.message);
	}
	if (isFlagSet(g_crash_report_flags, CrashReportFlags::STDERR)) {
		fprintf(stderr, "%s", crash_info.message.data);
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

void configureCrashReport(CrashReportFlags flags) {
	g_crash_report_flags = flags;
}

void installUnhandledExceptionHandler() {
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
