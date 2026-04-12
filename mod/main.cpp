#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <android/log.h>

#include "IAMI.h"

#define LOG_TAG "libantipause"
#define LOGFILE "/storage/emulated/0/antipause_log.txt"
#define EXPORT  __attribute__((visibility("default")))

static void logff(const char* fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", buf);
    FILE* f = fopen(LOGFILE, "a");
    if (f) { fprintf(f, "%s\n", buf); fclose(f); }
}

extern "C" {

EXPORT void __GetModInfo(ModInfo* info) {
    info->id      = "antipause";
    info->name    = "Anti-Pause SA-MP";
    info->author  = "brruham-arch";
    info->version = "1.0.0";
    info->SDK     = 10;
}

EXPORT void OnModPreLoad(IAMI* aml) {
    (void)aml;
    logff("[antipause] OnModPreLoad OK");
}

EXPORT void OnModLoad(IAMI* aml) {
    (void)aml;
    logff("[antipause] OnModLoad OK");
}

} // extern "C"
