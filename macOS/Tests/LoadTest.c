// Loads the extender dylib into a process that is not the game and runs its startup path.
// Nothing can be resolved here, so this checks that the extender comes up and stays inert
// instead of crashing when the engine isn't what it expects.
//
//   build/macos/SELoadTest build/macos/libBG3ScriptExtender.dylib
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: SELoadTest <libBG3ScriptExtender.dylib>\n");
        return 2;
    }

    void* handle = dlopen(argv[1], RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }
    printf("loaded; inert in a non-game process\n");

    void (*initialize)(void) = (void (*)(void))dlsym(handle, "BG3SEInitializeForTesting");
    if (!initialize) {
        fprintf(stderr, "BG3SEInitializeForTesting not exported\n");
        return 1;
    }

    initialize();
    printf("startup path completed\n");
    return 0;
}
