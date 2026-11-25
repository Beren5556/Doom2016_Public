#pragma once
#include "FrameProjection.h"
#include <optional>

namespace kharvox::sfs {
struct DoomRasterJitter {
    std::array<std::array<float,2>,2> pixels{};
    bool withinHalfPixel{};
};
// Coordinate conversion only; this does NOT establish that a live draw consumed
// these matrices. Pj/P0 are engine row-major matrices, clip is GLSL column-major.
// Contract: clipW=-cameraZ, original geometry shader negates clipY, then the
// parallel-eye SFS wrapper and Vulkan viewport mapping are applied.
// No clamping: the actual displacement must be preserved even when the eye FOV
// magnifies it beyond NGX's recommended half-pixel range (guide 3.7.3).
inline std::optional<DoomRasterJitter> doomRasterJitter(
    const std::array<float,16>& jittered,const std::array<float,16>& unjittered,
    const FrameUniforms& eyes,const std::array<float,6>& viewport){
    for(unsigned n=0;n<16;++n){
        if(!std::isfinite(jittered[n])||!std::isfinite(unjittered[n]))return {};
        if(n!=2&&n!=6&&jittered[n]!=unjittered[n])return {};
    }
    if(unjittered[12]!=0||unjittered[13]!=0||unjittered[14]!=-1||unjittered[15]!=0)return {};
    for(auto v:viewport)if(!std::isfinite(v))return {};
    if(viewport[2]<=0||viewport[3]==0)return {};
    const double dx=double(jittered[2])-unjittered[2],dy=double(jittered[6])-unjittered[6];
    DoomRasterJitter result;result.withinHalfPixel=true;
    for(unsigned e=0;e<2;++e){
        const auto& c=eyes.clip[e];const auto& t=eyes.translation[e];
        for(unsigned n=0;n<16;++n){
            if(!std::isfinite(c[n]))return {};
            if(n!=0&&n!=5&&n!=12&&n!=13&&c[n]!=(n==10||n==15?1.f:0.f))return {};
        }
        for(auto v:t)if(!std::isfinite(v))return {};
        if(c[0]<=0||c[5]<=0||t[2]!=0||t[3]!=0)return {};
        result.pixels[e]={float(-dx*c[0]*viewport[2]*.5),float(dy*c[5]*viewport[3]*.5)};
        for(auto v:result.pixels[e]){
            if(!std::isfinite(v))return {};
            result.withinHalfPixel&=std::abs(v)<=.5f;
        }
    }
    return result;
}
}
