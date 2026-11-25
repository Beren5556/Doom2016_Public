#pragma once
#include <string>
#include <stdexcept>
#include <cctype>

namespace kharvox::sfs {
// Private replay variant of the identified fragment TAA family. The original
// pass still owns its color + inverse-blur-speed/linear-depth outputs. Its second
// attachment is NOT a motion-vector texture. Keep the corrected computeVelocity
// function, but never run its temporal color resolve in this guide variant.
inline void makeDoomTemporalGuides(std::string& source,unsigned temporalCorrections,bool color=false) {
    const char* anchors[]{
        "vec3 computeVelocity(inout vec2 winPos)",
        "vec2 screenPosToTexcoord(vec2 pos, vec4 bias_scale)",
        "freqHigh_fragmentUniforms.renderpositiontoviewtexture",
        "freqHigh_fragmentUniforms.texsize",
        "layout(location = 0) out vec4 out_FragColor0;",
        "layout(location = 1) out vec4 out_FragColor1;",
        "uniform sampler2DArray samp_tex4;",
        "uniform sampler2DArray samp_tex2;",
        "khSfsPreviousUv(winPosPrev, rcpHW)",
        "vec2 objV = tex2Dlod(samp_tex2, param_8).xy;"
    };
    if(temporalCorrections!=1)throw std::runtime_error("Temporal guides require one corrected eye-local reprojection");
    for(auto anchor:anchors)if(source.find(anchor)==std::string::npos)
        throw std::runtime_error(std::string("Unrecognized temporal guide contract: ")+anchor);
    const auto main=source.find("void main()");
    if(main==std::string::npos||source.find("void main()",main+1)!=std::string::npos)
        throw std::runtime_error("Temporal guides require a single fragment entry point");
    auto start=source.find('{',main);int level=1;auto end=start+1;
    if(start==std::string::npos)throw std::runtime_error("Malformed temporal main");
    for(;end<source.size()&&level;++end){if(source[end]=='{')++level;else if(source[end]=='}')--level;}
    if(level)throw std::runtime_error("Unbalanced temporal main");
    // Native input dimensions and equal current/previous viewport scale are a
    // caller prerequisite. Movement is previous-minus-current in input pixels.
    // The depth follows the same foreground sample selected by computeVelocity.
    source.replace(main,end-main,R"(
void main() {
    vec2 uv = screenPosToTexcoord(gl_FragCoord.xy, freqHigh_fragmentUniforms.renderpositiontoviewtexture);
    float deviceDepth;
    vec3 motion = computeVelocity(uv, deviceDepth);
    out_FragColor0 = vec4(-motion.xy * freqHigh_fragmentUniforms.texsize.xy, 0.0, 1.0);
    out_FragColor1 = vec4(deviceDepth, 0.0, 0.0, 1.0);
}
)");
    const std::string signature="vec3 computeVelocity(inout vec2 winPos)";
    source.replace(source.find(signature),signature.size(),"vec3 computeVelocity(inout vec2 winPos, out float deviceDepth)");
    const auto selection=source.find("winPos += (offsetMinZ.xy * freqHigh_fragmentUniforms.texsize.zw);");
    if(selection==std::string::npos)throw std::runtime_error("Unknown foreground depth selection");
    // Export the exact gather-selected value. Resampling at the dilated UV
    // could blend different depths with the game's sampler.
    source.insert(selection,"deviceDepth = offsetMinZ.z;\n    ");
    if(color){
        if(source.find("layout(set = 0, binding = 4) uniform sampler2DArray samp_tex0;")==std::string::npos)
            throw std::runtime_error("Unrecognized pre-TAA HDR descriptor");
        const auto entry=source.find("void main()");
        source.insert(entry,"layout(location = 2) out vec4 khTemporalColor;\n");
        const auto motion=source.find("    float deviceDepth;",entry);
        // texelFetch copies the exact current pixel, before computeVelocity
        // dilates the guide UV toward the nearest foreground surface.
        source.insert(motion,"    khTemporalColor = vec4(texelFetch(samp_tex0, ivec3(ivec2(gl_FragCoord.xy), int(gl_ViewIndex)), 0).rgb, 1.0);\n");
    }
}
}
