// bbport: Vulkan driver loading. Nothing in the process links against libvulkan: this file is the
// only dlopen of a Vulkan library and everything else comes from the entry point it returns
// (vulkan-hpp's dispatcher, VMA, FFX, Dear ImGui, the smoke test).
//
//  - BB_VULKAN_DRIVER unset or empty: the platform's Vulkan loader, dlopen("libvulkan.so")
//    (libvulkan.so.1 on glibc). On Android that is the system loader and the phone's stock driver;
//    layers and ICD discovery are the loader's business. The entry point is vkGetInstanceProcAddr.
//  - BB_VULKAN_DRIVER=/abs/path/libvulkan_xxx.so: that driver (e.g. Mesa Turnip from an adrenotools
//    style zip) is dlopen'ed directly, RTLD_NOW | RTLD_LOCAL, with no loader in between: no layers,
//    and the driver answers instance-level calls itself. The entry point is the ICD's
//    vk_icdGetInstanceProcAddr (after vk_icdNegotiateLoaderICDInterfaceVersion, asking for 5), or
//    its vkGetInstanceProcAddr when it exports only that.
#include "vk_driver_loader.h"

#include <dlfcn.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace {

BbVkGetInstanceProcAddr Load() {
    const char* driver = std::getenv("BB_VULKAN_DRIVER");
    if (!driver || !*driver) {
#ifdef __ANDROID__
        static const char* const names[] = {"libvulkan.so"};
#else
        static const char* const names[] = {"libvulkan.so.1", "libvulkan.so"};
#endif
        for (const char* name : names) {
            if (void* lib = dlopen(name, RTLD_NOW | RTLD_LOCAL)) {
                if (void* gipa = dlsym(lib, "vkGetInstanceProcAddr")) {
                    std::fprintf(stderr, "Vulkan: system loader %s\n", name);
                    return reinterpret_cast<BbVkGetInstanceProcAddr>(gipa);
                }
            }
        }
        std::fprintf(stderr, "Vulkan: no system loader (%s)\n", dlerror());
        return nullptr;
    }
    void* lib = dlopen(driver, RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        std::fprintf(stderr, "Vulkan: cannot load driver %s: %s\n", driver, dlerror());
        return nullptr;
    }
    using NegotiateFn = int (*)(uint32_t*);
    if (auto negotiate = reinterpret_cast<NegotiateFn>(
            dlsym(lib, "vk_icdNegotiateLoaderICDInterfaceVersion"))) {
        uint32_t version = 5;
        if (negotiate(&version) != 0) {
            std::fprintf(stderr, "Vulkan: driver %s rejected ICD interface version 5\n", driver);
            return nullptr;
        }
    }
    const char* entry = "vk_icdGetInstanceProcAddr";
    void* gipa = dlsym(lib, entry);
    if (!gipa) {
        entry = "vkGetInstanceProcAddr";
        gipa = dlsym(lib, entry);
    }
    if (!gipa) {
        std::fprintf(stderr, "Vulkan: driver %s exports neither vk_icdGetInstanceProcAddr nor "
                             "vkGetInstanceProcAddr\n", driver);
        return nullptr;
    }
    std::fprintf(stderr, "Vulkan: direct driver %s via %s\n", driver, entry);
    return reinterpret_cast<BbVkGetInstanceProcAddr>(gipa);
}

} // namespace

extern "C" __attribute__((visibility("default"))) BbVkGetInstanceProcAddr
bb_vk_get_instance_proc_addr(void) {
    static std::once_flag once;
    static BbVkGetInstanceProcAddr gipa;
    std::call_once(once, [] { gipa = Load(); });
    return gipa;
}
