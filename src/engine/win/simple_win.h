#pragma once


#define PASCAL __stdcall
#ifndef _W64 
	#define _W64 __w64
#endif
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define CF_TEXT 1
#define WSADESCRIPTION_LEN 256
#define WSASYS_STATUS_LEN 128
#define IPPROTO_TCP 6
#define AF_INET 2
#define PF_INET AF_INET
#define SOCK_STREAM 1
#define INVALID_SOCKET (SOCKET)(~0)
#define INADDR_ANY (u_long)0x00000000
#define SOCKET_ERROR (-1)
#define WSABASEERR 10000
#define WSAEWOULDBLOCK (WSABASEERR + 35)
#define DECLSPEC_IMPORT __declspec(dllimport)
#define WINBASEAPI DECLSPEC_IMPORT
#define WINUSERAPI DECLSPEC_IMPORT
#define WINAPI __stdcall
#define GENERIC_READ (0x80000000L)
#define GENERIC_WRITE (0x40000000L)
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define VOID void
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INFINITE 0xFFFFFFFF
#define STATUS_WAIT_0 ((DWORD)0x00000000L)
#define WAIT_OBJECT_0 ((STATUS_WAIT_0) + 0)
#define CREATE_SUSPENDED 0x00000004
#define EXCEPTION_EXECUTE_HANDLER 1
#define GetFileAttributes  GetFileAttributesA
#define CreateFile CreateFileA
#define CreateSemaphore CreateSemaphoreA
#define CreateMutex CreateMutexA
#define CreateEvent CreateEventA
#define DefWindowProc DefWindowProcA
#define GetModuleHandle GetModuleHandleA
#define LoadIcon LoadIconA
#define LoadCursor LoadCursorA
#define FindFirstFile FindFirstFileA
#define PeekMessage PeekMessageA
#define DispatchMessage DispatchMessageA
#define FindNextFile FindNextFileA
#define OutputDebugString OutputDebugStringA
#define CreateWindowA(lpClassName,  \
					  lpWindowName, \
					  dwStyle,      \
					  x,            \
					  y,            \
					  nWidth,       \
					  nHeight,      \
					  hWndParent,   \
					  hMenu,        \
					  hInstance,    \
					  lpParam)      \
	\
CreateWindowExA(0L,                 \
		lpClassName,                \
		lpWindowName,               \
		dwStyle,                    \
		x,                          \
		y,                          \
		nWidth,                     \
		nHeight,                    \
		hWndParent,                 \
		hMenu,                      \
		hInstance,                  \
		lpParam)

#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_DEVICE 0x00000040
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100
#define FILE_ATTRIBUTE_SPARSE_FILE 0x00000200
#define FILE_ATTRIBUTE_REPARSE_POINT 0x00000400
#define FILE_ATTRIBUTE_COMPRESSED 0x00000800
#define FILE_ATTRIBUTE_OFFLINE 0x00001000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED 0x00002000
#define FILE_ATTRIBUTE_ENCRYPTED 0x00004000
#define FILE_ATTRIBUTE_INTEGRITY_STREAM 0x00008000
#define FILE_ATTRIBUTE_VIRTUAL 0x00010000
#define FILE_ATTRIBUTE_NO_SCRUB_DATA 0x00020000
#define FILE_ATTRIBUTE_EA 0x00040000
#define FILE_NOTIFY_CHANGE_FILE_NAME 0x00000001
#define FILE_NOTIFY_CHANGE_DIR_NAME 0x00000002
#define FILE_NOTIFY_CHANGE_ATTRIBUTES 0x00000004
#define FILE_NOTIFY_CHANGE_SIZE 0x00000008
#define FILE_NOTIFY_CHANGE_LAST_WRITE 0x00000010
#define FILE_NOTIFY_CHANGE_LAST_ACCESS 0x00000020
#define FILE_NOTIFY_CHANGE_CREATION 0x00000040
#define FILE_NOTIFY_CHANGE_SECURITY 0x00000100
#define CALLBACK __stdcall
#define ERROR_OPERATION_ABORTED 995L
#define FILE_ACTION_ADDED 0x00000001
#define FILE_ACTION_REMOVED 0x00000002
#define FILE_ACTION_MODIFIED 0x00000003
#define FILE_ACTION_RENAMED_OLD_NAME 0x00000004
#define FILE_ACTION_RENAMED_NEW_NAME 0x00000005
#define MAX_PATH 260
#define FILE_LIST_DIRECTORY (0x0001)
#define FILE_SHARE_DELETE 0x00000004
#define FILE_FLAG_WRITE_THROUGH 0x80000000
#define FILE_FLAG_OVERLAPPED 0x40000000
#define FILE_FLAG_NO_BUFFERING 0x20000000
#define FILE_FLAG_RANDOM_ACCESS 0x10000000
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000
#define FILE_FLAG_POSIX_SEMANTICS 0x01000000
#define FILE_FLAG_SESSION_AWARE 0x00800000
#define FILE_FLAG_OPEN_REPARSE_POINT 0x00200000
#define FILE_FLAG_OPEN_NO_RECALL 0x00100000
#define FILE_FLAG_FIRST_PIPE_INSTANCE 0x00080000
#define LRESULT LONG_PTR
#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP 0x80000000L
#define WS_CHILD 0x40000000L
#define WS_MINIMIZE 0x20000000L
#define WS_VISIBLE 0x10000000L
#define WS_DISABLED 0x08000000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_MAXIMIZE 0x01000000L
#define WS_CAPTION 0x00C00000L /* WS_BORDER | WS_DLGFRAME  */
#define WS_BORDER 0x00800000L
#define WS_DLGFRAME 0x00400000L
#define WS_VSCROLL 0x00200000L
#define WS_HSCROLL 0x00100000L
#define WS_SYSMENU 0x00080000L
#define WS_THICKFRAME 0x00040000L
#define WS_GROUP 0x00020000L
#define WS_TABSTOP 0x00010000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define PM_REMOVE 0x0001
#define WM_QUIT 0x0012

#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCE MAKEINTRESOURCEA
#define IDI_APPLICATION MAKEINTRESOURCE(32512)
#define IDC_ARROW MAKEINTRESOURCE(32512)
#define WS_OVERLAPPEDWINDOW \
	(WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)

#ifndef FALSE
	#define FALSE 0
#endif

#ifndef TRUE
	#define TRUE 1
#endif


typedef int INT;
typedef unsigned char BYTE;
typedef wchar_t WCHAR;
typedef const VOID* LPCVOID;
typedef char CHAR;
typedef CHAR *NPSTR, *LPSTR, *PSTR;
typedef struct sockaddr* LPSOCKADDR;
typedef unsigned long u_long;
typedef unsigned long ULONG;
typedef ULONG* PULONG;
typedef unsigned short USHORT;
typedef USHORT* PUSHORT;
typedef unsigned char UCHAR;
typedef UCHAR* PUCHAR;
typedef unsigned short u_short;
typedef unsigned long DWORD;
typedef void* LPVOID;
typedef int BOOL;
typedef DWORD* LPDWORD;
typedef const CHAR *LPCSTR, *PCSTR;
typedef void* PVOID;
typedef long LONG;
typedef LONG* PLONG;
typedef long* LPLONG;
typedef __int64 LONGLONG;
typedef unsigned __int64 ULONGLONG;
typedef void* HANDLE;
typedef void* HWND;


#if defined(_WIN64)
	typedef __int64 INT_PTR, *PINT_PTR;
	typedef unsigned __int64 UINT_PTR, *PUINT_PTR;

	typedef __int64 LONG_PTR, *PLONG_PTR;
	typedef unsigned __int64 ULONG_PTR, *PULONG_PTR;

	#define __int3264 __int64

#else
	typedef _W64 int INT_PTR, *PINT_PTR;
	typedef _W64 unsigned int UINT_PTR, *PUINT_PTR;

	typedef _W64 long LONG_PTR, *PLONG_PTR;
	typedef _W64 unsigned long ULONG_PTR, *PULONG_PTR;

	#define __int3264 __int32
#endif


typedef ULONG_PTR SIZE_T, *PSIZE_T;
typedef ULONG_PTR DWORD_PTR, *PDWORD_PTR;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef unsigned int UINT;
typedef void* HINSTANCE;
typedef wchar_t WCHAR;
typedef WCHAR *NWPSTR, *LPWSTR, *PWSTR;


typedef DWORD(WINAPI* PTHREAD_START_ROUTINE)(LPVOID lpThreadParameter);
typedef PTHREAD_START_ROUTINE LPTHREAD_START_ROUTINE;


typedef struct in_addr
{
	union {
		struct
		{
			UCHAR s_b1, s_b2, s_b3, s_b4;
		} S_un_b;
		struct
		{
			USHORT s_w1, s_w2;
		} S_un_w;
		ULONG S_addr;
	} S_un;
#define s_addr S_un.S_addr
#define s_host S_un.S_un_b.s_b2
#define s_net S_un.S_un_b.s_b1
#define s_imp S_un.S_un_w.s_w2
#define s_impno S_un.S_un_b.s_b4
#define s_lh S_un.S_un_b.s_b3
} IN_ADDR, *PIN_ADDR, *LPIN_ADDR;


struct sockaddr_in
{
	short sin_family;
	u_short sin_port;
	struct in_addr sin_addr;
	char sin_zero[8];
};


typedef UINT_PTR SOCKET;
typedef unsigned short WORD;
typedef struct sockaddr_in SOCKADDR_IN;


typedef struct _SECURITY_ATTRIBUTES
{
	DWORD nLength;
	LPVOID lpSecurityDescriptor;
	BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;


typedef struct WSAData
{
	WORD wVersion;
	WORD wHighVersion;
#ifdef _WIN64
	unsigned short iMaxSockets;
	unsigned short iMaxUdpDg;
	char* lpVendorInfo;
	char szDescription[WSADESCRIPTION_LEN + 1];
	char szSystemStatus[WSASYS_STATUS_LEN + 1];
#else
	char szDescription[WSADESCRIPTION_LEN + 1];
	char szSystemStatus[WSASYS_STATUS_LEN + 1];
	unsigned short iMaxSockets;
	unsigned short iMaxUdpDg;
	char* lpVendorInfo;
#endif
} WSADATA;
typedef WSADATA* LPWSADATA;
typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef void* HICON;
typedef void* HCURSOR;
typedef void* HBRUSH;
typedef struct tagWNDCLASSEXA
{
	UINT cbSize;
	/* Win 3.x */
	UINT style;
	WNDPROC lpfnWndProc;
	int cbClsExtra;
	int cbWndExtra;
	HINSTANCE hInstance;
	HICON hIcon;
	HCURSOR hCursor;
	HBRUSH hbrBackground;
	LPCSTR lpszMenuName;
	LPCSTR lpszClassName;
	/* Win 4.0 */
	HICON hIconSm;
} WNDCLASSEXA, *PWNDCLASSEXA, *NPWNDCLASSEXA, *LPWNDCLASSEXA;
typedef WNDCLASSEXA WNDCLASSEX;
typedef PWNDCLASSEXA PWNDCLASSEX;


typedef struct _SYSTEM_INFO
{
	union {
		DWORD dwOemId;
		struct
		{
			WORD wProcessorArchitecture;
			WORD wReserved;
		} DUMMYSTRUCTNAME;
	} DUMMYUNIONNAME;
	DWORD dwPageSize;
	LPVOID lpMinimumApplicationAddress;
	LPVOID lpMaximumApplicationAddress;
	DWORD_PTR dwActiveProcessorMask;
	DWORD dwNumberOfProcessors;
	DWORD dwProcessorType;
	DWORD dwAllocationGranularity;
	WORD wProcessorLevel;
	WORD wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;


typedef struct _OVERLAPPED
{
	ULONG_PTR Internal;
	ULONG_PTR InternalHigh;
	union {
		struct
		{
			DWORD Offset;
			DWORD OffsetHigh;
		} DUMMYSTRUCTNAME;
		PVOID Pointer;
	} DUMMYUNIONNAME;

	HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;


typedef VOID(WINAPI* LPOVERLAPPED_COMPLETION_ROUTINE)(DWORD dwErrorCode,
	DWORD dwNumberOfBytesTransfered,
	LPOVERLAPPED lpOverlapped);


typedef struct _PROCESSOR_NUMBER
{
	WORD Group;
	BYTE Number;
	BYTE Reserved;
} PROCESSOR_NUMBER, *PPROCESSOR_NUMBER;


typedef struct _FILE_NOTIFY_INFORMATION
{
	DWORD NextEntryOffset;
	DWORD Action;
	DWORD FileNameLength;
	WCHAR FileName[1];
} FILE_NOTIFY_INFORMATION, *PFILE_NOTIFY_INFORMATION;


#if defined(MIDL_PASS)
typedef struct _LARGE_INTEGER
{
#else // MIDL_PASS
typedef union _LARGE_INTEGER {
	struct
	{
		DWORD LowPart;
		LONG HighPart;
	} DUMMYSTRUCTNAME;
	struct
	{
		DWORD LowPart;
		LONG HighPart;
	} u;
#endif // MIDL_PASS
	LONGLONG QuadPart;
} LARGE_INTEGER;


typedef struct tagPOINT
{
	LONG x;
	LONG y;
} POINT, *PPOINT, *NPPOINT, *LPPOINT;


typedef struct tagMSG
{
	HWND hwnd;
	UINT message;
	WPARAM wParam;
	LPARAM lParam;
	DWORD time;
	POINT pt;
#ifdef _MAC
	DWORD lPrivate;
#endif
} MSG, *PMSG, *NPMSG, *LPMSG;
typedef HINSTANCE HMODULE;


typedef struct _FILETIME
{
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;


typedef struct _WIN32_FIND_DATAA
{
	DWORD dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD nFileSizeHigh;
	DWORD nFileSizeLow;
	DWORD dwReserved0;
	DWORD dwReserved1;
	CHAR cFileName[260];
	CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA;
typedef struct _WIN32_FIND_DATAA* LPWIN32_FIND_DATAA;


extern "C" {


int PASCAL closesocket(SOCKET s);
int PASCAL WSAStartup(WORD wVersionRequired, LPWSADATA lpWSAData);
SOCKET PASCAL socket(int af, int type, int protocol);
unsigned long PASCAL inet_addr(const char* cp);
u_short PASCAL htons(u_short hostshort);
int PASCAL bind(SOCKET s, const struct sockaddr* addr, int namelen);
int PASCAL listen(SOCKET s, int backlog);
int PASCAL WSAGetLastError();
SOCKET PASCAL accept(SOCKET s, struct sockaddr* addr, int* addrlen);
int PASCAL bind(SOCKET s, const struct sockaddr* addr, int namelen);
int PASCAL connect(SOCKET s, const struct sockaddr* name, int namelen);
int PASCAL closesocket(SOCKET s);
int PASCAL recv(SOCKET s, char* buf, int len, int flags);
int PASCAL send(SOCKET s, const char* buf, int len, int flags);
WINBASEAPI HANDLE WINAPI CreateFileA(LPCSTR lpFileName,
	DWORD dwDesiredAccess,
	DWORD dwShareMode,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes,
	DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes,
	HANDLE hTemplateFile);
WINBASEAPI BOOL WINAPI FlushFileBuffers(HANDLE hFile);
WINBASEAPI BOOL WINAPI CloseHandle(HANDLE hObject);
WINBASEAPI
BOOL WINAPI WriteFile(HANDLE hFile,
	LPCVOID lpBuffer,
	DWORD nNumberOfBytesToWrite,
	LPDWORD lpNumberOfBytesWritten,
	LPOVERLAPPED lpOverlapped);
WINBASEAPI BOOL WINAPI ReadFile(HANDLE hFile,
	LPVOID lpBuffer,
	DWORD nNumberOfBytesToRead,
	LPDWORD lpNumberOfBytesRead,
	LPOVERLAPPED lpOverlapped);
WINBASEAPI DWORD WINAPI GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
WINBASEAPI DWORD WINAPI SetFilePointer(HANDLE hFile,
	LONG lDistanceToMove,
	PLONG lpDistanceToMoveHigh,
	DWORD dwMoveMethod);
WINBASEAPI BOOL WINAPI SetEndOfFile(HANDLE hFile);
WINBASEAPI HANDLE WINAPI CreateSemaphoreA(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes,
	LONG lInitialCount,
	LONG lMaximumCount,
	LPCSTR lpName);
WINBASEAPI BOOL WINAPI ReleaseSemaphore(HANDLE hSemaphore,
	LONG lReleaseCount,
	LPLONG lpPreviousCount);
WINBASEAPI DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
WINBASEAPI HANDLE WINAPI CreateMutex(LPSECURITY_ATTRIBUTES lpMutexAttributes,
	BOOL bInitialOwner,
	LPCSTR lpName);
WINBASEAPI BOOL WINAPI ReleaseMutex(HANDLE hMutex);
WINBASEAPI HANDLE WINAPI CreateEvent(LPSECURITY_ATTRIBUTES lpEventAttributes,
	BOOL bManualReset,
	BOOL bInitialState,
	LPCSTR lpName);
WINBASEAPI BOOL WINAPI SetEvent(HANDLE hEvent);
WINBASEAPI BOOL WINAPI ResetEvent(HANDLE hEvent);
WINBASEAPI int WINAPI GetThreadPriority(HANDLE hThread);
WINBASEAPI HANDLE WINAPI GetCurrentThread();
WINBASEAPI HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes,
	SIZE_T dwStackSize,
	LPTHREAD_START_ROUTINE lpStartAddress,
	LPVOID lpParameter,
	DWORD dwCreationFlags,
	LPDWORD lpThreadId);
WINBASEAPI DWORD WINAPI ResumeThread(HANDLE hThread);
WINBASEAPI DWORD WINAPI SetThreadIdealProcessor(HANDLE hThread, DWORD dwIdealProcessor);
WINBASEAPI DWORD_PTR WINAPI SetThreadAffinityMask(HANDLE hThread, DWORD_PTR dwThreadAffinityMask);
WINBASEAPI BOOL WINAPI SetThreadPriority(HANDLE hThread, int nPriority);
WINBASEAPI BOOL WINAPI GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode);
WINBASEAPI VOID WINAPI ExitThread(DWORD dwExitCode);
WINBASEAPI VOID WINAPI Sleep(DWORD dwMilliseconds);
WINBASEAPI VOID WINAPI GetSystemInfo(LPSYSTEM_INFO lpSystemInfo);
WINBASEAPI DWORD WINAPI GetCurrentThreadId();
WINBASEAPI BOOL WINAPI GetThreadIdealProcessorEx(HANDLE hThread,
	PPROCESSOR_NUMBER lpIdealProcessor);
WINBASEAPI VOID WINAPI RaiseException(DWORD dwExceptionCode,
	DWORD dwExceptionFlags,
	DWORD nNumberOfArguments,
	const ULONG_PTR* lpArguments);

WINBASEAPI BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount);
WINBASEAPI BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency);
WINBASEAPI BOOL WINAPI CancelIoEx(HANDLE hFile, LPOVERLAPPED lpOverlapped);
WINBASEAPI BOOL WINAPI ReadDirectoryChangesW(HANDLE hDirectory,
	LPVOID lpBuffer,
	DWORD nBufferLength,
	BOOL bWatchSubtree,
	DWORD dwNotifyFilter,
	LPDWORD lpBytesReturned,
	LPOVERLAPPED lpOverlapped,
	LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);
WINBASEAPI DWORD WINAPI SleepEx(DWORD dwMilliseconds, BOOL bAlertable);
WINUSERAPI LRESULT WINAPI DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
WINBASEAPI HMODULE WINAPI GetModuleHandleA(LPCSTR lpModuleName);
WINUSERAPI HICON WINAPI LoadIconA(HINSTANCE hInstance, LPCSTR lpIconName);
WINUSERAPI HCURSOR WINAPI LoadCursorA(HINSTANCE hInstance, LPCSTR lpCursorName);
WINBASEAPI DWORD WINAPI GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer);

__inline DWORD GetCurrentDirectory(DWORD nBufferLength, LPSTR lpBuffer)
{
	return GetCurrentDirectoryA(nBufferLength, lpBuffer);
}
WINBASEAPI HANDLE WINAPI FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData);
WINUSERAPI BOOL WINAPI
PeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
WINUSERAPI LRESULT WINAPI DispatchMessageA(const MSG* lpMsg);
WINUSERAPI BOOL WINAPI TranslateMessage(const MSG* lpMsg);

WINBASEAPI BOOL WINAPI FindClose(HANDLE hFindFile);
WINBASEAPI BOOL WINAPI FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData);
WINBASEAPI VOID WINAPI OutputDebugStringA(LPCSTR lpOutputString);
WINBASEAPI DWORD WINAPI GetFileAttributesA(LPCSTR lpFileName);
WINUSERAPI BOOL WINAPI OpenClipboard(HWND hWndNewOwner);
WINUSERAPI HANDLE WINAPI SetClipboardData(UINT uFormat, HANDLE hMem);

}
