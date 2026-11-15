// Inline function hooking for arm64 macOS, plus the Detours-shaped API the shared code calls.
//
// A hook overwrites the first 16 bytes (4 instructions) of the target with an absolute jump:
//
//     ldr x17, #8
//     br  x17
//     .quad <detour>
//
// x17 (IP1) may be clobbered at any procedure call boundary under AAPCS64, so this is safe at a
// function entry point. The displaced instructions are copied into a trampoline, with every
// PC-relative instruction rewritten to behave as if it still ran at its original address, followed
// by a jump back to target+16.
//
// The game is not built with the hardened runtime, so its __TEXT pages can be made writable
// (copy-on-write) with mach_vm_protect, and plain mmap'd memory can be made executable.
#include "Darwin.h"

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <libkern/OSCacheControl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <unistd.h>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <detours.h>

namespace bg3se::darwin {

namespace {

std::size_t PageSize()
{
    static std::size_t size = static_cast<std::size_t>(getpagesize());
    return size;
}

std::uintptr_t PageFloor(std::uintptr_t address)
{
    return address & ~(PageSize() - 1);
}

}

bool MakeWritable(void const* address, std::size_t size)
{
    auto start = PageFloor(reinterpret_cast<std::uintptr_t>(address));
    auto end = PageFloor(reinterpret_cast<std::uintptr_t>(address) + size + PageSize() - 1);
    return mach_vm_protect(mach_task_self(), start, end - start, FALSE,
        VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) == KERN_SUCCESS;
}

bool RestoreProtection(void const* address, std::size_t size, bool executable)
{
    auto start = PageFloor(reinterpret_cast<std::uintptr_t>(address));
    auto end = PageFloor(reinterpret_cast<std::uintptr_t>(address) + size + PageSize() - 1);
    auto ok = mach_vm_protect(mach_task_self(), start, end - start, FALSE,
        executable ? (VM_PROT_READ | VM_PROT_EXECUTE) : VM_PROT_READ) == KERN_SUCCESS;
    if (executable) {
        sys_icache_invalidate(const_cast<void*>(address), size);
    }
    return ok;
}

bool IsReadable(void const* address, std::size_t size)
{
    if (address == nullptr) return false;

    auto cur = reinterpret_cast<mach_vm_address_t>(address);
    auto end = cur + (size ? size : 1);
    while (cur < end) {
        mach_vm_address_t regionStart = cur;
        mach_vm_size_t regionSize = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        if (mach_vm_region(mach_task_self(), &regionStart, &regionSize, VM_REGION_BASIC_INFO_64,
            reinterpret_cast<vm_region_info_t>(&info), &count, &object) != KERN_SUCCESS) {
            return false;
        }

        // mach_vm_region returns the next region if `cur` itself is unmapped
        if (regionStart > cur || (info.protection & VM_PROT_READ) == 0) {
            return false;
        }

        cur = regionStart + regionSize;
    }

    return true;
}

#if defined(__aarch64__)

namespace {

constexpr std::size_t HookSize = 16;
constexpr unsigned HookInstructions = HookSize / 4;

struct Hook
{
    void* Target{ nullptr };
    void* Detour{ nullptr };
    void* Trampoline{ nullptr };
    uint32_t Original[HookInstructions];
};

// Function-local statics: hooks are installed from the dylib's load constructor, possibly before
// this translation unit's dynamic initializers would have run.
std::mutex& HooksMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<void*, Hook>& Hooks()
{
    static auto hooks = new std::unordered_map<void*, Hook>();
    return *hooks;
}

inline int64_t SignExtend(uint64_t value, unsigned bits)
{
    auto shift = 64 - bits;
    return static_cast<int64_t>(value << shift) >> shift;
}

// Emits code into a trampoline; 64-bit literals are collected and placed after the code.
class TrampolineWriter
{
public:
    TrampolineWriter(uint32_t* buffer, std::size_t capacityWords)
        : buffer_(buffer), capacity_(capacityWords)
    {}

    bool Emit(uint32_t instruction)
    {
        if (count_ >= capacity_) return false;
        buffer_[count_++] = instruction;
        return true;
    }

    // ldr <reg>, <literal holding `value`>; the displacement is fixed up in Finish()
    bool EmitLoadLiteral(unsigned reg, uint64_t value)
    {
        fixups_.push_back({ count_, value });
        return Emit(0x58000000u | reg);
    }

    // Jump to an absolute address via x17
    bool EmitAbsoluteJump(uint64_t address)
    {
        return EmitLoadLiteral(17, address) && Emit(0xD61F0220u); // br x17
    }

    std::size_t Position() const { return count_; }

    bool Finish()
    {
        // Literals must be 8-byte aligned relative to the (page-aligned) buffer
        if (count_ & 1) {
            if (!Emit(0xD503201Fu)) return false; // nop
        }

        for (auto const& fixup : fixups_) {
            if (count_ + 2 > capacity_) return false;
            auto literalIndex = count_;
            memcpy(&buffer_[count_], &fixup.Value, sizeof(uint64_t));
            count_ += 2;

            auto disp = static_cast<int64_t>(literalIndex) - static_cast<int64_t>(fixup.Index);
            buffer_[fixup.Index] |= (static_cast<uint32_t>(disp) & 0x7FFFFu) << 5;
        }

        return true;
    }

private:
    struct Fixup
    {
        std::size_t Index;
        uint64_t Value;
    };

    uint32_t* buffer_;
    std::size_t capacity_;
    std::size_t count_{ 0 };
    std::vector<Fixup> fixups_;
};

// Rewrites one displaced instruction. `pc` is the address the instruction originally ran at.
// Returns false for instructions that can't be relocated safely.
bool Relocate(TrampolineWriter& out, uint32_t ins, uint64_t pc, uint64_t hookStart)
{
    auto targetsHookedRegion = [&](uint64_t address) {
        // A branch back into the displaced bytes would land in the middle of the detour jump
        return address >= hookStart && address < hookStart + HookSize;
    };

    // ADR / ADRP
    if ((ins & 0x1F000000u) == 0x10000000u) {
        bool isPage = (ins & 0x80000000u) != 0;
        unsigned rd = ins & 0x1F;
        auto imm = SignExtend((((ins >> 5) & 0x7FFFF) << 2) | ((ins >> 29) & 0x3), 21);
        uint64_t value = isPage
            ? (pc & ~0xFFFull) + (static_cast<uint64_t>(imm) << 12)
            : pc + imm;
        return out.EmitLoadLiteral(rd, value);
    }

    // B / BL
    if ((ins & 0x7C000000u) == 0x14000000u) {
        bool isLink = (ins & 0x80000000u) != 0;
        uint64_t target = pc + (SignExtend(ins & 0x03FFFFFFu, 26) << 2);
        if (targetsHookedRegion(target)) return false;
        return out.EmitLoadLiteral(17, target)
            && out.Emit(isLink ? 0xD63F0220u /* blr x17 */ : 0xD61F0220u /* br x17 */);
    }

    // B.cond, CBZ / CBNZ, TBZ / TBNZ: keep the condition, branch over a local absolute jump
    bool isCondBranch = (ins & 0xFF000010u) == 0x54000000u;
    bool isCompareBranch = (ins & 0x7E000000u) == 0x34000000u;
    bool isTestBranch = (ins & 0x7E000000u) == 0x36000000u;
    if (isCondBranch || isCompareBranch || isTestBranch) {
        uint64_t target;
        uint32_t rewritten;
        if (isTestBranch) {
            target = pc + (SignExtend((ins >> 5) & 0x3FFF, 14) << 2);
            rewritten = (ins & ~(0x3FFFu << 5)) | (2u << 5);
        } else {
            target = pc + (SignExtend((ins >> 5) & 0x7FFFF, 19) << 2);
            rewritten = (ins & ~(0x7FFFFu << 5)) | (2u << 5);
        }

        if (targetsHookedRegion(target)) return false;

        //   <cond> taken        ; +8
        //   b      not_taken    ; +12
        // taken:
        //   ldr x17, =target
        //   br  x17
        // not_taken:
        return out.Emit(rewritten)
            && out.Emit(0x14000003u)
            && out.EmitAbsoluteJump(target);
    }

    // LDR (literal): integer, LDRSW, SIMD&FP; PRFM (literal)
    if ((ins & 0x3B000000u) == 0x18000000u) {
        unsigned opc = ins >> 30;
        bool isSimd = (ins & 0x04000000u) != 0;
        unsigned rt = ins & 0x1F;
        uint64_t address = pc + (SignExtend((ins >> 5) & 0x7FFFF, 19) << 2);

        if (!isSimd) {
            if (opc == 3) {
                return out.Emit(0xD503201Fu); // prfm: a hint, safe to drop
            }

            // Load the address into the destination register itself, then load through it
            if (!out.EmitLoadLiteral(rt, address)) return false;
            switch (opc) {
            case 0: return out.Emit(0xB9400000u | (rt << 5) | rt); // ldr wt, [xt]
            case 1: return out.Emit(0xF9400000u | (rt << 5) | rt); // ldr xt, [xt]
            default: return out.Emit(0xB9800000u | (rt << 5) | rt); // ldrsw xt, [xt]
            }
        }

        if (!out.EmitLoadLiteral(17, address)) return false;
        switch (opc) {
        case 0: return out.Emit(0xBD400000u | (17u << 5) | rt); // ldr st, [x17]
        case 1: return out.Emit(0xFD400000u | (17u << 5) | rt); // ldr dt, [x17]
        case 2: return out.Emit(0x3DC00000u | (17u << 5) | rt); // ldr qt, [x17]
        default: return false;
        }
    }

    // Everything else is position independent
    return out.Emit(ins);
}

// Other threads must not be executing a page while its protection is being flipped
class ThreadSuspender
{
public:
    ThreadSuspender()
    {
        auto self = mach_thread_self();
        thread_act_array_t threads{ nullptr };
        mach_msg_type_number_t count{ 0 };
        if (task_threads(mach_task_self(), &threads, &count) == KERN_SUCCESS) {
            // Allocate before suspending anything: a suspended thread may hold the malloc lock
            suspended_.reserve(count);
            for (mach_msg_type_number_t i = 0; i < count; i++) {
                if (threads[i] != self && thread_suspend(threads[i]) == KERN_SUCCESS) {
                    suspended_.push_back(threads[i]);
                } else {
                    mach_port_deallocate(mach_task_self(), threads[i]);
                }
            }
            vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), count * sizeof(thread_act_t));
        }
        mach_port_deallocate(mach_task_self(), self);
    }

    ~ThreadSuspender()
    {
        for (auto thread : suspended_) {
            thread_resume(thread);
            mach_port_deallocate(mach_task_self(), thread);
        }
    }

private:
    std::vector<thread_act_t> suspended_;
};

// NOTE: while the target page is writable it is not executable. Other threads are suspended for
// that window; the caller's own code must not live on the page being patched (it never does when
// patching the game from this dylib).
bool WriteCode(void* target, void const* data, std::size_t size)
{
    ThreadSuspender suspender;
    if (!MakeWritable(target, size)) return false;
    memcpy(target, data, size);
    return RestoreProtection(target, size, true);
}

}

bool InstallHook(void* target, void* detour, void** original)
{
    std::lock_guard lock(HooksMutex());
    auto& gHooks = Hooks();
    if (target == nullptr || detour == nullptr || gHooks.find(target) != gHooks.end()) {
        return false;
    }

    Hook hook;
    hook.Target = target;
    hook.Detour = detour;
    memcpy(hook.Original, target, HookSize);

    // If the function ends (ret / indirect or unconditional branch) before the 4th instruction,
    // it may be shorter than the patch, which would then overwrite whatever follows it.
    for (unsigned i = 0; i < HookInstructions - 1; i++) {
        auto ins = hook.Original[i];
        bool isReturnOrIndirect = (ins & 0xFFFFFC1Fu) == 0xD61F0000u  // br
            || (ins & 0xFFFFFC1Fu) == 0xD65F0000u;                    // ret
        bool isBranch = (ins & 0xFC000000u) == 0x14000000u;           // b
        if (isReturnOrIndirect || isBranch) {
            return false;
        }
    }

    // One page per hook: a trampoline is never writable again once another thread may run it
    auto page = mmap(nullptr, PageSize(), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (page == MAP_FAILED) return false;

    TrampolineWriter writer(static_cast<uint32_t*>(page), PageSize() / sizeof(uint32_t));
    auto targetAddress = reinterpret_cast<uint64_t>(target);
    bool ok = true;
    for (unsigned i = 0; i < HookInstructions && ok; i++) {
        ok = Relocate(writer, hook.Original[i], targetAddress + i * 4, targetAddress);
    }

    ok = ok && writer.EmitAbsoluteJump(targetAddress + HookSize) && writer.Finish();
    if (!ok || mprotect(page, PageSize(), PROT_READ | PROT_EXEC) != 0) {
        munmap(page, PageSize());
        return false;
    }

    sys_icache_invalidate(page, PageSize());
    hook.Trampoline = page;

    uint32_t patch[HookInstructions];
    patch[0] = 0x58000051u; // ldr x17, #8
    patch[1] = 0xD61F0220u; // br x17
    memcpy(&patch[2], &detour, sizeof(void*));
    if (!WriteCode(target, patch, sizeof(patch))) {
        munmap(page, PageSize());
        return false;
    }

    if (original) *original = hook.Trampoline;
    gHooks.insert(std::make_pair(target, hook));
    return true;
}

bool RemoveHook(void* target)
{
    std::lock_guard lock(HooksMutex());
    auto& gHooks = Hooks();
    auto it = gHooks.find(target);
    if (it == gHooks.end()) return false;

    if (!WriteCode(target, it->second.Original, HookSize)) return false;

    // The trampoline page is intentionally leaked: another thread may still be executing it
    gHooks.erase(it);
    return true;
}

#else

// Intel Macs run the game's x86_64 slice; the hook engine only implements arm64 so far.
bool InstallHook(void*, void*, void**) { return false; }
bool RemoveHook(void*) { return false; }

#endif

}

// ---------------------------------------------------------------------------------------------
// Detours API
// ---------------------------------------------------------------------------------------------

LONG DetourTransactionBegin() { return NO_ERROR; }
LONG DetourTransactionCommit() { return NO_ERROR; }
LONG DetourTransactionAbort() { return NO_ERROR; }
LONG DetourUpdateThread(HANDLE) { return NO_ERROR; }

namespace {
// Detours identifies a hook by the trampoline pointer it handed out; map those back to targets
std::mutex& DetourMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<void*, void*>& TrampolineToTarget()
{
    static auto map = new std::unordered_map<void*, void*>();
    return *map;
}
constexpr LONG ErrorInvalidOperation = 4317; // ERROR_INVALID_OPERATION
}

LONG DetourAttachEx(PVOID* ppPointer, PVOID pDetour, PDETOUR_TRAMPOLINE* ppRealTrampoline, PVOID* ppRealTarget, PVOID* ppRealDetour)
{
    if (ppPointer == nullptr || *ppPointer == nullptr) return ErrorInvalidOperation;

    void* target = *ppPointer;
    void* trampoline{ nullptr };
    if (!bg3se::darwin::InstallHook(target, pDetour, &trampoline)) {
        return ErrorInvalidOperation;
    }

    {
        std::lock_guard lock(DetourMutex());
        auto& gTrampolineToTarget = TrampolineToTarget();
        gTrampolineToTarget[trampoline] = target;
    }

    *ppPointer = trampoline;
    if (ppRealTrampoline) *ppRealTrampoline = static_cast<PDETOUR_TRAMPOLINE>(trampoline);
    if (ppRealTarget) *ppRealTarget = target;
    if (ppRealDetour) *ppRealDetour = pDetour;
    return NO_ERROR;
}

LONG DetourAttach(PVOID* ppPointer, PVOID pDetour)
{
    return DetourAttachEx(ppPointer, pDetour, nullptr, nullptr, nullptr);
}

LONG DetourDetach(PVOID* ppPointer, PVOID pDetour)
{
    if (ppPointer == nullptr || *ppPointer == nullptr) return ErrorInvalidOperation;

    void* target{ nullptr };
    {
        std::lock_guard lock(DetourMutex());
        auto& gTrampolineToTarget = TrampolineToTarget();
        auto it = gTrampolineToTarget.find(*ppPointer);
        if (it == gTrampolineToTarget.end()) return ErrorInvalidOperation;
        target = it->second;
        gTrampolineToTarget.erase(it);
    }

    if (!bg3se::darwin::RemoveHook(target)) return ErrorInvalidOperation;
    *ppPointer = target;
    return NO_ERROR;
}
