#pragma once
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
namespace kharvox {struct AerSourceObservation;}
namespace kharvox::dlss {struct Options;}
namespace kharvox::native {struct FramePose;struct StereoFrame;}
namespace kharvox::sfs {
struct DoomTemporalUniformSnapshot;
struct TemporalAuditDevice {bool enabled{};VkPhysicalDeviceLimits limits{};};
#ifdef KHARVOX_HAVE_SFS_COMPILER
bool sourceRingRequested();
bool configureSourceRing(VkDevice,PFN_vkGetDeviceProcAddr,const VkPhysicalDeviceMemoryProperties&,VkQueue,void(*)(),void(*)());
bool sourceRingActive(VkDevice);
bool sourceSwapchain(VkDevice,VkSwapchainKHR);
bool sourceOutOfDate(VkDevice,VkSwapchainKHR,bool);
VkResult createSourceSwapchain(VkDevice,const VkSwapchainCreateInfoKHR&,VkSwapchainKHR*);
VkResult sourceImages(VkDevice,VkSwapchainKHR,uint32_t*,VkImage*);
VkResult acquireSource(VkDevice,VkSwapchainKHR,uint64_t,VkSemaphore,VkFence,uint32_t*);
VkResult presentSource(VkDevice,VkQueue,const VkPresentInfoKHR&,bool);
void destroySourceSwapchain(VkDevice,VkSwapchainKHR);
VkImageLayout sourceLayout(VkDevice,VkImage,VkImageLayout);
bool nativeProbeEnabled();
bool temporalAuditRequested();
bool geometryAuditRequested();
// Read-only latest observation, paired with its source frame. This is not a
// temporal certificate and may be unavailable when engine memory is unmapped.
bool temporalUniformSnapshot(VkDevice,uint64_t&,DoomTemporalUniformSnapshot&);
bool configureDlss(VkDevice,const kharvox::dlss::Options&);
uint64_t dlssEvaluations(VkDevice);
bool outputRenderedAt(VkDevice,VkExtent2D,uint64_t frame);
// Token captured while holding the common queue lock, before the XR copy is
// submitted. Complete it only after that copy's successful completion wait.
uint64_t dlssCopyToken(VkDevice,VkQueue);
void dlssCopyCompleted(VkDevice,uint64_t);
bool dlssValidateSubmission(VkDevice,VkQueue,uint32_t,const VkSubmitInfo*);
bool dlssValidateSubmission2(VkDevice,VkQueue,uint32_t,const VkSubmitInfo2*);
void dlssSubmitted(VkDevice,VkQueue,uint32_t,const VkSubmitInfo*,VkResult);
void dlssSubmitted2(VkDevice,VkQueue,uint32_t,const VkSubmitInfo2*,VkResult);
bool initialize(VkDevice,VkPhysicalDevice,PFN_vkGetDeviceProcAddr,const VkPhysicalDeviceMemoryProperties&,const TemporalAuditDevice& = {});
void shutdown(VkDevice);
PFN_vkVoidFunction wrapProc(VkDevice d,const char*,PFN_vkVoidFunction);
void swapchainImages(VkDevice,VkSwapchainKHR,uint32_t,const VkImage*);
void swapchainDestroyed(VkDevice,VkSwapchainKHR);
bool vrEnabled();
void prepare(VkDevice,const kharvox::native::FramePose&,const XrFovf&);
void copyCompleted(VkDevice);
void beginFrame(VkDevice,VkSwapchainKHR=VK_NULL_HANDLE,uint32_t imageIndex=0);
bool pair(VkDevice,VkImage,VkExtent2D,VkFormat,kharvox::native::StereoFrame&,const AerSourceObservation* =nullptr);
bool eyeAttachmentView(VkDevice,VkImageView,uint32_t,VkImageView&);
#else
inline bool sourceRingRequested(){return false;}
inline bool configureSourceRing(VkDevice,PFN_vkGetDeviceProcAddr,const VkPhysicalDeviceMemoryProperties&,VkQueue,void(*)(),void(*)()){return false;}
inline bool sourceRingActive(VkDevice){return false;}
inline bool sourceSwapchain(VkDevice,VkSwapchainKHR){return false;}
inline bool sourceOutOfDate(VkDevice,VkSwapchainKHR,bool){return false;}
inline VkResult createSourceSwapchain(VkDevice,const VkSwapchainCreateInfoKHR&,VkSwapchainKHR*){return VK_ERROR_FEATURE_NOT_PRESENT;}
inline VkResult sourceImages(VkDevice,VkSwapchainKHR,uint32_t*,VkImage*){return VK_ERROR_FEATURE_NOT_PRESENT;}
inline VkResult acquireSource(VkDevice,VkSwapchainKHR,uint64_t,VkSemaphore,VkFence,uint32_t*){return VK_ERROR_FEATURE_NOT_PRESENT;}
inline VkResult presentSource(VkDevice,VkQueue,const VkPresentInfoKHR&,bool){return VK_ERROR_FEATURE_NOT_PRESENT;}
inline void destroySourceSwapchain(VkDevice,VkSwapchainKHR){}
inline VkImageLayout sourceLayout(VkDevice,VkImage,VkImageLayout layout){return layout;}
inline bool nativeProbeEnabled(){return false;}
inline bool temporalAuditRequested(){return false;}
inline bool geometryAuditRequested(){return false;}
inline bool temporalUniformSnapshot(VkDevice,uint64_t&,DoomTemporalUniformSnapshot&){return false;}
inline bool configureDlss(VkDevice,const kharvox::dlss::Options&){return false;}
inline uint64_t dlssEvaluations(VkDevice){return 0;}
inline bool outputRenderedAt(VkDevice,VkExtent2D,uint64_t){return false;}
inline uint64_t dlssCopyToken(VkDevice,VkQueue){return 0;}
inline void dlssCopyCompleted(VkDevice,uint64_t){}
inline bool dlssValidateSubmission(VkDevice,VkQueue,uint32_t,const VkSubmitInfo*){return true;}
inline bool dlssValidateSubmission2(VkDevice,VkQueue,uint32_t,const VkSubmitInfo2*){return true;}
inline void dlssSubmitted(VkDevice,VkQueue,uint32_t,const VkSubmitInfo*,VkResult){}
inline void dlssSubmitted2(VkDevice,VkQueue,uint32_t,const VkSubmitInfo2*,VkResult){}
inline bool initialize(VkDevice,VkPhysicalDevice,PFN_vkGetDeviceProcAddr,const VkPhysicalDeviceMemoryProperties&,const TemporalAuditDevice& = {}){return true;}
inline void shutdown(VkDevice){}
inline PFN_vkVoidFunction wrapProc(VkDevice d,const char*,PFN_vkVoidFunction next){return next;}
inline void swapchainImages(VkDevice,VkSwapchainKHR,uint32_t,const VkImage*){}
inline void swapchainDestroyed(VkDevice,VkSwapchainKHR){}
inline bool vrEnabled(){return false;}
inline void prepare(VkDevice,const kharvox::native::FramePose&,const XrFovf&){}
inline void copyCompleted(VkDevice){}
inline void beginFrame(VkDevice,VkSwapchainKHR=VK_NULL_HANDLE,uint32_t=0){}
inline bool pair(VkDevice,VkImage,VkExtent2D,VkFormat,kharvox::native::StereoFrame&,const AerSourceObservation* =nullptr){return false;}
inline bool eyeAttachmentView(VkDevice,VkImageView,uint32_t,VkImageView&){return false;}
#endif
}
