#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace kharvox::sfs {
// std430 ABI. Each descriptor set has its own slice; slice zero is disabled.
// Raw IEEE words preserve NaNs and signed zero without invalid JSON numbers.
struct TemporalAuditRecord {std::array<uint32_t,160> words{};};
struct TemporalAuditSlice {
    uint32_t active{},count{},frameLow{},frameHigh{};
    uint32_t operationLow{},operationHigh{},reserved0{},reserved1{};
    std::array<TemporalAuditRecord,8> records{};
};
static_assert(sizeof(TemporalAuditSlice)==5152,"Temporal audit GPU ABI v2");
inline void makeDoomTemporalAudit(std::string& source,unsigned corrections){
    const char* anchors[]{"vec3 computeVelocity(inout vec2 winPos)",
        "khSfsPreviousUv(winPosPrev, rcpHW)","uniform sampler2DArray samp_tex4;",
        "uniform sampler2DArray samp_tex2;","uniform sampler2DArray samp_tex0;",
        "uniform sampler2DArray samp_tex3;","uniform sampler2DArray samp_tex1;",
        "layout(location = 0) out vec4 out_FragColor0;","layout(location = 1) out vec4 out_FragColor1;"};
    if(corrections!=1)throw std::runtime_error("Audit requires identified corrected temporal fragment");
    for(auto a:anchors)if(source.find(a)==std::string::npos)throw std::runtime_error(std::string("Unknown audit contract: ")+a);
    const auto main=source.find("void main()");
    if(main==std::string::npos||source.find("void main()",main+1)!=std::string::npos)
        throw std::runtime_error("Audit requires one main entry point");
    // Keep the complete original resolve, including its two original outputs.
    source.replace(main,11,"void khAuditOriginalMain()");
    source+=R"(
struct KhAuditRecord { uint words[160]; };
layout(set=0,binding=32,std430) buffer KhTemporalAudit {
    uvec4 control;
    uvec4 operation;
    KhAuditRecord records[8];
} khAudit;
void khAuditVector(uint n, uint offset, vec4 value) {
    uvec4 bits=floatBitsToUint(value);
    for(uint i=0u;i<4u;++i) khAudit.records[n].words[offset+i]=bits[i];
}
void main() {
    khAuditOriginalMain();
    // Fullscreen resolve origin only. A scissor excluding it yields no sample,
    // never a fabricated record. Helpers must not reserve an atomic slot.
    if(gl_HelperInvocation || any(notEqual(ivec2(floor(gl_FragCoord.xy)),ivec2(0)))) return;
    if(khAudit.control.x!=1u) return;
    uint n=atomicAdd(khAudit.control.y,1u);
    if(n>=8u) return;
    for(uint i=0u;i<160u;++i) khAudit.records[n].words[i]=0u;
    khAudit.records[n].words[0]=uint(gl_ViewIndex);
    khAudit.records[n].words[1]=2u;
    khAudit.records[n].words[2]=khAudit.operation.x;
    khAudit.records[n].words[3]=khAudit.operation.y;
)";
    const char* high[]{"texsize","prevviewprojectionmatrixw","prevviewprojectionmatrixx","prevviewprojectionmatrixy",
        "renderpositiontoviewtexture","downsampletype","filterweights0","filterweights1","filterweights3",
        "filterweights4","filterweights2","filterweights5","prevtexsize","motionblurparms"};
    const char* low[]{"projectionmatrixz","frustumvectl","frustumvectr","resolutionscale","frustumvecbl","frustumvecbr","globalvieworigin"};
    uint32_t offset=4;
    for(auto field:high){source+="khAuditVector(n,"+std::to_string(offset)+"u,freqHigh_fragmentUniforms."+field+");\n";offset+=4;}
    for(auto field:low){source+="khAuditVector(n,"+std::to_string(offset)+"u,freqLow_fragmentUniforms."+field+");\n";offset+=4;}
    source+=R"(
    for(uint i=0u;i<4u;++i) khAuditVector(n,88u+i*4u,khSfsProjection.clipFromCenter[gl_ViewIndex][i]);
    khAuditVector(n,104u,khSfsProjection.eyeTranslation[gl_ViewIndex]);
    for(uint i=0u;i<4u;++i) khAuditVector(n,108u+i*4u,khSfsProjection.previousClipFromCenter[gl_ViewIndex][i]);
    khAuditVector(n,124u,khSfsProjection.previousEyeTranslation[gl_ViewIndex]);
)";
    offset=128;
    for(auto texture:{"samp_tex4","samp_tex2","samp_tex0","samp_tex3","samp_tex1"}){
        source+="{ivec3 size=textureSize("+std::string(texture)+",0); for(uint i=0u;i<3u;++i) khAudit.records[n].words["+std::to_string(offset)+"u+i]=uint(size[i]);}\n";offset+=4;
    }
    source+="khAuditVector(n,148u,gl_FragCoord);\n}\n";
}
}
