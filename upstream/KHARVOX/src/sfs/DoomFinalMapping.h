#pragma once
#include <vulkan/vulkan.h>
#include "DoomTemporalParameters.h"
#include <array>
#include <cmath>

namespace kharvox::sfs {
// Final shader positions address the active scene rectangle in a potentially
// larger allocation. A full-resolution SR result is consumed at gl_FragCoord;
// admission proves that its coordinates match that exact destination.
inline bool doomFinalMapping(const std::array<float,12>& high,const std::array<float,12>& low,
    VkExtent2D allocation,VkExtent2D active,VkExtent2D output,const VkViewport& viewport,const VkRect2D& scissor){
    for(float x:high)if(!std::isfinite(x))return false;
    for(float x:low)if(!std::isfinite(x))return false;
    if(!active.width||!active.height||active.width>output.width||active.height>output.height||
       active.width>allocation.width||active.height>allocation.height||
       viewport.x||viewport.y||viewport.width!=float(output.width)||viewport.height!=float(output.height)||viewport.minDepth||viewport.maxDepth!=1||
       scissor.offset.x||scissor.offset.y||scissor.extent.width!=output.width||scissor.extent.height!=output.height)return false;
    const double sx=low[0],sy=low[1];
    if(sx<=0||sy<=0||sx>1||sy>1||!DoomTemporalParameters::rasterAxis(active.width,allocation.width,float(sx))||!DoomTemporalParameters::rasterAxis(active.height,allocation.height,float(sy)))return false;
    // Unlike the TAA UBO, final resolutionscale.zw are NOT inverse scale.
    // The original shader uses .w as the bicubic/sharp sample blend (observed
    // value 3), and does not use .z. Modes 0/1/2 share this scene mapping;
    // mode 2 additionally masks letterbox bands before the independent HUD.
    if(low[4]!=0.f&&low[4]!=1.f&&low[4]!=2.f)return false;
    auto close=[](double a,double b){return std::abs(a-b)<=2e-6;};
    return high[0]==float(allocation.width)&&high[1]==float(allocation.height)&&
        close(high[2]*output.width,sx)&&close(high[3]*output.height,sy);
}
}
