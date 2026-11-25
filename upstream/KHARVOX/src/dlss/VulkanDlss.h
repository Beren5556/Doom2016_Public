#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace kharvox::dlss {
enum class Mode {Off, Dlaa, Quality, Balanced, Performance, UltraPerformance, Neural};
enum class Status {Disabled, Pending, Ready, Busy, Rejected, Failed, Recorded};
struct Identity {uint64_t frame{},epoch{};uint32_t eye{};};
struct Image {
    VkDevice device{};VkImage image{};VkImageView view{};
    VkFormat format{};VkExtent2D extent{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VkImageLayout layout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkImageUsageFlags usage{};VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
    Identity identity{};
};
struct EyeInput {
    Image color,depth,motion;
    float jitterX{},jitterY{},mvScaleX{1},mvScaleY{1};
    float preExposure{1},exposureScale{1},frameTimeMs{11.111f};
};
struct PairInput {
    std::array<EyeInput,2> eyes;
    std::array<VkExtent2D,2> output;
    Mode mode{Mode::Off};
    bool hdr{true},depthInverted{},motionJittered{},reset{};
    // Feature 18 is a separate, LDR, native-size processor. Its Auto/A/B model
    // hint is independent of the SR/DLAA K hint. Host chains it before/after SR.
    unsigned neuralModel{};
    unsigned preset{11};
    struct Appearance {unsigned style{2};float intensity{1.2f},structure{1.27f},tone{1.34f},skin{-1};bool autoMask{};} appearance;
    // The adapter must qualify actual jitter, view/frame identity, motion units,
    // exposure and the pre-TAA insertion point. Unknown data stays disabled.
    bool temporalContractValidated{};
};
struct Output {
    std::array<Image,2> eyes;
    uint64_t ticket{};
};
struct Options {
    VkInstance instance{};VkPhysicalDevice physical{};VkDevice device{};
    VkQueue queue{};uint32_t queueFamily{};
    PFN_vkGetInstanceProcAddr gipa{};PFN_vkGetDeviceProcAddr gdpa{};
    // NGX discovery uses public loader calls internally. A layer adapter must
    // supply the matching public handles separately from its downstream ones.
    VkInstance ngxInstance{};VkPhysicalDevice ngxPhysical{};
    PFN_vkGetInstanceProcAddr ngxGipa{};
    std::filesystem::path runtimeDirectory,cacheDirectory;
    std::filesystem::path neuralLibrary;
    std::string projectId;
    // Pair must be externally synchronized, or supply the same lock used by
    // every other producer on this queue. Callbacks must not throw.
    void (*lockQueue)(){};void (*unlockQueue)(){};
    bool externallySynchronized{};
    bool diagnosticLogging{};
    bool storageImageExtendedFormatsEnabled{};
};
// Laboratory/adapter backend. Submit requires inputs already submitted on this
// queue and alive through Poll; its caller publishes only after Ready and gives
// Release the last-reader fence. Record instead uses caller-owned submission
// and retirement as documented below. No input pixels are overwritten. The host
// must resolve outstanding recorded tickets before destroying this provider.
// No game hook opts into this class merely because the library was built.
class VulkanDlss {
    struct State;std::unique_ptr<State> state_;
public:
    explicit VulkanDlss(Options);
    ~VulkanDlss();
    VulkanDlss(const VulkanDlss&)=delete;
    VulkanDlss& operator=(const VulkanDlss&)=delete;
    Status Submit(const PairInput&);
    // Record into the caller's primary GRAPHICS+COMPUTE command buffer, outside
    // a render pass. Producers may precede this operation in the same buffer;
    // consumers may follow it, with their own image access/layout barriers.
    // No begin/end, queue submission or GPU wait is issued here. Calls on this
    // provider and NGX must be serialized by the host. The queue family and
    // device must match Options. This method does not verify command-buffer state.
    // Successful outputs are transitioned to SHADER_READ_ONLY_OPTIMAL for
    // sampling and retained in that tracked layout for the next evaluation.
    // Output is a GPU dependency, NOT CPU-ready data. Every nonzero ticket must
    // be completed by CommitRecorded or CancelRecorded, including Failed work.
    Status Record(const PairInput&,VkCommandBuffer,Output&);
    // Call only after submitting all recorded work. The borrowed fence must
    // cover the LAST output reader and remain valid/signaled until the next
    // Record/Submit observes completion. Failed recordings have no usable output.
    bool CommitRecorded(uint64_t ticket,VkFence lastConsumer);
    // Alternative for hosts with an existing device-idle frame boundary (SFS).
    // Call ONLY after every referencing command buffer has been submitted and
    // a successful vkDeviceWaitIdle has retired its last consumer. Do not use
    // for unsubmitted buffers: discard those and CancelRecorded instead.
    // Performs no additional queue submission or GPU wait.
    bool RetireRecordedAfterDeviceIdle(uint64_t ticket);
    // Equivalent completion proof: a successfully waited fence on the same
    // queue, submitted after this work and its LAST private-output reader.
    bool RetireRecordedAfterCompletion(uint64_t ticket);
    // Call only after discarding/resetting every referencing command buffer,
    // with a guarantee it was never submitted. Clears both temporal histories.
    bool CancelRecorded(uint64_t ticket);
    // Drop inactive feature histories/output images only after all consumers
    // have retired. Keeps the shared NGX device initialized and counters intact.
    bool ReleaseResources();
    Status Poll(Output&);
    bool Release(VkFence lastConsumer);
    const std::string& Error() const;
    uint64_t Evaluations() const;
    uint64_t ResetEvaluations() const;
};
}
