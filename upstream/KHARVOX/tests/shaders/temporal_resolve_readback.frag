#version 450
#extension GL_EXT_multiview : require
// Independent readback of sampled-only game history/packed attachments.
layout(set=0,binding=0) uniform sampler2DArray color;
layout(set=0,binding=1) uniform sampler2DArray packed;
layout(location=0) out vec4 outColor;
layout(location=1) out vec4 outPacked;
void main() {
    ivec3 p=ivec3(ivec2(gl_FragCoord.xy),gl_ViewIndex);
    outColor=texelFetch(color,p,0);
    outPacked=texelFetch(packed,p,0);
}
