#pragma once
#include <string>
#include <stdexcept>
#include <regex>

namespace kharvox::sfs {
// Private, native-resolution DLAA consumer. The host owns a separate pipeline
// layout: original game/SFS descriptors in set 0 and two sampled 2D eye views
// in set 1, bindings 0/1. It must qualify matching extents, positive full-frame
// viewport at (0,0), frame/eye identity, layouts and producer completion before
// using this shader. No game path selects it merely by compiling it.
//
// Raster writes work with the observed game's COLOR_ATTACHMENT|SAMPLED history
// images; neither STORAGE nor TRANSFER_DST usage is required on those images.
// Preserve the corrected original blur/depth packing in output location 1.
// Location-to-attachment mapping remains a host obligation, not an inference
// from framebuffer order. SR/cropped/flipped viewport mapping is not supported.
inline void makeDoomTemporalResolve(std::string& source,unsigned temporalCorrections) {
    const char* anchors[]{
        "vec3 computeVelocity(inout vec2 winPos)",
        "vec2 screenPosToTexcoord(vec2 pos, vec4 bias_scale)",
        "vec4 OutputVelocityTarget(vec4 mbParms, inout vec2 v, float z)",
        "return vec4(v_orig, vec2(length(v), z));",
        "freqHigh_fragmentUniforms.renderpositiontoviewtexture",
        "freqHigh_fragmentUniforms.motionblurparms",
        "layout(location = 0) out vec4 out_FragColor0;",
        "layout(location = 1) out vec4 out_FragColor1;",
        "khSfsPreviousUv(winPosPrev, rcpHW)",
        "out_FragColor1.x = 1.0 / out_FragColor1.x;"
    };
    if(temporalCorrections!=1)
        throw std::runtime_error("Temporal resolve requires one corrected eye-local reprojection");
    for(auto anchor:anchors)if(source.find(anchor)==std::string::npos)
        throw std::runtime_error(std::string("Unrecognized temporal resolve contract: ")+anchor);
    const std::regex packedChannels(R"(out_FragColor1 = vec4\(([A-Za-z_][A-Za-z_0-9]*)\.z, \1\.w, out_FragColor1.z, out_FragColor1.w\);)");
    if(!std::regex_search(source,packedChannels))
        throw std::runtime_error("Unrecognized temporal packed output channels");
    // Private set 1 must not collide with an existing shader resource.
    if(std::regex_search(source,std::regex(R"(\bset\s*=\s*[1-9][0-9]*\b)")))
        throw std::runtime_error("Temporal resolve requires the original set-zero resource contract");
    const auto main=source.find("void main()");
    if(main==std::string::npos||source.find("void main()",main+1)!=std::string::npos)
        throw std::runtime_error("Temporal resolve requires a single fragment entry point");
    const auto start=source.find('{',main);
    if(start==std::string::npos)throw std::runtime_error("Malformed temporal resolve main");
    int level=1;auto end=start+1;
    for(;end<source.size()&&level;++end){if(source[end]=='{')++level;else if(source[end]=='}')--level;}
    if(level)throw std::runtime_error("Unbalanced temporal resolve main");
    // Keep computeVelocity and OutputVelocityTarget exactly as transformed for
    // SFS. They require the ORIGINAL motion convention and linear depth, not the
    // NGX pixel-motion/device-depth guides. Do not filter/blend DLSS with TAA.
    source.replace(main,end-main,R"(
layout(set = 1, binding = 0) uniform sampler2D khDlssLeft;
layout(set = 1, binding = 1) uniform sampler2D khDlssRight;
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec3 color;
    if (gl_ViewIndex == 0) color = texelFetch(khDlssLeft, pixel, 0).rgb;
    else color = texelFetch(khDlssRight, pixel, 0).rgb;
    out_FragColor0 = vec4(color, 1.0);
    vec2 uv = screenPosToTexcoord(gl_FragCoord.xy, freqHigh_fragmentUniforms.renderpositiontoviewtexture);
    vec3 velocity = computeVelocity(uv);
    vec2 motion = velocity.xy;
    vec4 packed = OutputVelocityTarget(freqHigh_fragmentUniforms.motionblurparms, motion, velocity.z);
    out_FragColor1 = vec4(1.0 / packed.z, packed.w, 0.0, 0.0);
}
)");
}
}
