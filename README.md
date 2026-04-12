# libantipause.so

**4-Layer Anti-Pause mod untuk SA-MP Mobile (com.sampmobilerp.game)**

> AML mod — ARM 32-bit (armeabi-v7a)

---

## Cara Kerja (4 Layer)

| Layer | Metode | Target |
|-------|--------|--------|
| L1 | Inline patch `BX LR` | `SetAndroidPaused` @ `0x00269ae4` |
| L2 | DobbyHook intercept | `SetAndroidPaused` (fallback L1) |
| L3 | Native thread 30ms | `IsAndroidPaused`, `WasAndroidPaused`, `CTimer::m_UserPause` |
| L4 | Foreground Service JNI | Cegah MIUI SIGSTOP ke proses game |

## Offsets (libGTASA.so)

```
IsAndroidPaused      0x006855bc  D (data)
WasAndroidPaused     0x006d7048  B (bss)
CTimer::m_UserPause  0x0096b514  B (bss)
SetAndroidPaused(int) 0x00269ae4 T (code, THUMB)
```

## Install

1. Build via GitHub Actions atau manual
2. Copy `libantipause.so` ke:
   ```
   /storage/emulated/0/Android/data/com.sampmobilerp.game/mods/
   ```
3. Launch game — cek log di `/storage/emulated/0/antipause_log.txt`

## Build Manual (Termux)

```bash
cd ~/antipausesamp
$NDK/ndk-build \
  NDK_PROJECT_PATH=. \
  NDK_APPLICATION_MK=jni/Application.mk \
  APP_BUILD_SCRIPT=jni/Android.mk \
  NDK_LIBS_OUT=./libs \
  NDK_OUT=./obj
```

Output: `libs/armeabi-v7a/libantipause.so`
