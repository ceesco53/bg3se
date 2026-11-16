// Standalone tests for the macOS platform layer: the arm64 inline-hook engine (including
// relocation of PC-relative instructions displaced by a hook) and Mach-O symbol lookup.
//
//   cmake --build build/macos --target SEPlatformTests && build/macos/SEPlatformTests
#include "../Platform/Darwin.h"
#include "../Platform/FaultGuard.h"

#include <csignal>

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace bg3se::darwin;

static int gFailures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { printf("  FAILED: %s (line %d)\n", #expr, __LINE__); gFailures++; } \
} while (0)

#if defined(__aarch64__)

// Each function below starts with a different kind of PC-relative instruction inside the 16 bytes
// a hook displaces, and returns a value that proves the relocated copy behaved like the original.
extern "C" {
long TestPlain(long a, long b);
long TestAdrp(long a);
long TestAdr(long a);
long TestCbz(long a);
long TestCondBranch(long a);
long TestTbz(long a);
long TestCall(long a);
long TestLoadLiteral(long a);
double TestLoadFpLiteral(double a);
long TestTiny();
long TestHelper(long a);
extern long gTestData;
}

long gTestData = 1000;

// The hook targets get pages of their own: a page being patched is briefly non-executable, so
// the code doing the patching must not share it (in the game it is in a different image).
__asm__(R"(
    .section __TEXT,__hooktest,regular,pure_instructions
    .p2align 14

    .globl _TestHelper
_TestHelper:
    add x0, x0, #7
    ret

    .globl _TestPlain
_TestPlain:
    sub sp, sp, #16
    str x0, [sp, #8]
    add x0, x0, x1
    add sp, sp, #16
    ret

    .globl _TestAdrp
_TestAdrp:
    adrp x8, _gTestData@PAGE
    ldr x8, [x8, _gTestData@PAGEOFF]
    add x0, x0, x8
    nop
    ret

    .globl _TestAdr
_TestAdr:
    adr x8, 1f
    ldr x8, [x8]
    add x0, x0, x8
    nop
    ret
    .p2align 3
1:  .quad 2000

    .globl _TestCbz
_TestCbz:
    cbz x0, 2f
    add x0, x0, #1
    nop
    nop
    ret
2:  mov x0, #-5
    ret

    .globl _TestCondBranch
_TestCondBranch:
    cmp x0, #10
    b.gt 3f
    mov x0, #1
    nop
    ret
3:  mov x0, #2
    ret

    .globl _TestTbz
_TestTbz:
    nop
    tbz x0, #3, 4f
    mov x0, #100
    nop
    ret
4:  mov x0, #200
    ret

    .globl _TestCall
_TestCall:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    bl _TestHelper
    add x0, x0, #1
    ldp x29, x30, [sp], #16
    ret

    .globl _TestLoadLiteral
_TestLoadLiteral:
    ldr x8, 5f
    add x0, x0, x8
    nop
    nop
    ret
    .p2align 3
5:  .quad 3000

    .globl _TestLoadFpLiteral
_TestLoadFpLiteral:
    ldr d1, 6f
    fadd d0, d0, d1
    nop
    nop
    ret
    .p2align 3
6:  .double 0.5

    .globl _TestTiny
_TestTiny:
    mov x0, #42
    ret

    .p2align 14
    nop
    .text
)");

static int gDetourCalls = 0;

#define DEFINE_DETOUR(name, signature, args) \
    static decltype(&name) Original##name = nullptr; \
    static auto Detour##name signature -> decltype(name args) { gDetourCalls++; return Original##name args; }

DEFINE_DETOUR(TestPlain, (long a, long b), (a, b))
DEFINE_DETOUR(TestAdrp, (long a), (a))
DEFINE_DETOUR(TestAdr, (long a), (a))
DEFINE_DETOUR(TestCbz, (long a), (a))
DEFINE_DETOUR(TestCondBranch, (long a), (a))
DEFINE_DETOUR(TestTbz, (long a), (a))
DEFINE_DETOUR(TestCall, (long a), (a))
DEFINE_DETOUR(TestLoadLiteral, (long a), (a))
DEFINE_DETOUR(TestLoadFpLiteral, (double a), (a))

static long ReplacementPlain(long a, long b) { return a * b; }

template <class TFun, class TDetour, class TCheck>
static void HookAndCheck(char const* name, TFun* target, TDetour* detour, TFun** original, TCheck&& check)
{
    printf("%s\n", name);
    check();                       // behaviour before hooking
    auto callsBefore = gDetourCalls;
    CHECK(InstallHook(reinterpret_cast<void*>(target), reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original)));
    check();                       // same results through detour + relocated prologue
    CHECK(gDetourCalls > callsBefore);
    CHECK(RemoveHook(reinterpret_cast<void*>(target)));
    callsBefore = gDetourCalls;
    check();                       // and again once restored
    CHECK(gDetourCalls == callsBefore);
}

static void TestHooks()
{
    // Call through volatile pointers so the compiler can't fold the calls away
    HookAndCheck("hook: plain prologue", &TestPlain, &DetourTestPlain, &OriginalTestPlain, [] {
        long (*volatile f)(long, long) = &TestPlain;
        CHECK(f(2, 3) == 5);
    });
    HookAndCheck("hook: adrp", &TestAdrp, &DetourTestAdrp, &OriginalTestAdrp, [] {
        long (*volatile f)(long) = &TestAdrp;
        CHECK(f(1) == 1001);
    });
    HookAndCheck("hook: adr", &TestAdr, &DetourTestAdr, &OriginalTestAdr, [] {
        long (*volatile f)(long) = &TestAdr;
        CHECK(f(1) == 2001);
    });
    HookAndCheck("hook: cbz", &TestCbz, &DetourTestCbz, &OriginalTestCbz, [] {
        long (*volatile f)(long) = &TestCbz;
        CHECK(f(0) == -5);
        CHECK(f(4) == 5);
    });
    HookAndCheck("hook: b.cond", &TestCondBranch, &DetourTestCondBranch, &OriginalTestCondBranch, [] {
        long (*volatile f)(long) = &TestCondBranch;
        CHECK(f(5) == 1);
        CHECK(f(50) == 2);
    });
    HookAndCheck("hook: tbz", &TestTbz, &DetourTestTbz, &OriginalTestTbz, [] {
        long (*volatile f)(long) = &TestTbz;
        CHECK(f(8) == 100);
        CHECK(f(0) == 200);
    });
    HookAndCheck("hook: bl", &TestCall, &DetourTestCall, &OriginalTestCall, [] {
        long (*volatile f)(long) = &TestCall;
        CHECK(f(1) == 9);
    });
    HookAndCheck("hook: ldr literal", &TestLoadLiteral, &DetourTestLoadLiteral, &OriginalTestLoadLiteral, [] {
        long (*volatile f)(long) = &TestLoadLiteral;
        CHECK(f(1) == 3001);
    });
    HookAndCheck("hook: ldr fp literal", &TestLoadFpLiteral, &DetourTestLoadFpLiteral, &OriginalTestLoadFpLiteral, [] {
        double (*volatile f)(double) = &TestLoadFpLiteral;
        CHECK(f(1.0) == 1.5);
    });

    printf("hook: replacing behaviour\n");
    void* original{ nullptr };
    long (*volatile plain)(long, long) = &TestPlain;
    CHECK(InstallHook(reinterpret_cast<void*>(&TestPlain), reinterpret_cast<void*>(&ReplacementPlain), &original));
    CHECK(plain(6, 7) == 42);
    CHECK(reinterpret_cast<long (*)(long, long)>(original)(6, 7) == 13);
    CHECK(!InstallHook(reinterpret_cast<void*>(&TestPlain), reinterpret_cast<void*>(&ReplacementPlain), &original)); // already hooked
    CHECK(RemoveHook(reinterpret_cast<void*>(&TestPlain)));
    CHECK(plain(6, 7) == 13);

    printf("hook: function shorter than the patch is refused\n");
    long (*volatile tiny)() = &TestTiny;
    CHECK(!InstallHook(reinterpret_cast<void*>(&TestTiny), reinterpret_cast<void*>(&ReplacementPlain), &original));
    CHECK(tiny() == 42);
}

#else
static void TestHooks()
{
    printf("hook engine: not implemented for this architecture, skipped\n");
}
#endif

__attribute__((noinline, used)) static int LocalStaticFunction() { return 1234; }

static void TestSymbols()
{
    printf("symbols: main executable\n");
    ImageInfo info;
    CHECK(GetImageInfo(nullptr, info));
    CHECK(info.TextStart != nullptr && info.TextSize > 0);
    CHECK(info.Path.find("SEPlatformTests") != std::string::npos);

    // An exported symbol, and a local one that dlsym() would not find
    CHECK(FindSymbol(nullptr, "main") != nullptr);
    auto local = FindSymbol(nullptr, "_ZL19LocalStaticFunctionv");
    CHECK(local == reinterpret_cast<void*>(&LocalStaticFunction));
    CHECK(FindSymbol(nullptr, "__ZL19LocalStaticFunctionv") == local); // spelled the way nm prints it
    CHECK(FindSymbol(nullptr, "_ZN4this4does3not5existEv") == nullptr);

    printf("symbols: other images\n");
    CHECK(GetImageInfo("libSystem.B.dylib", info));
    CHECK(!GetImageInfo("libNotLoadedAnywhere.dylib", info));

    printf("memory: readability probe\n");
    int onStack = 0;
    CHECK(IsReadable(&onStack, sizeof(onStack)));
    CHECK(IsReadable(info.Start, 16));
    CHECK(!IsReadable(nullptr, 1));
    CHECK(!IsReadable(reinterpret_cast<void*>(0x10), 8));
}

// ---------------------------------------------------------------------------------------------
// Fault containment
// ---------------------------------------------------------------------------------------------

__attribute__((noinline)) static void WriteThrough(volatile int* pointer)
{
    *pointer = 1;
}

__attribute__((noinline)) static void CopyInto(void* destination)
{
    char source[32] = "payload";
    // The fault happens inside libsystem's memcpy; the nearest frame of ours is this one
    memcpy(destination, source, sizeof(source));
}

static void TestFaultGuard()
{
    printf("fault guard: null write is contained\n");
    InstallFaultHandler();
    InstallFaultHandler(); // idempotent
    auto before = ContainedFaultCount();
    {
        FaultGuard guard;
        volatile bool reachedEnd = false;
        if (sigsetjmp(guard.Buf, 0) == 0) {
            WriteThrough(reinterpret_cast<volatile int*>(0x10));
            reachedEnd = true;
        } else {
            CHECK(guard.Signal == SIGSEGV || guard.Signal == SIGBUS);
            CHECK(guard.FaultAddress == reinterpret_cast<void*>(0x10));
            char description[512];
            DescribeFault(guard, description, sizeof(description));
            CHECK(strstr(description, "WriteThrough") != nullptr);
            printf("  %s\n", description);
        }
        CHECK(!reachedEnd);
    }
    CHECK(ContainedFaultCount() == before + 1);

    printf("fault guard: fault inside a system library called by guarded code\n");
    {
        FaultGuard guard;
        volatile bool contained = false;
        if (sigsetjmp(guard.Buf, 0) == 0) {
            CopyInto(reinterpret_cast<void*>(0x20));
        } else {
            contained = true;
        }
        CHECK(contained);
    }

    printf("fault guard: nested guards unwind to the innermost\n");
    {
        FaultGuard outer;
        volatile int innerHits = 0, outerHits = 0;
        if (sigsetjmp(outer.Buf, 0) == 0) {
            {
                FaultGuard inner;
                if (sigsetjmp(inner.Buf, 0) == 0) {
                    WriteThrough(nullptr);
                } else {
                    innerHits = innerHits + 1;
                }
            }
            // The inner guard is gone; this one belongs to the outer guard
            WriteThrough(nullptr);
        } else {
            outerHits = outerHits + 1;
        }
        CHECK(innerHits == 1);
        CHECK(outerHits == 1);
    }

    printf("fault guard: failed assertion (trap) is contained\n");
    {
        FaultGuard guard;
        volatile bool contained = false;
        if (sigsetjmp(guard.Buf, 0) == 0) {
            __builtin_debugtrap();
        } else {
            contained = guard.Signal == SIGTRAP;
        }
        CHECK(contained);
    }

    printf("fault guard: the signal is deliverable again afterwards\n");
    {
        FaultGuard guard;
        volatile int hits = 0;
        for (volatile int i = 0; i < 3; i = i + 1) {
            if (sigsetjmp(guard.Buf, 0) == 0) {
                WriteThrough(nullptr);
            } else {
                hits = hits + 1;
            }
        }
        CHECK(hits == 3);
    }
}

int main()
{
    TestSymbols();
    TestHooks();
    TestFaultGuard();

    if (gFailures == 0) {
        printf("\nAll platform tests passed.\n");
        return 0;
    }

    printf("\n%d check(s) FAILED.\n", gFailures);
    return 1;
}
