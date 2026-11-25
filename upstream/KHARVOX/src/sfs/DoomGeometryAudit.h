#pragma once
#include "ShaderIdentity.h"
#include <stdexcept>
#include <string>

namespace kharvox::sfs {
// Private diagnostic for one game vertex shader and its fingerprinted profile.
// Shares the 160-word storage ABI, but uses record schema 3. No output changes.
inline constexpr uint64_t doomGeometryAuditHash=0xf70ac337f3d62c58ULL;
inline constexpr uint64_t doomGeometryAuditProfileHash=0x8e62a9857d072b2fULL;
inline void makeDoomGeometryAudit(std::string& source,uint64_t inputHash){
    const auto main=source.find("void main()");
    if(main==std::string::npos||source.find("void main()",main+1)!=std::string::npos)
        throw std::runtime_error("Geometry audit requires one stereo entry point");
    source.replace(main,11,"void khGeometryOriginalMain()");
    source+=R"(
struct KhGeometryRecord { uint words[160]; };
layout(set=0,binding=32,std430) buffer KhGeometryAudit {
    uvec4 control;
    uvec4 operation;
    KhGeometryRecord records[8];
} khGeometry;
void khGeometryVector(uint n,uint offset,vec4 value) {
    uvec4 bits=floatBitsToUint(value);
    for(uint i=0u;i<4u;++i) khGeometry.records[n].words[offset+i]=bits[i];
}
void main() {
    khGeometryOriginalMain();
    if(khGeometry.control.x!=1u || gl_ViewIndex<0 || gl_ViewIndex>1) return;
    // Indexed draws need not reference vertex zero. Elect one actual invocation
    // per eye, irrespective of clipping, scissor, vertex base or instance base.
    if(atomicCompSwap(khGeometry.operation[2+gl_ViewIndex],0u,1u)!=0u) return;
    uint n=atomicAdd(khGeometry.control.y,1u);
    if(n>=8u) return;
    for(uint i=0u;i<160u;++i) khGeometry.records[n].words[i]=0u;
    khGeometry.records[n].words[0]=uint(gl_ViewIndex);
    khGeometry.records[n].words[1]=3u;
    khGeometry.records[n].words[2]=khGeometry.operation.x;
    khGeometry.records[n].words[3]=khGeometry.operation.y;
)";
    unsigned offset=4;
    for(auto field:{"mvpmatrixx","mvpmatrixy","mvpmatrixz","mvpmatrixw","modelmatrixx","modelmatrixy","modelmatrixz","mvpmatrixdeterminantsign"}){
        source+="khGeometryVector(n,"+std::to_string(offset)+"u,freqHigh_vertexUniforms."+field+");\n";offset+=4;
    }
    for(auto field:{"vertexstscalebias","vertexxyzscale","vertexxyzbias","globalvieworigin"}){
        source+="khGeometryVector(n,"+std::to_string(offset)+"u,freqLow_vertexUniforms."+field+");\n";offset+=4;
    }
    source+=R"(
    for(uint i=0u;i<4u;++i) khGeometryVector(n,52u+i*4u,khSfsProjection.clipFromCenter[gl_ViewIndex][i]);
    khGeometryVector(n,68u,khSfsProjection.eyeTranslation[gl_ViewIndex]);
    khGeometryVector(n,72u,in_Position);
    khGeometryVector(n,76u,gl_Position);
    khGeometry.records[n].words[80]=uint(gl_VertexIndex);
    khGeometry.records[n].words[81]=uint(gl_InstanceIndex);
)";
    source+="khGeometry.records[n].words[82]="+std::to_string(uint32_t(inputHash))+"u;\n";
    source+="khGeometry.records[n].words[83]="+std::to_string(uint32_t(inputHash>>32))+"u;\n}\n";
}
}
