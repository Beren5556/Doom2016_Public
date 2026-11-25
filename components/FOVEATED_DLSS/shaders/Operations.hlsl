cbuffer Parameters : register(b0) {
    float4 sourceRect;
    float4 sizes;
    float4 crop;
    float4 centerHalf;
    float4 options;
    float4 componentScale;
    float4 commonX;
    float4 commonY;
    float4 commonW;
    float4 radii;
};
Texture2D<float4> source0 : register(t0);
Texture2D<float4> source1 : register(t1);
SamplerState linearClamp : register(s0);
float4 VS(uint vertex : SV_VertexID) : SV_Position {
    return float4(vertex == 1 ? 3 : -1, vertex == 2 ? 3 : -1, 0, 1);
}
float4 Resample(float4 position : SV_Position) : SV_Target {
    float2 p = sourceRect.xy + position.xy / sizes.zw * sourceRect.zw;
    p = clamp(p, sourceRect.xy + 0.5, sourceRect.xy + sourceRect.zw - 0.5);
    float4 value = options.x > 0.5 ? source0.Load(int3(int2(p), 0)) : source0.SampleLevel(linearClamp, p / sizes.xy, 0);
    value.xy *= componentScale.xy;
    return value;
}
float4 GuideCrop(float4 position : SV_Position) : SV_Target {
    // Reproduce both nearest coordinate mappings, including their clamping.
    float2 q = sourceRect.xy + position.xy / sourceRect.zw * sourceRect.zw;
    q = clamp(q, sourceRect.xy + 0.5, sourceRect.xy + sourceRect.zw - 0.5);
    float2 p = (float2(int2(q)) + 0.5) / sizes.zw * sizes.xy;
    p = clamp(p, float2(0.5,0.5), sizes.xy - 0.5);
    float4 value = source0.Load(int3(int2(p),0));
    value.xy *= componentScale.xy;
    return value;
}
float DistanceToRegion(float2 p) {
    if (commonX.w > 0.5) {
        const float3 v = float3(p, 1);
        const float2 d = float2(dot(commonX.xyz,v),dot(commonY.xyz,v)) / dot(commonW.xyz,v) - 0.5;
        const float2 radius = float2(d.x < 0 ? radii.w : radii.z, d.y < 0 ? radii.x : radii.y);
        const float2 q = abs(d) / radius;
        return (options.x > 0.5 ? length(q) : max(q.x,q.y)) - 1;
    }
    float2 q = abs(p - centerHalf.xy);
    float distance = options.x > 0.5 ? (length(q / centerHalf.zw) - 1) * min(centerHalf.z, centerHalf.w)
                                    : max(q.x - centerHalf.z, q.y - centerHalf.w);
    return distance;
}
float3 Decode(float3 c) {
    return float3(c.x <= 0.04045 ? c.x / 12.92 : pow(max((c.x + 0.055) / 1.055, 0), 2.4),
                  c.y <= 0.04045 ? c.y / 12.92 : pow(max((c.y + 0.055) / 1.055, 0), 2.4),
                  c.z <= 0.04045 ? c.z / 12.92 : pow(max((c.z + 0.055) / 1.055, 0), 2.4));
}
float3 Encode(float3 c) {
    return float3(c.x <= 0.0031308 ? c.x * 12.92 : 1.055 * pow(max(c.x, 0), 1.0 / 2.4) - 0.055,
                  c.y <= 0.0031308 ? c.y * 12.92 : 1.055 * pow(max(c.y, 0), 1.0 / 2.4) - 0.055,
                  c.z <= 0.0031308 ? c.z * 12.92 : 1.055 * pow(max(c.z, 0), 1.0 / 2.4) - 0.055);
}
float4 Composite(float4 position : SV_Position) : SV_Target {
    const float2 uv = position.xy / sizes.zw;
    float4 base = source0.SampleLevel(linearClamp, uv, 0);
    const float2 regionPosition = position.xy * componentScale.xy;
    const float distance = DistanceToRegion(regionPosition);
    float t = options.y > 0 ? saturate(distance / options.y) : (distance > 0 ? 1 : 0);
    const float weight = 1 - t * t * (3 - 2 * t);
    if (weight <= 0) return base;
    float4 detail = source1.SampleLevel(linearClamp, regionPosition * crop.xy + crop.zw, 0);
    if (weight >= 1) return detail;
    float3 rgb = options.z > 0.5 ? Encode(lerp(Decode(base.rgb), Decode(detail.rgb), weight)) : lerp(base.rgb, detail.rgb, weight);
    return float4(rgb, lerp(base.a, detail.a, weight));
}
float4 BlendRegion(float4 position : SV_Position) : SV_Target {
    const float distance = DistanceToRegion(position.xy);
    const float t = options.y > 0 ? saturate(distance / options.y) : (distance > 0 ? 1 : 0);
    const float weight = (1 - t * t * (3 - 2 * t)) * options.w;
    clip(weight - 0.00001);
    const float3 detail = source0.SampleLevel(linearClamp, position.xy * crop.xy + crop.zw, 0).rgb;
    return float4(detail, weight);
}
float4 Indicator(float4 position : SV_Position) : SV_Target {
    const float distance = DistanceToRegion(position.xy - sourceRect.xy);
    // Projected contour thickness stays in screen pixels for the new mapping.
    const float scale = commonX.w > 0.5 ? max(length(float2(ddx(distance),ddy(distance))),1e-7) : 1;
    clip(options.w * scale - abs(distance));
    return componentScale;
}
float4 Sharpen(float4 position : SV_Position) : SV_Target {
    const float2 uv=position.xy/sizes.xy, step=1/sizes.xy;
    float4 c=source0.SampleLevel(linearClamp,uv,0);
    const float3 a=source0.SampleLevel(linearClamp,uv+float2(step.x,0),0).rgb;
    const float3 b=source0.SampleLevel(linearClamp,uv-float2(step.x,0),0).rgb;
    const float3 d=source0.SampleLevel(linearClamp,uv+float2(0,step.y),0).rgb;
    const float3 e=source0.SampleLevel(linearClamp,uv-float2(0,step.y),0).rgb;
    const float3 low=min(c.rgb,min(min(a,b),min(d,e))),high=max(c.rgb,max(max(a,b),max(d,e)));
    return float4(clamp(c.rgb+(c.rgb-(a+b+d+e)*0.25)*options.x,low,high),c.a);
}
