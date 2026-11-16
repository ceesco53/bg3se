#include "FaultGuard.h"

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cxxabi.h>
#include <cstdlib>

namespace bg3se::darwin {

namespace {

struct ImageRange
{
    std::uintptr_t Start{ 0 };
    std::uintptr_t End{ 0 };

    bool Contains(std::uintptr_t address) const
    {
        return address >= Start && address < End;
    }
};

pthread_key_t gGuardKey;
pthread_once_t gGuardKeyOnce = PTHREAD_ONCE_INIT;

// Written once, before the handler is installed; read from the signal handler
ImageRange gExtenderImage;
ImageRange gGameImages[2]; // main executable, libOsiris
struct sigaction gPreviousActions[NSIG];
std::atomic<bool> gInstalled{ false };
std::atomic<unsigned> gContainedFaults{ 0 };

const int HandledSignals[] = { SIGSEGV, SIGBUS, SIGTRAP };

void CreateGuardKey()
{
    pthread_key_create(&gGuardKey, nullptr);
}

ImageRange GetImageRange(mach_header_64 const* header, std::intptr_t slide)
{
    ImageRange range;
    range.Start = ~std::uintptr_t(0);
    auto cmd = reinterpret_cast<load_command const*>(header + 1);
    for (uint32_t i = 0; i < header->ncmds; i++) {
        if (cmd->cmd == LC_SEGMENT_64) {
            auto seg = reinterpret_cast<segment_command_64 const*>(cmd);
            if (strcmp(seg->segname, SEG_TEXT) == 0) {
                range.Start = seg->vmaddr + slide;
                range.End = range.Start + seg->vmsize;
            }
        }
        cmd = reinterpret_cast<load_command const*>(reinterpret_cast<uint8_t const*>(cmd) + cmd->cmdsize);
    }
    if (range.End == 0) range.Start = 0;
    return range;
}

void FindImages()
{
    Dl_info self;
    void const* selfBase = nullptr;
    if (dladdr(reinterpret_cast<void const*>(&FindImages), &self)) {
        selfBase = self.dli_fbase;
    }

    auto count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        auto header = reinterpret_cast<mach_header_64 const*>(_dyld_get_image_header(i));
        auto name = _dyld_get_image_name(i);
        if (!header || header->magic != MH_MAGIC_64) continue;

        auto range = GetImageRange(header, _dyld_get_image_vmaddr_slide(i));
        if (header == selfBase) {
            gExtenderImage = range;
        } else if (header->filetype == MH_EXECUTE) {
            gGameImages[0] = range;
        } else if (name && strstr(name, "libOsiris.dylib")) {
            gGameImages[1] = range;
        }
    }
}

enum class CodeOwner { Unknown, Extender, Game };

CodeOwner Classify(std::uintptr_t address)
{
    if (gExtenderImage.Contains(address)) return CodeOwner::Extender;
    for (auto const& image : gGameImages) {
        if (image.Contains(address)) return CodeOwner::Game;
    }
    return CodeOwner::Unknown;
}

// Whose code was running: the nearest frame, starting from the faulting instruction, that belongs
// to either the extender or the game. System library frames in between (memcpy, ...) are skipped.
CodeOwner FindFaultOwner(ucontext_t const* context)
{
#if defined(__aarch64__)
    auto const& state = context->uc_mcontext->__ss;
    auto pc = static_cast<std::uintptr_t>(__darwin_arm_thread_state64_get_pc(state));
    auto lr = static_cast<std::uintptr_t>(__darwin_arm_thread_state64_get_lr(state));
    auto fp = static_cast<std::uintptr_t>(__darwin_arm_thread_state64_get_fp(state));

    auto owner = Classify(pc);
    if (owner != CodeOwner::Unknown) return owner;
    owner = Classify(lr);
    if (owner != CodeOwner::Unknown) return owner;

    auto self = pthread_self();
    auto stackTop = reinterpret_cast<std::uintptr_t>(pthread_get_stackaddr_np(self));
    auto stackBottom = stackTop - pthread_get_stacksize_np(self);
    for (unsigned depth = 0; depth < 64; depth++) {
        if (fp < stackBottom || fp + 16 > stackTop || (fp & 7) != 0) break;
        auto frame = reinterpret_cast<std::uintptr_t const*>(fp);
        owner = Classify(frame[1]);
        if (owner != CodeOwner::Unknown) return owner;
        if (frame[0] <= fp) break;
        fp = frame[0];
    }
#endif
    return CodeOwner::Unknown;
}

void PassToPreviousHandler(int sig, siginfo_t* info, void* context)
{
    auto const& previous = gPreviousActions[sig];
    if (previous.sa_flags & SA_SIGINFO) {
        if (previous.sa_sigaction) {
            previous.sa_sigaction(sig, info, context);
            return;
        }
    } else if (previous.sa_handler == SIG_IGN) {
        return;
    } else if (previous.sa_handler != SIG_DFL && previous.sa_handler != nullptr) {
        previous.sa_handler(sig);
        return;
    }

    // Default action: restore it and return, so the faulting instruction runs again and the
    // process dies the way it would have without the extender.
    signal(sig, SIG_DFL);
}

void FaultHandler(int sig, siginfo_t* info, void* context)
{
    auto guard = static_cast<FaultGuard*>(pthread_getspecific(gGuardKey));
    if (guard != nullptr && FindFaultOwner(static_cast<ucontext_t const*>(context)) == CodeOwner::Extender) {
        guard->Signal = sig;
        guard->FaultAddress = info ? info->si_addr : nullptr;
#if defined(__aarch64__)
        guard->FaultPC = reinterpret_cast<void*>(
            __darwin_arm_thread_state64_get_pc(static_cast<ucontext_t const*>(context)->uc_mcontext->__ss));
#endif
        gContainedFaults.fetch_add(1, std::memory_order_relaxed);
        // The handler is installed with SA_NODEFER and an empty mask, so the signal mask here is
        // the one the thread had; nothing to restore on the way out.
        siglongjmp(guard->Buf, 1);
    }

    PassToPreviousHandler(sig, info, context);
}

}

FaultGuard::FaultGuard()
{
    pthread_once(&gGuardKeyOnce, CreateGuardKey);
    Previous = static_cast<FaultGuard*>(pthread_getspecific(gGuardKey));
    pthread_setspecific(gGuardKey, this);
}

FaultGuard::~FaultGuard()
{
    pthread_setspecific(gGuardKey, Previous);
}

void InstallFaultHandler()
{
    pthread_once(&gGuardKeyOnce, CreateGuardKey);
    if (!gInstalled.exchange(true)) {
        FindImages();
    }

    for (auto sig : HandledSignals) {
        struct sigaction current;
        if (sigaction(sig, nullptr, &current) != 0) continue;
        if ((current.sa_flags & SA_SIGINFO) && current.sa_sigaction == &FaultHandler) continue;

        // Someone (the game's crash reporter) installed a handler after us, or this is the first call
        gPreviousActions[sig] = current;

        struct sigaction action;
        memset(&action, 0, sizeof(action));
        action.sa_sigaction = &FaultHandler;
        action.sa_flags = SA_SIGINFO | SA_NODEFER;
        sigemptyset(&action.sa_mask);
        sigaction(sig, &action, nullptr);
    }
}

void DescribeFault(FaultGuard const& guard, char* buffer, unsigned long size)
{
    char const* name = "fault";
    switch (guard.Signal) {
    case SIGSEGV: name = "SIGSEGV (invalid memory access)"; break;
    case SIGBUS: name = "SIGBUS (invalid memory access)"; break;
    case SIGTRAP: name = "SIGTRAP (failed assertion)"; break;
    }

    char location[512];
    Dl_info info;
    if (guard.FaultPC && dladdr(guard.FaultPC, &info) && info.dli_fname) {
        auto image = strrchr(info.dli_fname, '/');
        image = image ? image + 1 : info.dli_fname;
        if (info.dli_sname) {
            int status = 0;
            auto demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            snprintf(location, sizeof(location), "%s + %ld (%s)", demangled ? demangled : info.dli_sname,
                static_cast<long>(reinterpret_cast<std::uintptr_t>(guard.FaultPC) - reinterpret_cast<std::uintptr_t>(info.dli_saddr)), image);
            free(demangled);
        } else {
            snprintf(location, sizeof(location), "%s + 0x%lx", image,
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(guard.FaultPC) - reinterpret_cast<std::uintptr_t>(info.dli_fbase)));
        }
    } else {
        snprintf(location, sizeof(location), "%p", guard.FaultPC);
    }

    if (guard.Signal == SIGTRAP) {
        snprintf(buffer, size, "%s in %s", name, location);
    } else {
        snprintf(buffer, size, "%s at address %p in %s", name, guard.FaultAddress, location);
    }
}

unsigned ContainedFaultCount()
{
    return gContainedFaults.load(std::memory_order_relaxed);
}

}
