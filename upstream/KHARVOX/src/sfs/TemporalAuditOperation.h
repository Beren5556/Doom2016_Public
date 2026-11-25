#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace kharvox::sfs {
// Values copied at command recording. GPU frame identity comes from the slice
// at execution, not from recordedFrame: executable command buffers may repeat.
struct TemporalAuditOperation {
    uint64_t recordedFrame{},command{},generation{},draw{},pipeline{},layout{},set{};
    uint64_t renderPass{},framebuffer{},vertexHash{},fragmentHash{};
    uint32_t subpass{};
    const char* kind{"unknown"};
    bool viewportKnown{},scissorKnown{},dynamicViewport{},dynamicScissor{};
    VkViewport viewport{};VkRect2D scissor{},renderArea{};
    std::array<uint32_t,2> dynamicBindings{};
    std::vector<uint32_t> dynamicOffsets;
    std::string attachments{"[]"};
    std::string json()const {
        std::ostringstream out;out<<std::setprecision(std::numeric_limits<float>::max_digits10);
        out<<"{\"recordedFrame\":\""<<recordedFrame<<"\",\"command\":\""<<command
            <<"\",\"generation\":\""<<generation<<"\",\"draw\":\""<<draw
            <<"\",\"pipeline\":\""<<pipeline<<"\",\"layout\":\""<<layout
            <<"\",\"set\":\""<<set<<"\",\"renderPass\":\""<<renderPass
            <<"\",\"framebuffer\":\""<<framebuffer<<"\",\"vertexHash\":\""<<vertexHash
            <<"\",\"fragmentHash\":\""<<fragmentHash<<"\",\"subpass\":"<<subpass
            <<",\"kind\":\""<<kind<<"\",\"dynamicBindings\":["<<dynamicBindings[0]<<','<<dynamicBindings[1]
            <<"],\"dynamicOffsets\":[";
        for(size_t i=0;i<dynamicOffsets.size();++i)out<<(i?",":"")<<dynamicOffsets[i];
        out<<"],\"viewportSource\":\""<<(dynamicViewport?"dynamic":"static")<<"\",\"viewport\":";
        const bool finite=std::isfinite(viewport.x)&&std::isfinite(viewport.y)&&std::isfinite(viewport.width)&&std::isfinite(viewport.height)&&std::isfinite(viewport.minDepth)&&std::isfinite(viewport.maxDepth);
        if(viewportKnown&&finite)out<<'['<<viewport.x<<','<<viewport.y<<','<<viewport.width<<','<<viewport.height<<','<<viewport.minDepth<<','<<viewport.maxDepth<<']';else out<<"null";
        out<<",\"scissorSource\":\""<<(dynamicScissor?"dynamic":"static")<<"\",\"scissor\":";
        if(scissorKnown)out<<'['<<scissor.offset.x<<','<<scissor.offset.y<<','<<scissor.extent.width<<','<<scissor.extent.height<<']';else out<<"null";
        out<<",\"renderArea\":["<<renderArea.offset.x<<','<<renderArea.offset.y<<','<<renderArea.extent.width<<','<<renderArea.extent.height
            <<"],\"attachments\":"<<attachments<<'}';return out.str();
    }
};
}
