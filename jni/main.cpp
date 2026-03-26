#include <cstdint>
#include <dlfcn.h>

typedef int (*DobbyHook_t)(void*, void*, void**);

static DobbyHook_t DobbyHook = nullptr;

static void hook(uintptr_t addr, void* replace) {
    // addr | 1 = Thumb mode
    void* target = reinterpret_cast<void*>(addr | 1);
    DobbyHook(target, replace, nullptr);
}

static void fake_AndroidPause()    {}
static void fake_StartUserPause()  {}
static void fake_CTimerSuspend()   {}

__attribute__((constructor))
static void init() {
    void* dobby = dlopen("libdobby.so", RTLD_NOW);
    if (!dobby) return;

    DobbyHook = (DobbyHook_t)dlsym(dobby, "DobbyHook");
    if (!DobbyHook) return;

    void* libGTASA = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
    if (!libGTASA) return;

    // Dapatkan base address dari simbol yang diketahui offset-nya
    uintptr_t sym = (uintptr_t)dlsym(libGTASA, "_Z12AndroidPausev");
    if (!sym) return;

    uintptr_t base = sym - 0x269af4;

    hook(base + 0x269af4, (void*)fake_AndroidPause);
    hook(base + 0x4210c8, (void*)fake_StartUserPause);
    hook(base + 0x420f14, (void*)fake_CTimerSuspend);
}
