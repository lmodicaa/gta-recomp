//
// Created by REA Reconstruction for reGTA on 16/10/2026.
//

#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <link.h>
#include <android/log.h>
#include <shadowhook.h>

extern void Log(const char *fmt, ...);

namespace Patch
{
    struct DlIterateContext
    {
        const char* libName;
        uintptr_t baseAddress;
    };

    inline int DlIterateCallback(struct dl_phdr_info* info, size_t size, void* data)
    {
        auto* ctx = reinterpret_cast<DlIterateContext*>(data);
        if (info && info->dlpi_name && ctx && ctx->libName)
        {
            if (strstr(info->dlpi_name, ctx->libName))
            {
                ctx->baseAddress = info->dlpi_addr;
                return 1;
            }
        }
        return 0;
    }

    inline uintptr_t FindLib(const char* libName)
    {
        if (!libName || !libName[0]) return 0;

        // Primary: read /proc/self/maps for exact first segment base address
        FILE* maps = fopen("/proc/self/maps", "rt");
        if (maps)
        {
            char line[512];
            while (fgets(line, sizeof(line), maps))
            {
                if (strstr(line, libName))
                {
                    uintptr_t addr = (uintptr_t)strtoull(line, nullptr, 16);
                    fclose(maps);
                    return addr;
                }
            }
            fclose(maps);
        }

        // Fallback: dl_iterate_phdr
        DlIterateContext ctx{ libName, 0 };
        dl_iterate_phdr(DlIterateCallback, &ctx);
        if (ctx.baseAddress != 0)
        {
            return ctx.baseAddress;
        }

        // Fallback: dlopen handle query via dladdr if loaded
        void* handle = dlopen(libName, RTLD_NOLOAD);
        if (handle)
        {
            void* sym = dlsym(handle, "JNI_OnLoad");
            if (!sym) sym = dlsym(handle, "_init");
            if (sym)
            {
                Dl_info info;
                if (dladdr(sym, &info) && info.dli_fbase)
                {
                    dlclose(handle);
                    return reinterpret_cast<uintptr_t>(info.dli_fbase);
                }
            }
            dlclose(handle);
        }

        return 0;
    }

    inline void InitHookStuff()
    {
        int err = shadowhook_init(SHADOWHOOK_MODE_SHARED, false);
        if (err == SHADOWHOOK_ERRNO_OK || err == SHADOWHOOK_ERRNO_PENDING)
        {
            __android_log_print(ANDROID_LOG_INFO, "reGTA", "shadowhook_init initialized successfully (v%s)",
                                shadowhook_get_version());
        }
        else
        {
            __android_log_print(ANDROID_LOG_INFO, "reGTA", "shadowhook_init status (%d: %s)",
                                err, shadowhook_to_errmsg(err));
        }
    }

    template<typename T>
    inline void* Redirect(const char* symbol, T target, void** orig = nullptr)
    {
        if (!symbol) return nullptr;

        void* stub = shadowhook_hook_sym_name("libGTASA.so", symbol, reinterpret_cast<void*>(target), orig);
        if (!stub)
        {
            stub = shadowhook_hook_sym_name("libreGTA.so", symbol, reinterpret_cast<void*>(target), orig);
        }

        if (!stub)
        {
            int err = shadowhook_get_errno();
            __android_log_print(ANDROID_LOG_WARN, "reGTA", "Failed to hook symbol %s (errno %d: %s)",
                                symbol, err, shadowhook_to_errmsg(err));
        }
        return stub;
    }

    template<typename T>
    inline void* Redirect(const char* libName, const char* symbol, T target, void** orig = nullptr)
    {
        if (!symbol) return nullptr;

        void* stub = shadowhook_hook_sym_name(libName ? libName : "libGTASA.so", symbol, reinterpret_cast<void*>(target), orig);
        if (!stub)
        {
            int err = shadowhook_get_errno();
            __android_log_print(ANDROID_LOG_WARN, "reGTA", "Failed to hook symbol %s in %s (errno %d: %s)",
                                symbol, libName ? libName : "libGTASA.so", err, shadowhook_to_errmsg(err));
        }
        return stub;
    }

    template<typename T>
    inline void* Redirect(uintptr_t addr, T target, void** orig = nullptr)
    {
        if (!addr) return nullptr;

        void* stub = shadowhook_hook_func_addr(reinterpret_cast<void*>(addr), reinterpret_cast<void*>(target), orig);
        if (!stub)
        {
            int err = shadowhook_get_errno();
            __android_log_print(ANDROID_LOG_WARN, "reGTA", "Failed to hook address 0x%lx (errno %d: %s)",
                                (unsigned long)addr, err, shadowhook_to_errmsg(err));
        }
        return stub;
    }

    inline bool Unprotect(uintptr_t addr, size_t size)
    {
        uintptr_t pageSize = (uintptr_t)sysconf(_SC_PAGESIZE);
        uintptr_t start = addr & ~(pageSize - 1);
        uintptr_t end = (addr + size + pageSize - 1) & ~(pageSize - 1);
        return mprotect(reinterpret_cast<void*>(start), end - start, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
    }

    inline bool WriteMemory(uintptr_t addr, const void* data, size_t size)
    {
        if (!addr || !data || size == 0) return false;
        if (!Unprotect(addr, size)) return false;
        memcpy(reinterpret_cast<void*>(addr), data, size);
        __builtin___clear_cache(reinterpret_cast<char*>(addr), reinterpret_cast<char*>(addr + size));
        return true;
    }

    inline bool ReadMemory(uintptr_t addr, void* dest, size_t size)
    {
        if (!addr || !dest || size == 0) return false;
        memcpy(dest, reinterpret_cast<const void*>(addr), size);
        return true;
    }

    template<typename T>
    inline bool Write(uintptr_t addr, T value)
    {
        return WriteMemory(addr, &value, sizeof(T));
    }

    template<typename T>
    inline T Read(uintptr_t addr)
    {
        T value{};
        ReadMemory(addr, &value, sizeof(T));
        return value;
    }

    inline bool NOP(uintptr_t addr, size_t count = 1)
    {
#if defined(__arm__)
        const uint16_t nopThumb = 0xBF00;
        for (size_t i = 0; i < count; ++i)
        {
            if (!WriteMemory(addr + i * sizeof(uint16_t), &nopThumb, sizeof(uint16_t))) return false;
        }
        return true;
#elif defined(__aarch64__)
        const uint32_t nopArm64 = 0xD503201F;
        for (size_t i = 0; i < count; ++i)
        {
            if (!WriteMemory(addr + i * sizeof(uint32_t), &nopArm64, sizeof(uint32_t))) return false;
        }
        return true;
#else
        const uint8_t nopX86 = 0x90;
        for (size_t i = 0; i < count; ++i)
        {
            if (!WriteMemory(addr + i, &nopX86, 1)) return false;
        }
        return true;
#endif
    }
}
