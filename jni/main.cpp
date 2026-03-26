#include <cstdint>
#include <dlfcn.h>

typedef int (*DobbyHook_t)(void*, void*, void**);

static void fake_AndroidPause()   {}
static void fake_StartUserPause() {}
static void fake_CTimerSuspend()  {}

static void hook(DobbyHook_t fn, uintptr_t addr, void* replace) {
    void* target = reinterpret_cast<void*>(addr | 1);
    fn(target, replace, nullptr);
}

extern "C" void OnModLoad() {
    void* dobby = dlopen("libdobby.so", RTLD_NOW);
    if (!dobby) return;

    DobbyHook_t DobbyHook = (DobbyHook_t)dlsym(dobby, "DobbyHook");
    if (!DobbyHook) return;

    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) return;

    uintptr_t sym = (uintptr_t)dlsym(lib, "_Z12AndroidPausev");
    if (!sym) return;

    uintptr_t base = sym - 0x269af4;

    hook(DobbyHook, base + 0x269af4, (void*)fake_AndroidPause);
    hook(DobbyHook, base + 0x4210c8, (void*)fake_StartUserPause);
    hook(DobbyHook, base + 0x420f14, (void*)fake_CTimerSuspend);
}
