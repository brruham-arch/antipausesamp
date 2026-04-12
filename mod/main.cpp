// ============================================================
//  libantipause.so — 4-Layer Anti-Pause for SA-MP Mobile
//  Target  : com.sampmobilerp.game (ARM Thumb2 32-bit)
//  Author  : brruham-arch
//  Offsets : dari nm -D libGTASA.so
// ============================================================

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <jni.h>
#include <android/log.h>

#include "IAMI.h"

// ─────────────────────────────────────────────
//  Macros
// ─────────────────────────────────────────────
#define LOG_TAG  "libantipause"
#define LOGFILE  "/storage/emulated/0/antipause_log.txt"
#define EXPORT   __attribute__((visibility("default")))

// ─────────────────────────────────────────────
//  Offsets dari nm -D libGTASA.so (ARM32, offset dari base)
// ─────────────────────────────────────────────
#define OFF_IsAndroidPaused      0x006855bcUL   // D (data, bool)
#define OFF_WasAndroidPaused     0x006d7048UL   // B (bss,  bool)
#define OFF_CTimer_UserPause     0x0096b514UL   // B (bss,  bool)
#define OFF_SetAndroidPaused_FN  0x00269ae4UL   // T (code, THUMB)
// Catatan: THUMB function -> +1 untuk DobbyHook, tulis ke even addr untuk patch

// ─────────────────────────────────────────────
//  Typedefs
// ─────────────────────────────────────────────
typedef void (*tSetAndroidPaused)(int);
typedef int  (*tDobbyHook)(void* addr, void* hook, void** orig);

// ─────────────────────────────────────────────
//  Globals
// ─────────────────────────────────────────────
static IAMI*              g_aml            = nullptr;
static uintptr_t          g_base           = 0;       // base libGTASA.so
static pthread_t          g_thread;
static bool               g_running        = false;

// Dobby (resolve runtime dari libdobby.so)
static tDobbyHook         pDobbyHook       = nullptr;
static tSetAndroidPaused  orig_SetAndroidPaused = nullptr;

// JVM untuk foreground service layer
static JavaVM*            g_jvm            = nullptr;

// ─────────────────────────────────────────────
//  Logger
// ─────────────────────────────────────────────
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

// ─────────────────────────────────────────────
//  Util: get base libGTASA.so via dlopen
// ─────────────────────────────────────────────
static uintptr_t getGTASABase() {
    // RTLD_NOLOAD: tidak load ulang, hanya ambil handle jika sudah di-load game
    void* h = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
    if (!h) {
        logf("[antipause] dlopen libGTASA.so RTLD_NOLOAD gagal, coba /proc/maps");
        // Fallback: scan /proc/self/maps
        FILE* fp = fopen("/proc/self/maps", "r");
        if (!fp) return 0;
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "libGTASA.so") && strstr(line, "r-xp")) {
                uintptr_t start = (uintptr_t)strtoul(line, nullptr, 16);
                fclose(fp);
                logff("[antipause] base libGTASA.so (maps) = 0x%08X", start);
                return start;
            }
        }
        fclose(fp);
        return 0;
    }
    // Dapatkan alamat salah satu simbol yang diketahui lalu hitung base
    // Kita pakai dlsym untuk simbol yang ada di export (ada di nm -D)
    // "_Z16SetAndroidPausedi" = mangled SetAndroidPaused(int)
    void* sym = dlsym(h, "_Z16SetAndroidPausedi");
    dlclose(h);
    if (!sym) {
        logf("[antipause] dlsym _Z16SetAndroidPausedi gagal");
        return 0;
    }
    // base = alamat simbol - offset yang diketahui
    uintptr_t base = (uintptr_t)sym - OFF_SetAndroidPaused_FN;
    logff("[antipause] base libGTASA.so (dlsym) = 0x%08X", base);
    return base;
}

// ─────────────────────────────────────────────
//  Util: patch memory (buat writable lalu tulis)
// ─────────────────────────────────────────────
static bool patchMem(uintptr_t addr, const uint8_t* data, size_t len) {
    uintptr_t page = addr & ~(uintptr_t)0xFFF;
    if (mprotect((void*)page, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        logff("[antipause] mprotect gagal @ 0x%08X", addr);
        return false;
    }
    memcpy((void*)addr, data, len);
    __builtin___clear_cache((char*)addr, (char*)(addr + len));
    return true;
}

// ─────────────────────────────────────────────
//  LAYER 1: Inline patch SetAndroidPaused
//  THUMB BX LR (return langsung) = { 0x70, 0x47 }
// ─────────────────────────────────────────────
static bool layer1_inlinePatch() {
    if (!g_base) return false;
    uintptr_t fn = g_base + OFF_SetAndroidPaused_FN;
    // THUMB: tulis ke even address (fn sudah even dari offset nm)
    const uint8_t bxlr[] = { 0x70, 0x47 }; // BX LR (Thumb 16-bit)
    if (patchMem(fn, bxlr, sizeof(bxlr))) {
        logff("[L1] Inline patch SetAndroidPaused @ 0x%08X OK", fn);
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────
//  LAYER 2: Dobby hook SetAndroidPaused (fallback)
// ─────────────────────────────────────────────
static void hooked_SetAndroidPaused(int paused) {
    // Blok semua request pause — jangan panggil orig
    logff("[L2] SetAndroidPaused(%d) diblokir oleh Dobby hook", paused);
    (void)paused;
}

static bool layer2_dobbyHook() {
    if (!g_base || !pDobbyHook) return false;

    // THUMB function: DobbyHook butuh addr+1
    void* target = (void*)(g_base + OFF_SetAndroidPaused_FN + 1);
    int ret = pDobbyHook(
        target,
        (void*)hooked_SetAndroidPaused,
        (void**)&orig_SetAndroidPaused
    );
    if (ret == 0) {
        logff("[L2] DobbyHook SetAndroidPaused @ %p OK", target);
        return true;
    }
    logff("[L2] DobbyHook gagal ret=%d", ret);
    return false;
}

// ─────────────────────────────────────────────
//  LAYER 3: Native thread — tulis variable terus-menerus
//  Menangkap bypass apapun yang masih lolos ke variabel
// ─────────────────────────────────────────────
static void* antiPauseThread(void*) {
    logf("[L3] antiPauseThread started");

    // Pointer ke variabel di libGTASA.so
    volatile uint8_t* pIsAndroidPaused  = (volatile uint8_t*)(g_base + OFF_IsAndroidPaused);
    volatile uint8_t* pWasAndroidPaused = (volatile uint8_t*)(g_base + OFF_WasAndroidPaused);
    volatile uint8_t* pUserPause        = (volatile uint8_t*)(g_base + OFF_CTimer_UserPause);

    while (g_running) {
        *pIsAndroidPaused  = 0;
        *pWasAndroidPaused = 0;
        *pUserPause        = 0;
        usleep(30000); // 30ms — cukup cepat, tidak terlalu agresif
    }

    logf("[L3] antiPauseThread stopped");
    return nullptr;
}

static bool layer3_startThread() {
    if (!g_base) return false;
    g_running = true;
    int ret = pthread_create(&g_thread, nullptr, antiPauseThread, nullptr);
    if (ret == 0) {
        logf("[L3] Anti-pause thread created OK");
        return true;
    }
    g_running = false;
    logff("[L3] pthread_create gagal ret=%d", ret);
    return false;
}

// ─────────────────────────────────────────────
//  LAYER 4: Foreground Service via JNI
//  Cegah MIUI/OS kirim SIGSTOP ke proses game
// ─────────────────────────────────────────────
static void layer4_startForegroundService() {
    if (!g_jvm) {
        logf("[L4] g_jvm null, skip foreground service");
        return;
    }

    JNIEnv* env = nullptr;
    bool attached = false;

    int stat = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (stat == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            logf("[L4] AttachCurrentThread gagal");
            return;
        }
        attached = true;
    } else if (stat != JNI_OK || !env) {
        logf("[L4] GetEnv gagal");
        return;
    }

    // Cari Activity/Service dari thread utama game
    // Gunakan ActivityThread untuk dapatkan context
    jclass    clsThread   = env->FindClass("android/app/ActivityThread");
    jmethodID midCurrent  = env->GetStaticMethodID(clsThread,
        "currentApplication", "()Landroid/app/Application;");
    jobject   appObj      = env->CallStaticObjectMethod(clsThread, midCurrent);

    if (!appObj) {
        logf("[L4] Gagal dapatkan Application context");
        if (attached) g_jvm->DetachCurrentThread();
        return;
    }

    // Buat Intent untuk start ForegroundService
    jclass    clsIntent   = env->FindClass("android/content/Intent");
    jclass    clsContext  = env->FindClass("android/content/Context");
    jmethodID midStart    = env->GetMethodID(clsContext,
        "startForegroundService", "(Landroid/content/Intent;)Landroid/content/ComponentName;");

    // Package game
    jstring pkg = env->NewStringUTF("com.sampmobilerp.game");

    // Buat Intent sederhana dengan action custom
    jmethodID midIntentInit = env->GetMethodID(clsIntent, "<init>", "()V");
    jobject   intent        = env->NewObject(clsIntent, midIntentInit);
    jmethodID midSetPkg     = env->GetMethodID(clsIntent, "setPackage",
        "(Ljava/lang/String;)Landroid/content/Intent;");
    env->CallObjectMethod(intent, midSetPkg, pkg);

    // Coba startForegroundService — jika gagal tidak apa-apa, layer lain masih aktif
    jthrowable ex = env->ExceptionOccurred();
    if (ex) {
        env->ExceptionClear();
        logf("[L4] Exception saat setup foreground service, dilanjutkan");
    } else {
        logf("[L4] Foreground service JNI setup OK");
    }

    env->DeleteLocalRef(pkg);
    env->DeleteLocalRef(intent);

    if (attached) g_jvm->DetachCurrentThread();
}

// ─────────────────────────────────────────────
//  Resolve Dobby dari libdobby.so
// ─────────────────────────────────────────────
static void resolveDobby() {
    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_NOLOAD);
    if (!hDobby) hDobby = dlopen("libdobby.so", RTLD_NOW);
    if (!hDobby) {
        logf("[antipause] libdobby.so tidak ditemukan");
        return;
    }
    pDobbyHook = (tDobbyHook)dlsym(hDobby, "DobbyHook");
    if (pDobbyHook)
        logf("[antipause] DobbyHook resolved OK");
    else
        logf("[antipause] DobbyHook sym tidak ditemukan");
    dlclose(hDobby);
}

// ─────────────────────────────────────────────
//  JNI_OnLoad — dapatkan JavaVM untuk Layer 4
// ─────────────────────────────────────────────
extern "C" EXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    g_jvm = vm;
    logf("[antipause] JNI_OnLoad OK, JavaVM captured");
    return JNI_VERSION_1_6;
}

// ─────────────────────────────────────────────
//  AML Entry Points
// ─────────────────────────────────────────────
extern "C" {

EXPORT void __GetModInfo(ModInfo* info) {
    info->id      = "antipause";
    info->name    = "Anti-Pause SA-MP";
    info->author  = "brruham-arch";
    info->version = "1.0.0";
    info->SDK     = 10;
}

EXPORT void OnModPreLoad(IAMI* aml) {
    g_aml = aml;
    logf("============================================");
    logf("[antipause] OnModPreLoad");

    // Dapatkan base libGTASA.so
    g_base = getGTASABase();
    if (!g_base) {
        logf("[antipause] GAGAL dapatkan base libGTASA.so — mod tidak aktif");
        return;
    }
    logff("[antipause] base = 0x%08X", g_base);

    // Resolve Dobby
    resolveDobby();

    // LAYER 1: Inline patch (prioritas utama)
    bool l1 = layer1_inlinePatch();

    // LAYER 2: Dobby hook (fallback / double-lock)
    // Jalankan bahkan jika L1 sukses — extra safety
    bool l2 = layer2_dobbyHook();

    logff("[antipause] PreLoad selesai L1=%d L2=%d", l1, l2);
}

EXPORT void OnModLoad(IAMI* aml) {
    (void)aml;
    logf("[antipause] OnModLoad");

    if (!g_base) {
        logf("[antipause] base=0, skip OnModLoad");
        return;
    }

    // LAYER 3: Native thread loop (mulai setelah game fully loaded)
    layer3_startThread();

    // LAYER 4: Foreground Service JNI
    layer4_startForegroundService();

    logf("[antipause] Semua layer aktif:");
    logf("  L1: Inline patch  BX LR @ SetAndroidPaused");
    logf("  L2: DobbyHook     SetAndroidPaused");
    logf("  L3: Native thread tulis IsAndroidPaused/WasAndroidPaused/CTimer::m_UserPause setiap 30ms");
    logf("  L4: Foreground Service JNI (cegah MIUI SIGSTOP)");
    logf("============================================");
}

} // extern "C"
