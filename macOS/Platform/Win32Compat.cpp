// Implementations of the Win32 calls declared in macOS/Compat/windows.h.
#include <windows.h>
#include "Darwin.h"

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/mach_vm.h>
#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <dlfcn.h>
#include <copyfile.h>
#include <uuid/uuid.h>
#include <codecvt>
#include <filesystem>
#include <locale>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace {

std::string ToNarrowPath(wchar_t const* path)
{
    std::wstring wide(path ? path : L"");
    for (auto& ch : wide) {
        if (ch == L'\\') ch = L'/';
    }
    return std::filesystem::path(wide).string();
}

thread_local DWORD gLastError{ 0 };

}

// ---------------------------------------------------------------------------------------------
// Locks
// ---------------------------------------------------------------------------------------------

namespace {

using CriticalSectionProc = void (CRITICAL_SECTION*);

struct GameCriticalSectionApi
{
    CriticalSectionProc* Lock{ nullptr };
    CriticalSectionProc* Unlock{ nullptr };
};

GameCriticalSectionApi const& GetGameCriticalSectionApi()
{
    static GameCriticalSectionApi api = [] {
        GameCriticalSectionApi result;
        result.Lock = reinterpret_cast<CriticalSectionProc*>(bg3se::darwin::FindSymbol(nullptr, "_ZN2ls15CriticalSection4LockEv"));
        result.Unlock = reinterpret_cast<CriticalSectionProc*>(bg3se::darwin::FindSymbol(nullptr, "_ZN2ls15CriticalSection6UnlockEv"));
        if (!result.Lock || !result.Unlock) {
            result.Lock = nullptr;
            result.Unlock = nullptr;
        }
        return result;
    }();
    return api;
}

}

void InitializeCriticalSection(CRITICAL_SECTION* cs)
{
    // Same initial state as the engine's constructor
    cs->Semaphore = 0;
    cs->LockCount = -1;
    cs->RecursionCount = 0;
    cs->OwnerThread = 0;
    cs->SpinCount = 4000;
}

void DeleteCriticalSection(CRITICAL_SECTION* cs)
{
    if (cs->Semaphore != 0) {
        semaphore_destroy(mach_task_self(), cs->Semaphore);
        cs->Semaphore = 0;
    }
}

void EnterCriticalSection(CRITICAL_SECTION* cs)
{
    auto const& api = GetGameCriticalSectionApi();
    if (api.Lock) {
        api.Lock(cs);
        return;
    }

    // Outside the game nothing else touches the lock, so a plain recursive spin lock that keeps
    // the same field meanings is enough.
    auto self = GetCurrentThreadId();
    for (;;) {
        int32_t expected = -1;
        if (__atomic_compare_exchange_n(&cs->LockCount, &expected, 0, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            cs->OwnerThread = self;
            cs->RecursionCount = 1;
            return;
        }
        if (cs->OwnerThread == self) {
            __atomic_add_fetch(&cs->LockCount, 1, __ATOMIC_SEQ_CST);
            cs->RecursionCount++;
            return;
        }
        sched_yield();
    }
}

void LeaveCriticalSection(CRITICAL_SECTION* cs)
{
    auto const& api = GetGameCriticalSectionApi();
    if (api.Unlock) {
        api.Unlock(cs);
        return;
    }

    if (--cs->RecursionCount == 0) {
        cs->OwnerThread = 0;
    }
    __atomic_sub_fetch(&cs->LockCount, 1, __ATOMIC_SEQ_CST);
}

BOOL TryEnterCriticalSection(CRITICAL_SECTION* cs)
{
    auto self = GetCurrentThreadId();
    int32_t expected = -1;
    if (__atomic_compare_exchange_n(&cs->LockCount, &expected, 0, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        cs->OwnerThread = self;
        cs->RecursionCount = 1;
        return TRUE;
    }
    if (cs->OwnerThread == self) {
        __atomic_add_fetch(&cs->LockCount, 1, __ATOMIC_SEQ_CST);
        cs->RecursionCount++;
        return TRUE;
    }
    return FALSE;
}

// SRWLOCK is pointer-sized and zero-initialized, so the real lock lives in a side table keyed by
// the SRWLOCK's address. Only the extender's own locks go through here.
namespace {
std::shared_mutex& GetSrw(SRWLOCK* lock)
{
    // Function-local statics: usable before this file's dynamic initializers have run
    static std::mutex tableMutex;
    static auto table = new std::unordered_map<SRWLOCK*, std::unique_ptr<std::shared_mutex>>();

    std::lock_guard guard(tableMutex);
    auto& entry = (*table)[lock];
    if (!entry) entry = std::make_unique<std::shared_mutex>();
    return *entry;
}
}

void AcquireSRWLockExclusive(SRWLOCK* lock) { GetSrw(lock).lock(); }
void ReleaseSRWLockExclusive(SRWLOCK* lock) { GetSrw(lock).unlock(); }
void AcquireSRWLockShared(SRWLOCK* lock) { GetSrw(lock).lock_shared(); }
void ReleaseSRWLockShared(SRWLOCK* lock) { GetSrw(lock).unlock_shared(); }
BOOLEAN TryAcquireSRWLockExclusive(SRWLOCK* lock) { return GetSrw(lock).try_lock(); }
BOOLEAN TryAcquireSRWLockShared(SRWLOCK* lock) { return GetSrw(lock).try_lock_shared(); }

// ---------------------------------------------------------------------------------------------
// Processes, threads, time
// ---------------------------------------------------------------------------------------------

DWORD GetCurrentThreadId()
{
    static thread_local DWORD id = static_cast<DWORD>(pthread_mach_thread_np(pthread_self()));
    return id;
}

DWORD GetCurrentProcessId() { return static_cast<DWORD>(getpid()); }
HANDLE GetCurrentProcess() { return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1)); }
HANDLE GetCurrentThread() { return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-2)); }

ULONGLONG GetTickCount64()
{
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000000ull;
}

DWORD GetTickCount() { return static_cast<DWORD>(GetTickCount64()); }

void Sleep(DWORD ms)
{
    if (ms == 0) {
        sched_yield();
    } else {
        usleep(static_cast<useconds_t>(ms) * 1000);
    }
}

DWORD GetLastError() { return gLastError; }

BOOL IsDebuggerPresent()
{
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
    struct kinfo_proc info{};
    size_t size = sizeof(info);
    if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0) return FALSE;
    return (info.kp_proc.p_flag & P_TRACED) != 0;
}

void DebugBreak() { __builtin_debugtrap(); }
void ExitProcess(UINT code) { _exit(static_cast<int>(code)); }
BOOL TerminateProcess(HANDLE, UINT code) { _exit(static_cast<int>(code)); }
BOOL CloseHandle(HANDLE) { return TRUE; }
BOOL DisableThreadLibraryCalls(HMODULE) { return TRUE; }

void OutputDebugStringA(char const*) {}
void OutputDebugStringW(wchar_t const*) {}

BOOL QueryPerformanceCounter(LARGE_INTEGER* v)
{
    v->QuadPart = static_cast<LONGLONG>(clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
    return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER* v)
{
    v->QuadPart = 1000000000ll;
    return TRUE;
}

int UuidCreate(UUID* uuid)
{
    uuid_t raw;
    uuid_generate_random(raw);
    memcpy(uuid, raw, sizeof(raw));
    return 0;
}

wchar_t const* GetCommandLineW()
{
    static std::wstring commandLine = [] {
        std::wstring line;
        auto argc = *_NSGetArgc();
        auto argv = *_NSGetArgv();
        for (int i = 0; i < argc; i++) {
            if (i) line += L' ';
            auto len = MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, nullptr, 0);
            std::wstring arg(len > 0 ? len - 1 : 0, L'\0');
            if (len > 1) MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, arg.data(), len);
            line += arg;
        }
        return line;
    }();
    return commandLine.c_str();
}

// ---------------------------------------------------------------------------------------------
// Modules
// ---------------------------------------------------------------------------------------------

namespace {

// Windows module names the shared code asks for, mapped to Mach-O images
char const* TranslateModuleName(std::string const& name, bool& isMainExecutable)
{
    isMainExecutable = false;
    if (name.empty() || strcasecmp(name.c_str(), "bg3.exe") == 0) {
        isMainExecutable = true;
        return nullptr;
    }

    if (strcasecmp(name.c_str(), "osiris.dll") == 0) return "libOsiris.dylib";
    return name.c_str();
}

}

HMODULE GetModuleHandleA(char const* name)
{
    bool isMain;
    std::string moduleName = name ? name : "";
    auto image = TranslateModuleName(moduleName, isMain);
    // bg3_dx11.exe, kernel32.dll and friends have no counterpart
    if (!isMain && (image == nullptr || strstr(image, ".exe") || strstr(image, ".dll"))) return nullptr;
    return const_cast<void*>(bg3se::darwin::GetImageHeader(image));
}

HMODULE GetModuleHandleW(wchar_t const* name)
{
    if (!name) return GetModuleHandleA(nullptr);
    return GetModuleHandleA(std::filesystem::path(name).string().c_str());
}

HMODULE LoadLibraryA(char const* name) { return GetModuleHandleA(name); }
HMODULE LoadLibraryW(wchar_t const* name) { return GetModuleHandleW(name); }
BOOL FreeLibrary(HMODULE) { return TRUE; }

FARPROC GetProcAddress(HMODULE module, char const* name)
{
    if (!module || !name) return nullptr;

    Dl_info info;
    if (!dladdr(module, &info) || !info.dli_fname) return nullptr;

    // Covers exported and local symbols alike
    return reinterpret_cast<FARPROC>(bg3se::darwin::FindSymbol(
        info.dli_fbase == bg3se::darwin::GetImageHeader(nullptr) ? nullptr : info.dli_fname, name));
}

DWORD GetModuleFileNameA(HMODULE module, char* path, DWORD size)
{
    std::string result;
    Dl_info info;
    if (module && dladdr(module, &info) && info.dli_fname) {
        result = info.dli_fname;
    } else {
        bg3se::darwin::ImageInfo image;
        if (bg3se::darwin::GetImageInfo(nullptr, image)) result = image.Path;
    }

    if (result.empty() || size == 0) return 0;
    auto len = std::min<std::size_t>(result.size(), size - 1);
    memcpy(path, result.data(), len);
    path[len] = 0;
    return static_cast<DWORD>(len);
}

DWORD GetModuleFileNameW(HMODULE module, wchar_t* path, DWORD size)
{
    char narrow[MAX_PATH * 4];
    auto len = GetModuleFileNameA(module, narrow, sizeof(narrow));
    if (len == 0 || size == 0) return 0;
    auto wlen = MultiByteToWideChar(CP_UTF8, 0, narrow, static_cast<int>(len), path, static_cast<int>(size - 1));
    path[wlen] = 0;
    return static_cast<DWORD>(wlen);
}

BOOL GetModuleInformation(HANDLE, HMODULE module, MODULEINFO* info, DWORD)
{
    Dl_info dl;
    if (!module || !dladdr(module, &dl) || !dl.dli_fname) return FALSE;

    bg3se::darwin::ImageInfo image;
    bool isMain = dl.dli_fbase == bg3se::darwin::GetImageHeader(nullptr);
    if (!bg3se::darwin::GetImageInfo(isMain ? nullptr : dl.dli_fname, image)) return FALSE;

    info->lpBaseOfDll = const_cast<uint8_t*>(image.Start);
    info->SizeOfImage = static_cast<DWORD>(image.Size);
    info->EntryPoint = nullptr;
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------------------------

BOOL VirtualProtect(LPVOID address, SIZE_T size, DWORD newProtect, DWORD* oldProtect)
{
    // Callers always follow the Windows idiom: make writable, patch, then restore the old
    // protection. Code pages report PAGE_EXECUTE_READ as "old" so the restore re-arms execution.
    bool wantsWrite = newProtect == PAGE_READWRITE || newProtect == PAGE_EXECUTE_READWRITE || newProtect == PAGE_WRITECOPY;
    if (wantsWrite) {
        mach_vm_address_t regionStart = reinterpret_cast<mach_vm_address_t>(address);
        mach_vm_size_t regionSize = 0;
        vm_region_basic_info_data_64_t info{};
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        bool wasExecutable = mach_vm_region(mach_task_self(), &regionStart, &regionSize, VM_REGION_BASIC_INFO_64,
            reinterpret_cast<vm_region_info_t>(&info), &count, &object) == KERN_SUCCESS
            && (info.protection & VM_PROT_EXECUTE) != 0;
        bool wasWritable = (info.protection & VM_PROT_WRITE) != 0;
        if (oldProtect) {
            *oldProtect = wasExecutable ? PAGE_EXECUTE_READ : (wasWritable ? PAGE_READWRITE : PAGE_READONLY);
        }
        return bg3se::darwin::MakeWritable(address, size);
    }

    if (oldProtect) *oldProtect = PAGE_READWRITE;
    if (newProtect == PAGE_EXECUTE_READ || newProtect == PAGE_EXECUTE) {
        return bg3se::darwin::RestoreProtection(address, size, true);
    }
    if (newProtect == PAGE_READONLY) {
        return bg3se::darwin::RestoreProtection(address, size, false);
    }
    return TRUE;
}

SIZE_T VirtualQuery(LPCVOID address, MEMORY_BASIC_INFORMATION* out, SIZE_T length)
{
    mach_vm_address_t regionStart = reinterpret_cast<mach_vm_address_t>(address);
    mach_vm_size_t regionSize = 0;
    vm_region_basic_info_data_64_t info{};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    if (mach_vm_region(mach_task_self(), &regionStart, &regionSize, VM_REGION_BASIC_INFO_64,
        reinterpret_cast<vm_region_info_t>(&info), &count, &object) != KERN_SUCCESS
        || regionStart > reinterpret_cast<mach_vm_address_t>(address)) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->BaseAddress = reinterpret_cast<PVOID>(regionStart);
    out->AllocationBase = out->BaseAddress;
    out->RegionSize = regionSize;
    out->State = MEM_COMMIT;
    if (info.protection & VM_PROT_EXECUTE) {
        out->Protect = (info.protection & VM_PROT_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    } else if (info.protection & VM_PROT_WRITE) {
        out->Protect = PAGE_READWRITE;
    } else if (info.protection & VM_PROT_READ) {
        out->Protect = PAGE_READONLY;
    } else {
        out->Protect = PAGE_NOACCESS;
    }
    return sizeof(*out);
}

BOOL IsBadReadPtr(const void* ptr, UINT_PTR size)
{
    return !bg3se::darwin::IsReadable(ptr, size);
}

BOOL IsBadWritePtr(void* ptr, UINT_PTR size)
{
    return !bg3se::darwin::IsReadable(ptr, size);
}

BOOL FlushInstructionCache(HANDLE, LPCVOID, SIZE_T) { return TRUE; }

// ---------------------------------------------------------------------------------------------
// Strings (wchar_t is UTF-32 here)
// ---------------------------------------------------------------------------------------------

int WideCharToMultiByte(UINT, DWORD, wchar_t const* wstr, int wlen, char* str, int len, char const*, BOOL*)
{
    if (wlen < 0) wlen = static_cast<int>(wcslen(wstr)) + 1;

    int written = 0;
    for (int i = 0; i < wlen; i++) {
        auto cp = static_cast<uint32_t>(wstr[i]);
        char buf[4];
        int n;
        if (cp < 0x80) { buf[0] = static_cast<char>(cp); n = 1; }
        else if (cp < 0x800) { buf[0] = static_cast<char>(0xC0 | (cp >> 6)); buf[1] = static_cast<char>(0x80 | (cp & 0x3F)); n = 2; }
        else if (cp < 0x10000) { buf[0] = static_cast<char>(0xE0 | (cp >> 12)); buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); buf[2] = static_cast<char>(0x80 | (cp & 0x3F)); n = 3; }
        else { buf[0] = static_cast<char>(0xF0 | (cp >> 18)); buf[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); buf[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); buf[3] = static_cast<char>(0x80 | (cp & 0x3F)); n = 4; }

        if (str && len > 0) {
            if (written + n > len) return 0;
            memcpy(str + written, buf, n);
        }
        written += n;
    }
    return written;
}

int MultiByteToWideChar(UINT, DWORD, char const* str, int len, wchar_t* wstr, int wlen)
{
    if (len < 0) len = static_cast<int>(strlen(str)) + 1;

    int written = 0;
    for (int i = 0; i < len; ) {
        auto c = static_cast<unsigned char>(str[i]);
        uint32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
        else { cp = 0xFFFD; n = 1; }

        if (i + n > len) { cp = 0xFFFD; n = 1; }
        for (int k = 1; k < n; k++) {
            cp = (cp << 6) | (static_cast<unsigned char>(str[i + k]) & 0x3F);
        }
        i += n;

        if (wstr && wlen > 0) {
            if (written >= wlen) return 0;
            wstr[written] = static_cast<wchar_t>(cp);
        }
        written++;
    }
    return written;
}

// ---------------------------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------------------------

BOOL CreateDirectoryA(char const* path, LPSECURITY_ATTRIBUTES)
{
    if (mkdir(path, 0755) == 0) return TRUE;
    gLastError = errno == EEXIST ? ERROR_ALREADY_EXISTS : ERROR_ACCESS_DENIED;
    return FALSE;
}

BOOL CreateDirectoryW(wchar_t const* path, LPSECURITY_ATTRIBUTES attrs)
{
    return CreateDirectoryA(ToNarrowPath(path).c_str(), attrs);
}

BOOL PathFileExistsA(char const* path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

BOOL PathFileExistsW(wchar_t const* path) { return PathFileExistsA(ToNarrowPath(path).c_str()); }

BOOL PathIsDirectoryW(wchar_t const* path)
{
    struct stat st;
    return stat(ToNarrowPath(path).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

BOOL DeleteFileW(wchar_t const* path) { return unlink(ToNarrowPath(path).c_str()) == 0; }
BOOL RemoveDirectoryW(wchar_t const* path) { return rmdir(ToNarrowPath(path).c_str()) == 0; }

BOOL CopyFileW(wchar_t const* from, wchar_t const* to, BOOL failIfExists)
{
    auto dst = ToNarrowPath(to);
    if (failIfExists && PathFileExistsA(dst.c_str())) {
        gLastError = ERROR_ALREADY_EXISTS;
        return FALSE;
    }
    return copyfile(ToNarrowPath(from).c_str(), dst.c_str(), nullptr, COPYFILE_DATA) == 0;
}

// ---------------------------------------------------------------------------------------------
// Console and UI
// ---------------------------------------------------------------------------------------------

HANDLE GetStdHandle(DWORD which)
{
    switch (which) {
    case STD_INPUT_HANDLE: return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(STDIN_FILENO + 1));
    case STD_OUTPUT_HANDLE: return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(STDOUT_FILENO + 1));
    default: return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(STDERR_FILENO + 1));
    }
}

BOOL SetConsoleTextAttribute(HANDLE, WORD) { return TRUE; }
BOOL AllocConsole() { return TRUE; }
BOOL FreeConsole() { return TRUE; }

int MessageBoxA(HWND, char const* text, char const* caption, UINT)
{
    bg3se::darwin::ShowAlert(caption ? caption : "BG3 Script Extender", text ? text : "");
    return 1;
}

int MessageBoxW(HWND, wchar_t const* text, wchar_t const* caption, UINT type)
{
    auto narrow = [](wchar_t const* s) {
        std::string out;
        if (!s) return out;
        auto len = WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(wcslen(s)), nullptr, 0, nullptr, nullptr);
        out.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(wcslen(s)), out.data(), len, nullptr, nullptr);
        return out;
    };
    return MessageBoxA(nullptr, narrow(text).c_str(), caption ? narrow(caption).c_str() : nullptr, type);
}

SHORT GetAsyncKeyState(int key)
{
    if (key == VK_SHIFT && bg3se::darwin::IsShiftKeyDown()) {
        return static_cast<SHORT>(0x8000);
    }
    return 0;
}
