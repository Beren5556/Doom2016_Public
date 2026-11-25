#pragma once
#include "IndependentSurface.h"

namespace kharvox {
// Foveation can be selected after device creation, including after a DLAA
// startup. Reserve its core feature for every own-Layer session. Never borrow
// supported features wholesale or modify the application's Features2 chain.
inline bool prepareDoomReconstructionFeatures(bool layerEnabled,
        const VkPhysicalDeviceFeatures& supported, VkDeviceCreateInfo& game,
        VkDeviceCreateInfo& runtime, VkPhysicalDeviceFeatures& owned) {
    if (!layerEnabled) return true;
    const auto gameCore = surfaceChain<VkPhysicalDeviceFeatures2>(game.pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    const auto runtimeCore = surfaceChain<VkPhysicalDeviceFeatures2>(runtime.pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    if (!supported.shaderStorageImageExtendedFormats ||
        (gameCore && !gameCore->features.shaderStorageImageExtendedFormats) ||
        (runtimeCore && !runtimeCore->features.shaderStorageImageExtendedFormats)) return false;
    owned = game.pEnabledFeatures ? *game.pEnabledFeatures : VkPhysicalDeviceFeatures{};
    owned.shaderStorageImageExtendedFormats = VK_TRUE;
    if (!gameCore) game.pEnabledFeatures = &owned;
    if (!runtimeCore) runtime.pEnabledFeatures = &owned;
    return true;
}
inline bool doomStorageImageFormatsEnabled(const VkDeviceCreateInfo& device) {
    const auto core = surfaceChain<VkPhysicalDeviceFeatures2>(device.pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    return core ? bool(core->features.shaderStorageImageExtendedFormats) :
        device.pEnabledFeatures && device.pEnabledFeatures->shaderStorageImageExtendedFormats;
}
}
