// Minimal Win32 surface for building the Script Extender on macOS.
//
// The extender's sources include <windows.h> and use a small, fixed subset of Win32 types and
// calls. Rather than sprinkle #ifdefs over the upstream code, the macOS build puts this directory
// first on the include path so those includes resolve here. Everything is implemented on top of
// POSIX / Darwin APIs (see macOS/Platform/Win32Compat.cpp).
#pragma once

#if !defined(__APPLE__)
#error "macOS/Compat is only meant to be on the include path of the macOS build"
#endif

#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <cerrno>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <pthread.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <os/lock.h>

#include "sal.h"

// ---------------------------------------------------------------------------------------------
// Compiler keywords
// ---------------------------------------------------------------------------------------------

#define __forceinline inline __attribute__((always_inline))
#define __fastcall
#define __stdcall
#define __cdecl
#define __vectorcall
#define WINAPI
#define APIENTRY
#define CALLBACK
#define NTAPI
#define __assume(x) __builtin_assume(x)
#define _ReturnAddress() __builtin_return_address(0)
#define __debugbreak() __builtin_debugtrap()
#define WINBASEAPI
#define WINUSERAPI

#if defined(__aarch64__)
#define _mm_pause() __builtin_arm_yield()
#else
#include <immintrin.h>
#endif

// ---------------------------------------------------------------------------------------------
// Basic types
// ---------------------------------------------------------------------------------------------

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned char BOOLEAN;
typedef unsigned short WORD;
typedef unsigned short USHORT;
typedef unsigned int DWORD;       // 32-bit on Windows; `unsigned long` would be 64-bit here
typedef unsigned int UINT;
typedef unsigned int ULONG;
typedef int LONG;
typedef int INT;
typedef short SHORT;
typedef char CHAR;
typedef unsigned char UCHAR;
typedef wchar_t WCHAR;
typedef float FLOAT;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef long long LONG64;
typedef unsigned long long ULONG64;
typedef unsigned long long DWORD64;
typedef std::uintptr_t UINT_PTR;
typedef std::uintptr_t ULONG_PTR;
typedef std::uintptr_t DWORD_PTR;
typedef std::intptr_t INT_PTR;
typedef std::intptr_t LONG_PTR;
typedef std::size_t SIZE_T;
typedef std::intptr_t SSIZE_T;
typedef std::uintptr_t WPARAM;
typedef std::intptr_t LPARAM;
typedef std::intptr_t LRESULT;
typedef int HRESULT;
typedef void VOID;
typedef void* PVOID;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef BYTE* PBYTE;
typedef BYTE* LPBYTE;
typedef DWORD* PDWORD;
typedef DWORD* LPDWORD;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef const char* PCSTR;
typedef wchar_t* LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef const wchar_t* PCWSTR;
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* HINSTANCE;
typedef void* HWND;
typedef void* HRSRC;
typedef void* HGLOBAL;
typedef void* HKEY;
typedef void* LPSECURITY_ATTRIBUTES;
typedef std::intptr_t (*FARPROC)();
typedef std::intptr_t (*PROC)();

#define __int64 long long
#define __int32 int
#define __int16 short
#define __int8 char

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define MAX_PATH 1024
#define INVALID_HANDLE_VALUE ((HANDLE)(std::intptr_t)-1)
#define NO_ERROR 0
#define ERROR_ALREADY_EXISTS 183
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_ACCESS_DENIED 5
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x1
#define FILE_SHARE_WRITE 0x2
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_NORMAL 0x80
#define ERROR_SUCCESS 0
#define S_OK ((HRESULT)0)
#define E_FAIL ((HRESULT)0x80004005)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define INFINITE 0xFFFFFFFFu
#define CP_UTF8 65001

#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define LOBYTE(w) ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#define HIBYTE(w) ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xff))
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#define UNREFERENCED_PARAMETER(x) (void)(x)

typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} GUID, UUID;

int UuidCreate(UUID* uuid);

struct EXCEPTION_RECORD {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    EXCEPTION_RECORD* ExceptionRecord;
    PVOID ExceptionAddress;
    DWORD NumberParameters;
    ULONG_PTR ExceptionInformation[15];
};
struct CONTEXT;
struct EXCEPTION_POINTERS {
    EXCEPTION_RECORD* ExceptionRecord;
    CONTEXT* ContextRecord;
};
typedef EXCEPTION_POINTERS* PEXCEPTION_POINTERS;
typedef EXCEPTION_POINTERS* LPEXCEPTION_POINTERS;
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION (-1)

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

struct WIN32_FIND_DATAW {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
    DWORD dwReserved0, dwReserved1;
    wchar_t cFileName[MAX_PATH];
    wchar_t cAlternateFileName[14];
};
typedef WIN32_FIND_DATAW* LPWIN32_FIND_DATAW;
#define FILE_ATTRIBUTE_DIRECTORY 0x10

typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; };
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME;

// ---------------------------------------------------------------------------------------------
// Locks. NOTE: these are used by the extender for its *own* synchronization. Game structures that
// embed a lock declare the platform's real layout separately (see GameDefinitions/Base/Lock.h).
// ---------------------------------------------------------------------------------------------

struct CRITICAL_SECTION {
    pthread_mutex_t mutex;
};
typedef CRITICAL_SECTION* LPCRITICAL_SECTION;

void InitializeCriticalSection(CRITICAL_SECTION* cs);
void DeleteCriticalSection(CRITICAL_SECTION* cs);
inline void EnterCriticalSection(CRITICAL_SECTION* cs) { pthread_mutex_lock(&cs->mutex); }
inline void LeaveCriticalSection(CRITICAL_SECTION* cs) { pthread_mutex_unlock(&cs->mutex); }
inline BOOL TryEnterCriticalSection(CRITICAL_SECTION* cs) { return pthread_mutex_trylock(&cs->mutex) == 0; }

struct SRWLOCK {
    void* Ptr;
};
typedef SRWLOCK* PSRWLOCK;
#define SRWLOCK_INIT { nullptr }
inline void InitializeSRWLock(SRWLOCK* lock) { lock->Ptr = nullptr; }
void AcquireSRWLockExclusive(SRWLOCK* lock);
BOOLEAN TryAcquireSRWLockExclusive(SRWLOCK* lock);
BOOLEAN TryAcquireSRWLockShared(SRWLOCK* lock);
void ReleaseSRWLockExclusive(SRWLOCK* lock);
void AcquireSRWLockShared(SRWLOCK* lock);
void ReleaseSRWLockShared(SRWLOCK* lock);

// ---------------------------------------------------------------------------------------------
// Interlocked / intrinsics
// ---------------------------------------------------------------------------------------------

inline LONG InterlockedIncrement(LONG volatile* v) { return __sync_add_and_fetch(v, 1); }
inline LONG InterlockedDecrement(LONG volatile* v) { return __sync_sub_and_fetch(v, 1); }
inline LONG InterlockedExchange(LONG volatile* v, LONG n) { return __sync_lock_test_and_set(v, n); }
inline LONG InterlockedCompareExchange(LONG volatile* v, LONG n, LONG cmp) { return __sync_val_compare_and_swap(v, cmp, n); }
inline LONG64 InterlockedIncrement64(LONG64 volatile* v) { return __sync_add_and_fetch(v, 1); }
inline LONG64 InterlockedDecrement64(LONG64 volatile* v) { return __sync_sub_and_fetch(v, 1); }
inline LONG64 InterlockedExchangeAdd64(LONG64 volatile* v, LONG64 n) { return __sync_fetch_and_add(v, n); }
inline LONG InterlockedExchangeAdd(LONG volatile* v, LONG n) { return __sync_fetch_and_add(v, n); }
inline LONG64 InterlockedOr64(LONG64 volatile* v, LONG64 mask) { return __sync_fetch_and_or(v, mask); }
inline LONG64 InterlockedAnd64(LONG64 volatile* v, LONG64 mask) { return __sync_fetch_and_and(v, mask); }
inline LONG InterlockedOr(LONG volatile* v, LONG mask) { return __sync_fetch_and_or(v, mask); }
inline LONG InterlockedAnd(LONG volatile* v, LONG mask) { return __sync_fetch_and_and(v, mask); }

// _BitScanForward/_BitScanReverse(64) are compiler builtins under -fms-extensions.

#define _mm_popcnt_u64(x) ((long long)__builtin_popcountll(x))
#define _mm_popcnt_u32(x) ((int)__builtin_popcount(x))
#define __popcnt(x) ((unsigned int)__builtin_popcount(x))
#define __popcnt64(x) ((unsigned long long)__builtin_popcountll(x))
#define _byteswap_ulong(x) __builtin_bswap32(x)
#define _byteswap_uint64(x) __builtin_bswap64(x)
#define _byteswap_ushort(x) __builtin_bswap16(x)

// ---------------------------------------------------------------------------------------------
// CRT "secure" variants
// ---------------------------------------------------------------------------------------------

#define _TRUNCATE ((std::size_t)-1)
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _wcsicmp wcscasecmp
#define _wcsnicmp wcsncasecmp
#define _strdup strdup
#define _alloca alloca
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define _itoa_s(value, buf, radix) snprintf(buf, sizeof(buf), (radix) == 16 ? "%x" : "%d", value)

inline int _vsnprintf_s(char* buf, std::size_t size, std::size_t /*maxCount*/, char const* fmt, va_list args)
{
    int n = vsnprintf(buf, size, fmt, args);
    return (n < 0 || (std::size_t)n >= size) ? -1 : n;
}

template <std::size_t N>
inline int _vsnprintf_s(char (&buf)[N], std::size_t maxCount, char const* fmt, va_list args)
{
    return _vsnprintf_s(buf, N, maxCount, fmt, args);
}

inline int vsprintf_s(char* buf, std::size_t size, char const* fmt, va_list args)
{
    return vsnprintf(buf, size, fmt, args);
}

template <std::size_t N>
inline int vsprintf_s(char (&buf)[N], char const* fmt, va_list args)
{
    return vsnprintf(buf, N, fmt, args);
}

template <std::size_t N>
__attribute__((format(printf, 2, 3)))
inline int sprintf_s(char (&buf)[N], char const* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, N, fmt, args);
    va_end(args);
    return n;
}

__attribute__((format(printf, 3, 4)))
inline int sprintf_s(char* buf, std::size_t size, char const* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return n;
}

__attribute__((format(printf, 4, 5)))
inline int _snprintf_s(char* buf, std::size_t size, std::size_t /*maxCount*/, char const* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return (n < 0 || (std::size_t)n >= size) ? -1 : n;
}

template <std::size_t N>
__attribute__((format(printf, 3, 4)))
inline int _snprintf_s(char (&buf)[N], std::size_t /*maxCount*/, char const* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, N, fmt, args);
    va_end(args);
    return (n < 0 || (std::size_t)n >= N) ? -1 : n;
}

inline int strcpy_s(char* dst, std::size_t size, char const* src)
{
    if (size == 0) return ERANGE;
    strlcpy(dst, src, size);
    return 0;
}

template <std::size_t N>
inline int strcpy_s(char (&dst)[N], char const* src) { return strcpy_s(dst, N, src); }

inline int strncpy_s(char* dst, std::size_t size, char const* src, std::size_t count)
{
    if (size == 0) return ERANGE;
    std::size_t n = strnlen(src, count);
    if (n >= size) n = size - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
    return 0;
}

template <std::size_t N>
inline int strncpy_s(char (&dst)[N], char const* src, std::size_t count) { return strncpy_s(dst, N, src, count); }

inline int strcat_s(char* dst, std::size_t size, char const* src)
{
    strlcat(dst, src, size);
    return 0;
}

template <std::size_t N>
inline int strcat_s(char (&dst)[N], char const* src) { return strcat_s(dst, N, src); }

template <std::size_t N>
inline int wcscpy_s(wchar_t (&dst)[N], wchar_t const* src)
{
    wcsncpy(dst, src, N - 1);
    dst[N - 1] = 0;
    return 0;
}

inline int wcscpy_s(wchar_t* dst, std::size_t size, wchar_t const* src)
{
    if (size == 0) return ERANGE;
    wcsncpy(dst, src, size - 1);
    dst[size - 1] = 0;
    return 0;
}

inline int memcpy_s(void* dst, std::size_t dstSize, void const* src, std::size_t count)
{
    if (count > dstSize) return ERANGE;
    memcpy(dst, src, count);
    return 0;
}

inline int fopen_s(FILE** f, char const* path, char const* mode)
{
    *f = fopen(path, mode);
    return *f ? 0 : errno;
}

inline int localtime_s(struct tm* out, const time_t* t) { return localtime_r(t, out) ? 0 : errno; }
inline int gmtime_s(struct tm* out, const time_t* t) { return gmtime_r(t, out) ? 0 : errno; }

inline void* _aligned_malloc(std::size_t size, std::size_t alignment)
{
    void* p = nullptr;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    return posix_memalign(&p, alignment, size) == 0 ? p : nullptr;
}
inline void _aligned_free(void* p) { free(p); }

// ---------------------------------------------------------------------------------------------
// Processes, threads, modules, memory
// ---------------------------------------------------------------------------------------------

DWORD GetCurrentThreadId();
DWORD GetCurrentProcessId();
HANDLE GetCurrentProcess();
HANDLE GetCurrentThread();
ULONGLONG GetTickCount64();
DWORD GetTickCount();
void Sleep(DWORD ms);
DWORD GetLastError();
BOOL IsDebuggerPresent();
void DebugBreak();
[[noreturn]] void ExitProcess(UINT code);
[[noreturn]] BOOL TerminateProcess(HANDLE process, UINT code);
BOOL CloseHandle(HANDLE h);
void OutputDebugStringA(char const* s);
void OutputDebugStringW(wchar_t const* s);
BOOL QueryPerformanceCounter(LARGE_INTEGER* v);
BOOL QueryPerformanceFrequency(LARGE_INTEGER* v);
BOOL DisableThreadLibraryCalls(HMODULE module);

// Modules are Mach-O images; an HMODULE is the image's mach_header address.
// "bg3.exe" / NULL resolve to the main executable, "osiris.dll" to libOsiris.dylib.
HMODULE GetModuleHandleA(char const* name);
HMODULE GetModuleHandleW(wchar_t const* name);
HMODULE LoadLibraryA(char const* name);
HMODULE LoadLibraryW(wchar_t const* name);
BOOL FreeLibrary(HMODULE module);
inline BOOL FreeModule(HMODULE module) { return FreeLibrary(module); }
FARPROC GetProcAddress(HMODULE module, char const* name);
DWORD GetModuleFileNameA(HMODULE module, char* path, DWORD size);
DWORD GetModuleFileNameW(HMODULE module, wchar_t* path, DWORD size);
struct MODULEINFO {
    LPVOID lpBaseOfDll;
    DWORD SizeOfImage;
    LPVOID EntryPoint;
};
// lpBaseOfDll is the image's mach_header; SizeOfImage spans its segments up to (excluding) __LINKEDIT.
BOOL GetModuleInformation(HANDLE process, HMODULE module, MODULEINFO* info, DWORD size);
wchar_t const* GetCommandLineW();

#define GetModuleHandle GetModuleHandleW
#define LoadLibrary LoadLibraryW
#define GetModuleFileName GetModuleFileNameW

#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_GUARD 0x100
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000
#define MEM_FREE 0x10000

typedef struct _MEMORY_BASIC_INFORMATION {
    PVOID BaseAddress;
    PVOID AllocationBase;
    DWORD AllocationProtect;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MEMORY_BASIC_INFORMATION;

BOOL VirtualProtect(LPVOID address, SIZE_T size, DWORD newProtect, DWORD* oldProtect);
SIZE_T VirtualQuery(LPCVOID address, MEMORY_BASIC_INFORMATION* info, SIZE_T length);
BOOL IsBadReadPtr(const void* ptr, UINT_PTR size);
BOOL IsBadWritePtr(void* ptr, UINT_PTR size);
BOOL FlushInstructionCache(HANDLE process, LPCVOID address, SIZE_T size);

// ---------------------------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------------------------

// NOTE: wchar_t is UTF-32 on macOS. These convert between UTF-8 and the platform wchar_t.
int WideCharToMultiByte(UINT codePage, DWORD flags, wchar_t const* wstr, int wlen, char* str, int len, char const* defaultChar, BOOL* usedDefault);
int MultiByteToWideChar(UINT codePage, DWORD flags, char const* str, int len, wchar_t* wstr, int wlen);

// ---------------------------------------------------------------------------------------------
// Files and directories (paths are wide strings holding POSIX paths; both separators accepted)
// ---------------------------------------------------------------------------------------------

BOOL CreateDirectoryW(wchar_t const* path, LPSECURITY_ATTRIBUTES attrs);
BOOL CreateDirectoryA(char const* path, LPSECURITY_ATTRIBUTES attrs);
BOOL PathFileExistsW(wchar_t const* path);
BOOL PathFileExistsA(char const* path);
BOOL DeleteFileW(wchar_t const* path);
BOOL CopyFileW(wchar_t const* from, wchar_t const* to, BOOL failIfExists);
BOOL RemoveDirectoryW(wchar_t const* path);
BOOL PathIsDirectoryW(wchar_t const* path);
#define CreateDirectory CreateDirectoryW
#define PathFileExists PathFileExistsW

// ---------------------------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------------------------

#define FOREGROUND_BLUE 0x0001
#define FOREGROUND_GREEN 0x0002
#define FOREGROUND_RED 0x0004
#define FOREGROUND_INTENSITY 0x0008
#define STD_INPUT_HANDLE ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)

HANDLE GetStdHandle(DWORD which);
BOOL SetConsoleTextAttribute(HANDLE console, WORD attributes);
BOOL AllocConsole();
BOOL FreeConsole();

// ---------------------------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------------------------

#define MB_OK 0x0
#define MB_ICONERROR 0x10
#define MB_ICONWARNING 0x30
#define MB_ICONINFORMATION 0x40
#define MB_TASKMODAL 0x2000
#define MB_SETFOREGROUND 0x10000
#define VK_SHIFT 0x10

int MessageBoxA(HWND owner, char const* text, char const* caption, UINT type);
int MessageBoxW(HWND owner, wchar_t const* text, wchar_t const* caption, UINT type);
#define MessageBox MessageBoxW
SHORT GetAsyncKeyState(int key);
