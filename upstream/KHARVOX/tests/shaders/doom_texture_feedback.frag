#version 450
layout(set=0,binding=5,std140) uniform High { vec4 physicalfilterparms; } freqHigh_fragmentUniforms;
layout(set=0,binding=6,std140) uniform Low { vec4 lod[8]; } inputValues;
layout(location=0) out vec4 color;
void main(){
    float anisoLOD = inputValues.lod[int(gl_FragCoord.x)].x;
    float feedbackBias = freqHigh_fragmentUniforms.physicalfilterparms.y;
    // Exact original DOOM expression, transformed by the production compiler.
    uint feedbackLOD = uint(min(freqHigh_fragmentUniforms.physicalfilterparms.w, max(0.0, anisoLOD) + feedbackBias));
    uint packed = (17u | (29u << 8u)) | (feedbackLOD << 19u) | (3u << 27u);
    color = vec4(float(feedbackLOD), float((packed >> 27u) & 31u), float(packed & 255u), float((packed >> 8u) & 255u));
}
