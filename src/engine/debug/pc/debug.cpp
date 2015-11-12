#include "debug/debug.h"
#include "core/string.h"
#include "core/string.h"
#include "core/system.h"
#include "debug/stack_tree.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>
#include <mapi.h>
#include <cstdlib>


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


} // namespace Debug


BOOL SendFile(LPCSTR lpszSubject,
			  LPCSTR lpszTo,
			  LPCSTR lpszName,
			  LPCSTR lpszText,
			  LPCSTR lpszFullFileName)
{
	HINSTANCE hMAPI = ::LoadLibrary("mapi32.dll");
	if (!hMAPI)
		return FALSE;
	LPMAPISENDMAIL lpfnMAPISendMail =
		(LPMAPISENDMAIL)::GetProcAddress(hMAPI, "MAPISendMail");

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

	char message[4096];
	getStack(*info->ContextRecord, message, sizeof(message));
	messageBox(message);

	char minidump_path[Lumix::MAX_PATH_LENGTH];
	GetCurrentDirectory(sizeof(minidump_path), minidump_path);
	Lumix::catString(minidump_path, "\\minidump.dmp");

	HANDLE process = GetCurrentProcess();
	DWORD process_id = GetProcessId(process);
	HANDLE file = CreateFile(minidump_path,
							 GENERIC_WRITE,
							 0,
							 nullptr,
							 CREATE_ALWAYS,
							 FILE_ATTRIBUTE_NORMAL,
							 nullptr);
	MINIDUMP_TYPE minidump_type = (MINIDUMP_TYPE)(
		/*MiniDumpWithFullMemory | MiniDumpWithFullMemoryInfo |*/ MiniDumpFilterMemory |
		MiniDumpWithHandleData | MiniDumpWithThreadInfo |
		MiniDumpWithUnloadedModules);

	MINIDUMP_EXCEPTION_INFORMATION minidump_exception_info;
	minidump_exception_info.ThreadId = GetCurrentThreadId();
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
			 "Lumix Studio crashed, minidump attached",
			 minidump_path);

	minidump_type =
		(MINIDUMP_TYPE)(MiniDumpWithFullMemory | MiniDumpWithFullMemoryInfo |
						MiniDumpFilterMemory | MiniDumpWithHandleData |
						MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
	file = CreateFile("fulldump.dmp",
					  GENERIC_WRITE,
					  0,
					  nullptr,
					  CREATE_ALWAYS,
					  FILE_ATTRIBUTE_NORMAL,
					  nullptr);
	MiniDumpWriteDump(process,
					  process_id,
					  file,
					  minidump_type,
					  info ? &minidump_exception_info : nullptr,
					  nullptr,
					  nullptr);
	CloseHandle(file);

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