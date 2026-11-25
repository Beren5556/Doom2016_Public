#pragma once
#include <windows.h>
#include <vulkan/vulkan.h>

namespace kharvox {
// Pre-instance layer entry points can use the loader's four-argument chain ABI
// (RenderDoc does). Only the public loader export has the Vulkan three-argument
// ABI and constructs the enumeration chain, including layer extension filters.
inline PFN_vkEnumerateInstanceExtensionProperties globalInstanceExtensions() {
    auto loader = GetModuleHandleW(L"vulkan-1.dll");
    return loader ? reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
        GetProcAddress(loader, "vkEnumerateInstanceExtensionProperties")) : nullptr;
}
}
