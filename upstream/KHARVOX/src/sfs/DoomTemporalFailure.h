#pragma once
#include "DoomTemporalParameters.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace kharvox::sfs {
// Bounded CPU-side evidence from the exact bound uniforms and camera ring.
// Does not read back GPU images or relax any temporal admission requirement.
inline std::string temporalFailureJson(uint64_t frame,VkExtent2D active,VkExtent2D allocation,
    const DoomTemporalUniformSnapshot& uniforms,const FrameUniforms& eyes,
    const std::vector<doom_temporal::ViewSnapshot>& candidates){
    std::ostringstream out;out<<std::setprecision(9);
    const auto array=[&](const auto& values){out<<'[';bool first=true;for(auto v:values){if(!first)out<<',';first=false;if(std::isfinite(double(v)))out<<v;else out<<"null";}out<<']';};
    const auto now=doom_temporal::ViewSnapshots::now();
    out<<"{\"schema\":1,\"timeNs\":"<<now<<",\"frame\":"<<frame<<",\"active\":["<<active.width<<','<<active.height
       <<"],\"allocation\":["<<allocation.width<<','<<allocation.height<<"],\"high\":";array(uniforms.high);
    out<<",\"low\":";array(uniforms.low);out<<",\"eyeClip\":[";array(eyes.clip[0]);out<<',';array(eyes.clip[1]);
    out<<"],\"candidates\":[";
    for(size_t n=0;n<candidates.size()&&n<32;++n){const auto& v=candidates[n];const char* reason=nullptr;
        const bool matched=DoomTemporalParameters::currentMatches(v,uniforms,active,allocation,&reason);
        if(n)out<<',';out<<"{\"sequence\":"<<v.sequence<<",\"source\":"<<v.source<<",\"present\":"<<v.present
            <<",\"timeNs\":"<<v.timeNs<<",\"mode\":"<<unsigned(v.mode)<<",\"phase\":"<<unsigned(v.phase)
            <<",\"fresh\":"<<((v.timeNs<=now&&now-v.timeNs<=250000000)?"true":"false")
            <<",\"matched\":"<<(matched?"true":"false")<<",\"reason\":\""<<(reason?reason:"")<<"\",\"rectangle\":";array(v.rectangle);
        out<<",\"jittered\":";array(v.jittered);out<<",\"unjittered\":";array(v.unjittered);out<<",\"view\":";array(v.view);out<<'}';
    }
    out<<"]}";return out.str();
}
}
