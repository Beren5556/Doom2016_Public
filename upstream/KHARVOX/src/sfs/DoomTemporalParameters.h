#pragma once
#include "DoomTemporalUniforms.h"
#include "DoomRasterJitter.h"
#include "../common/DoomViewSnapshots.h"
#include <algorithm>
#include <limits>

namespace kharvox::sfs {
// Only coordinates and CPU/bound-UBO identity. Exposure, recording ownership,
// input resource lifetime and post-record writes are separate responsibilities.
struct DoomTemporalParameters {
    DoomRasterJitter jitter;
    bool previousMatches{};
    unsigned phase{};
    static double halton(unsigned n,unsigned base){double v=0,f=1.0/base;while(n){v+=(n%base)*f;n/=base;f/=base;}return v-.5;}
    static bool rasterAxis(uint32_t pixels,uint32_t allocation,float fraction){
        if(!std::isfinite(fraction)||fraction<=0||fraction>1||!pixels||!allocation)return false;
        const double nominal=double(allocation)*fraction;
        return pixels>=std::floor(nominal+1e-4)&&pixels<=std::ceil(nominal-1e-4);
    }
    static std::optional<VkExtent2D> renderExtent(const DoomTemporalUniformSnapshot& u,VkExtent2D allocation){
        if(!allocation.width||!allocation.height||u.high[0]!=float(allocation.width)||u.high[1]!=float(allocation.height))return {};
        VkExtent2D result{};uint32_t* dimensions[]{&result.width,&result.height};
        for(unsigned k=0;k<2;++k){const float scale=u.low[12+k],inverse=u.low[14+k];
            if(!std::isfinite(scale)||scale<=0||scale>1||!std::isfinite(inverse)||std::abs(scale*inverse-1.f)>2e-6f)return {};
            // DOOM stores nominal fractions, not rounded pixel ratios. The TAA
            // viewport can cover the full allocation even at reduced scene size.
            const double pixels=double(k?allocation.height:allocation.width)*scale;
            if(pixels<32)return {};*dimensions[k]=uint32_t(std::round(pixels));
        }
        return result;
    }
    static bool currentMatches(const doom_temporal::ViewSnapshot& v,const DoomTemporalUniformSnapshot& u,VkExtent2D extent,VkExtent2D allocation={},const char** reason=nullptr){
        const auto stage=[&](const char* text){if(reason)*reason=text;};
        stage("view-mode-phase-rectangle");
        if(v.mode||v.phase>=8||v.rectangle[0]||v.rectangle[1]||v.rectangle[2]<=0||v.rectangle[3]<=0)return false;
        stage("view-matrix-finite");
        for(const auto* matrix:{&v.jittered,&v.unjittered,&v.view})for(auto x:*matrix)if(!std::isfinite(x))return false;
        const auto& h=u.high;const auto& l=u.low;const auto& p=v.unjittered;const auto& m=v.view;
        stage("active-allocation-extent");
        if(!allocation.width&&!allocation.height)allocation=extent;
        if(!extent.width||!extent.height||extent.width>allocation.width||extent.height>allocation.height)return false;
        const float sx=l[12],sy=l[13];
        const auto raster=renderExtent(u,allocation);
        if(!raster||!rasterAxis(extent.width,allocation.width,sx)||!rasterAxis(extent.height,allocation.height,sy))return false;
        for(unsigned k=12;k<16;++k)if(!std::isfinite(l[k]))return false;
        // Observed shader: texture UV * resolutionscale.zw is full-view UV;
        // previous reprojection is multiplied by resolutionscale.xy. Only
        // fixed, internally consistent active rectangles are admitted here.
        stage("uniform-extents-and-resolution-scale");
        if(h[0]!=float(allocation.width)||h[1]!=float(allocation.height)||h[16]!=0||h[17]!=0||h[48]!=1||h[49]!=1||
           std::abs(l[14]-1.f/sx)>1e-6f||std::abs(l[15]-1.f/sy)>1e-6f)return false;
        stage("uniform-texel-reciprocals");
        for(unsigned k=0;k<2;++k){const float reciprocal=1.f/(k?allocation.height:allocation.width);
            if(!std::isfinite(reciprocal)||std::abs(h[2+k]-reciprocal)>1e-10f||std::abs(h[18+k]-reciprocal)>1e-10f||std::abs(h[50+k]-reciprocal/l[12+k])>1e-9f)return false;}
        stage("projection-identity");
        for(unsigned n=0;n<4;++n)if(l[n]!=p[8+n])return false;
        if(p[12]||p[13]||p[14]!=-1||p[15]||!(p[0]>0&&p[5]>0)||!(p[10]<-1&&p[11]<0))return false;
        for(unsigned n=0;n<16;++n)if(n!=2&&n!=6&&v.jittered[n]!=p[n])return false;
        stage("raster-jitter-phase");
        const double phaseX=halton(v.phase+1,3),phaseY=halton(v.phase+1,2);
        const double rasterX=(double(v.jittered[2])-p[2])*v.rectangle[2]*.5;
        const double rasterY=(double(v.jittered[6])-p[6])*v.rectangle[3]*.5;
        // The engine's projection dispatch multiplies each dimension in float32
        // and CVTTSS2SI truncates it before generating jitter. Using the nominal
        // fraction instead rejects phase 7 at 2800x2974 / .6667 once the original
        // perspective calculation's float32 cancellation is included.
        // Both axes must agree with the same observed Halton phase and route.
        const bool outputJitter=std::abs(rasterX-phaseX)<=.0002&&std::abs(rasterY-phaseY)<=.0002;
        const double sceneWidth=std::trunc(float(v.rectangle[2])*sx),sceneHeight=std::trunc(float(v.rectangle[3])*sy);
        const bool sceneJitter=sceneWidth>0&&sceneHeight>0&&
            std::abs(rasterX*sceneWidth/v.rectangle[2]-phaseX)<=.0002&&
            std::abs(rasterY*sceneHeight/v.rectangle[3]-phaseY)<=.0002;
        if(!outputJitter&&!sceneJitter)return false;
        stage("temporal-filter-phase");
        double weights[9]{},total=0;unsigned n=0;
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){weights[n]=std::exp(-(8.0/9)*((x+phaseX)*(x+phaseX)+(y+phaseY)*(y+phaseY)));total+=weights[n++];}
        for(unsigned k=0;k<9;++k){const float actual=h[k<8?32+k:44];if(!std::isfinite(actual)||std::abs(actual-weights[k]/total)>1e-6)return false;}
        // Invert the observed rotation explicitly; using its transpose hides
        // float32 non-orthogonality and weakens the opposing-jitter control.
        stage("camera-rotation");
        const double a=m[0],b=m[1],c=m[2],d=m[4],e=m[5],f=m[6],g=m[8],hh=m[9],i=m[10];
        const double det=a*(e*i-f*hh)-b*(d*i-f*g)+c*(d*hh-e*g);
        if(!std::isfinite(det)||std::abs(std::abs(det)-1)>.001)return false;
        const double inv[3][3]{{(e*i-f*hh)/det,(c*hh-b*i)/det,(b*f-c*e)/det},
            {(f*g-d*i)/det,(a*i-c*g)/det,(c*d-a*f)/det},
            {(d*hh-e*g)/det,(b*g-a*hh)/det,(a*e-b*d)/det}};
        constexpr double eps=std::numeric_limits<float>::epsilon();
        const unsigned offsets[]{4,8,16,20};const int corners[4][2]{{-1,1},{1,1},{-1,-1},{1,-1}};
        stage("camera-frustum-rays");
        for(unsigned corner=0;corner<4;++corner){const double ray[]{(corners[corner][0]+double(p[2]))/p[0],(corners[corner][1]+double(p[6]))/p[5],-1};
            for(unsigned r=0;r<3;++r){double expected=0,magnitude=0;for(unsigned k=0;k<3;++k){expected+=inv[r][k]*ray[k];magnitude+=std::abs(inv[r][k]*ray[k]);}
                const auto actual=l[offsets[corner]+r];if(!std::isfinite(actual)||std::abs(actual-expected)>32*eps*magnitude+1e-7)return false;}}
        // Match the camera origin as well: rotations alone cannot distinguish
        // two views with equal orientation and different translations.
        stage("camera-origin");
        for(unsigned r=0;r<3;++r){double residual=m[r*4+3],magnitude=std::abs(residual);
            for(unsigned k=0;k<3;++k){const double term=double(m[r*4+k])*l[24+k];residual+=term;magnitude+=std::abs(term);}
            if(!std::isfinite(residual)||std::abs(residual)>16*eps*magnitude+1e-6)return false;}
        stage(nullptr);return true;
    }
    static bool previousMatchesView(const doom_temporal::ViewSnapshot& old,const DoomTemporalUniformSnapshot& u){
        double vp[4][4]{},magnitude[4][4]{};for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)for(unsigned k=0;k<4;++k){
            const auto term=double(old.unjittered[r*4+k])*old.view[k*4+c];vp[r][c]+=term;magnitude[r][c]+=std::abs(term);}
        constexpr double eps=std::numeric_limits<float>::epsilon();
        for(unsigned k=0;k<4;++k){const double expected[]{.5*(vp[0][k]+vp[3][k]),.5*(-vp[1][k]+vp[3][k]),vp[3][k]};
            const double bound[]{.5*(magnitude[0][k]+magnitude[3][k]),.5*(magnitude[1][k]+magnitude[3][k]),magnitude[3][k]};
            const unsigned offset[]{8,12,4};for(unsigned n=0;n<3;++n)if(!std::isfinite(u.high[offset[n]+k])||std::abs(u.high[offset[n]+k]-expected[n])>8*eps*bound[n]+1e-7)return false;}
        return true;
    }
    static std::optional<DoomTemporalParameters> derive(const doom_temporal::ViewSnapshot& view,const doom_temporal::ViewSnapshot* previous,
        const DoomTemporalUniformSnapshot& uniforms,const FrameUniforms& eyes,VkExtent2D extent,const std::array<float,6>& viewport,VkExtent2D allocation={}){
        if(!currentMatches(view,uniforms,extent,allocation)||viewport[0]||viewport[1]||viewport[2]!=float(extent.width)||viewport[3]!=float(extent.height)||viewport[4]!=0||viewport[5]!=1)return {};
        const auto jitter=doomRasterJitter(view.jittered,view.unjittered,eyes,viewport);if(!jitter)return {};
        DoomTemporalParameters result;result.jitter=*jitter;result.phase=view.phase;
        result.previousMatches=previous&&previous->source==view.source&&previous->present+1==view.present&&
            (previous->phase+1)%8==view.phase&&previous->rectangle==view.rectangle&&previousMatchesView(*previous,uniforms);
        return result;
    }
};
}
