#pragma once
#include "../src/sfs/DoomTemporalParameters.h"

// Replay the float32 operations in the pinned engine's projection dispatch
// (0x1830A19..37), jitter generator and perspective matrix (0x2C42D0/0x2C41E0).
// In particular, CVTTSS2SI truncates the scaled dimensions BEFORE jitter is
// generated; adding the offset to the near-plane edges loses float precision.
inline void doomEngineJitterFixture(kharvox::doom_temporal::ViewSnapshot& view,
    kharvox::sfs::DoomTemporalUniformSnapshot& uniforms,VkExtent2D allocation,float fraction){
    view.rectangle={0,0,int(allocation.width),int(allocation.height)};
    uniforms.high[0]=float(allocation.width);uniforms.high[1]=float(allocation.height);
    for(unsigned axis=0;axis<2;++axis){
        const unsigned offset=axis?6:2;
        const float dimension=float(axis?allocation.height:allocation.width);
        uniforms.high[2+axis]=uniforms.high[18+axis]=1.f/dimension;
        uniforms.high[50+axis]=uniforms.high[2+axis]/fraction;
        uniforms.low[12+axis]=fraction;uniforms.low[14+axis]=1.f/fraction;
        const float nearPlane=view.unjittered[11]/view.unjittered[10];
        const float halfSpan=nearPlane/view.unjittered[axis?5:0];
        const int denominator=int(dimension*fraction);
        const float pixel=float(kharvox::sfs::DoomTemporalParameters::halton(view.phase+1,axis?2:3));
        const float offsetOnPlane=(2.f*halfSpan/float(denominator))*pixel;
        const float lo=-halfSpan+offsetOnPlane,hi=halfSpan+offsetOnPlane;
        view.jittered[offset]=(hi+lo)/(hi-lo);
    }
}
