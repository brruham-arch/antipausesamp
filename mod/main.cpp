#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <android/log.h>

#define LOG_TAG "libantipause"
#define LOGFILE "/storage/emulated/0/antipause_log.txt"

static void logf(const char* msg) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg);
    FILE* f = fopen(LOGFILE, "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}
static void logff(const char* fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    logf(buf);
}

#define OFF_IsAndroidPaused   0x006855bcUL
#define OFF_WasAndroidPaused  0x006d7048UL
#define OFF_CTimer_UserPause  0x0096b514UL
#define OFF_SetAndroidPaused  0x00269ae4UL

typedef int (*tDobbyHook)(void*, void*, void**);
static tDobbyHook pDobbyHook = nullptr;

static uintptr_t g_base    = 0;
static bool      g_running = false;
static pthread_t g_thread;

static uintptr_t getGTASABase() {
    // Sesuai referensi: dlopen RTLD_NOLOAD, handle = base
    void* h = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
    if (h) {
        uintptr_t base = (uintptr_t)h;
        dlclose(h);
        logff("[antipause] base via dlopen = 0x%08X", base);
        return base;
    }
    // Fallback: /proc/self/maps
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return 0;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "libGTASA.so") && strstr(line, "r-xp")) {
            uintptr_t base = (uintptr_t)strtoul(line, nullptr, 16);
            fclose(fp);
            logff("[antipause] base via maps = 0x%08X", base);
            return base;
        }
    }
    fclose(fp);
    return 0;
}

static bool patchMem(uintptr_t addr, const uint8_t* data, size_t len) {
    uintptr_t page = addr & ~(uintptr_t)0xFFF;
    if (mprotect((void*)page, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return false;
    memcpy((void*)addr, data, len);
    __builtin___clear_cache((char*)addr, (char*)(addr + len));
    return true;
}

static void layer1_patch() {
    uintptr_t fn = g_base + OFF_SetAndroidPaused;
    const uint8_t bxlr[] = { 0x70, 0x47 };
    if (patchMem(fn, bxlr, sizeof(bxlr)))
        logff("[L1] patch OK @ 0x%08X", fn);
    else
        logff("[L1] patch GAGAL @ 0x%08X", fn);
}

static void (*orig_SetAndroidPaused)(int) = nullptr;
static void hooked_SetAndroidPaused(int paused) {
    logff("[L2] SetAndroidPaused(%d) diblokir", paused);
}

static void layer2_dobby() {
    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) { logf("[L2] libdobby.so tidak ditemukan"); return; }
    pDobbyHook = (tDobbyHook)dlsym(hDobby, "DobbyHook");
    if (!pDobbyHook) { logf("[L2] DobbyHook tidak ditemukan"); return; }
    void* target = (void*)(g_base + OFF_SetAndroidPaused + 1); // THUMB +1
    int ret = pDobbyHook(target, (void*)hooked_SetAndroidPaused, (void**)&orig_SetAndroidPaused);
    logff("[L2] DobbyHook ret=%d target=%p", ret, target);
}

static void* antiPauseThread(void*) {
    logf("[L3] thread started");
    volatile uint8_t* pIs  = (volatile uint8_t*)(g_base + OFF_IsAndroidPaused);
    volatile uint8_t* pWas = (volatile uint8_t*)(g_base + OFF_WasAndroidPaused);
    volatile uint8_t* pCT  = (volatile uint8_t*)(g_base + OFF_CTimer_UserPause);
    while (g_running) {
        *pIs  = 0;
        *pWas = 0;
        *pCT  = 0;
        usleep(30000);
    }
    return nullptr;
}

static void layer3_thread() {
    g_running = true;
    if (pthread_create(&g_thread, nullptr, antiPauseThread, nullptr) == 0)
        logf("[L3] thread OK");
    else {
        g_running = false;
        logf("[L3] pthread_create GAGAL");
    }
}

// ── AML entry points — TANPA visibility attribute, sama seperti voicefx ──
extern "C" {

void* __GetModInfo() {
    static const char* info = "libantipause|1.0|Anti-Pause SA-MP|brruham-arch";
    return (void*)info;
}

void OnModPreLoad() {
    remove(LOGFILE);
    logf("[antipause] OnModPreLoad");
    g_base = getGTASABase();
    if (!g_base) { logf("[antipause] GAGAL base libGTASA.so"); return; }
    layer1_patch();
    layer2_dobby();
}

void OnModLoad() {
    logf("[antipause] OnModLoad");
    if (!g_base) return;
    layer3_thread();
    logf("[antipause] semua layer aktif");
}

} // extern "C"
