#pragma once
#include <stdint.h>

class IAMI {
public:
    virtual ~IAMI() {}
    virtual uintptr_t GetLib(const char* lib) = 0;
    virtual void*     GetSym(uintptr_t lib, const char* sym) = 0;
    virtual bool      Hook(void* addr, void* hook, void** orig) = 0;
    virtual void      PlaceLong(uintptr_t addr, uintptr_t val) = 0;
    virtual void      PlaceNOP(uintptr_t addr, size_t count) = 0;
    virtual void      PlaceRET(uintptr_t addr) = 0;
};

struct ModInfo {
    const char* id;
    const char* name;
    const char* author;
    const char* version;
    int         SDK;
};
