#pragma once
#include <string>
#include <stdexcept>

namespace kharvox::sfs {
// Observed final scene-upsample pass (RenderDoc 2619). Unlike tonemap, this
// pass blends the separate GUI. Replacing its scene sample preserves GUI,
// gamma and color-accessibility processing after reconstruction.
inline bool isDoomFinalScene(const std::string& source){
    for(const char* anchor:{"vec4 positiontoviewtexture;","vec4 upsampleparms;","vec4 randomint;",
        "layout(set = 0, binding = 2) uniform sampler2DArray samp_viewcolormap;",
        "layout(set = 0, binding = 3) uniform sampler2DArray samp_tex0;",
        "vec3 SRGBlinear(vec3 sRGB)","vec4 guiColor = tex2Dlod(samp_tex0,",
        "float guiBlendScale =","out_FragColor0.w = 1.0;"})if(source.find(anchor)==source.npos)return false;
    return source.find("layout(location = 1) out")==source.npos&&source.find("set = 1")==source.npos;
}
inline void makeDoomFinalScene(std::string& source,bool copy){
    if(!isDoomFinalScene(source))throw std::runtime_error("Unrecognized DOOM final scene/GUI contract");
    const auto main=source.find("void main()");const auto start=source.find('{',main);
    if(main==source.npos||start==source.npos||source.find("void main()",main+1)!=source.npos)throw std::runtime_error("Final scene entry point");
    if(copy){
        int level=1;auto end=start+1;for(;end<source.size()&&level;++end){if(source[end]=='{')++level;else if(source[end]=='}')--level;}
        if(level)throw std::runtime_error("Final scene body");
        source.replace(main,end-main,R"(
void main(){
    out_FragColor0 = vec4(texelFetch(samp_viewcolormap, ivec3(ivec2(gl_FragCoord.xy), int(gl_ViewIndex)), 0).rgb, 1.0);
}
)");
    }else{
        const auto gui=source.find("vec4 guiColor = tex2Dlod(samp_tex0,",start);const auto end=source.find(';',gui);
        if(gui==source.npos||end==source.npos)throw std::runtime_error("Final GUI boundary");
        source.replace(start+1,end-start,R"(
    vec2 texcoord = gl_FragCoord.xy * freqHigh_fragmentUniforms.positiontoviewtexture.zw;
    vec3 khScene;
    if(gl_ViewIndex == 0) khScene = texelFetch(khFinalLeft, ivec2(gl_FragCoord.xy), 0).rgb;
    else khScene = texelFetch(khFinalRight, ivec2(gl_FragCoord.xy), 0).rgb;
    vec3 linearColor = SRGBlinear(clamp(khScene, vec3(0.0), vec3(1.0)));
    if(freqLow_fragmentUniforms.rendermode.x == 2.0 &&
       (texcoord.y < 0.05 * freqLow_fragmentUniforms.resolutionscale.y ||
        texcoord.y > 0.95 * freqLow_fragmentUniforms.resolutionscale.y))
        linearColor = vec3(0.0);
    vec4 guiColor = tex2Dlod(samp_tex0, vec4(texcoord / freqLow_fragmentUniforms.resolutionscale.xy, 0.0, 0.0));
)");
        source.insert(main,"layout(set = 1, binding = 0) uniform sampler2D khFinalLeft;\nlayout(set = 1, binding = 1) uniform sampler2D khFinalRight;\n");
    }
}
}
