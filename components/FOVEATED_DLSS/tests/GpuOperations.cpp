#include <dlss_foveado/D3D11.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace dlss_foveado;
using namespace dlss_foveado::d3d11;
template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::vector<std::array<float,4>> read(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source) {
    D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
    // Tests use float32 targets for an independent exact readback oracle.
    check(desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT, "test readback format");
    desc.BindFlags = desc.MiscFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.Usage = D3D11_USAGE_STAGING;
    Ptr<ID3D11Texture2D> staging; check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)), "staging allocation");
    context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE map{}; check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "readback map");
    std::vector<std::array<float,4>> pixels(size_t(desc.Width) * desc.Height);
    for (uint32_t y=0; y<desc.Height; ++y) std::memcpy(pixels.data()+size_t(y)*desc.Width,
        static_cast<const char*>(map.pData)+size_t(y)*map.RowPitch, size_t(desc.Width)*sizeof(pixels[0]));
    context->Unmap(staging.Get(), 0); return pixels;
}
int main() try {
    Ptr<ID3D11Device> device; Ptr<ID3D11DeviceContext> context; D3D_FEATURE_LEVEL level{};
    check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        &device, &level, &context)), "WARP device");
    Operations gpu(device.Get()); Image atlas, cropped, central, base, composite;
    gpu.Ensure(atlas, {128,64}, DXGI_FORMAT_R32G32B32A32_FLOAT);
    std::vector<std::array<float,4>> pattern(128*64);
    for (unsigned y=0;y<64;++y) for(unsigned x=0;x<128;++x)
        pattern[y*128+x] = x<64 ? std::array<float,4>{float(x),float(y),0,1} : std::array<float,4>{999,999,999,1};
    context->UpdateSubresource(atlas.texture.Get(),0,nullptr,pattern.data(),128*sizeof(pattern[0]),0);
    gpu.Ensure(cropped,{64,64},DXGI_FORMAT_R32G32B32A32_FLOAT);
    const D3D11_VIEWPORT prior{7,11,23,31,0.1f,0.9f}; context->RSSetViewports(1,&prior);
    auto* bound=atlas.view.Get(); context->PSSetShaderResources(7,1,&bound);
    gpu.Resample(context.Get(),atlas.texture.Get(),{0,0,64,64},cropped,true);
    const auto first=gpu.Views();
    for(int n=0;n<16;++n)gpu.Resample(context.Get(),atlas.texture.Get(),{0,0,64,64},cropped,true,{2,3});
    check(gpu.Views().created==first.created&&gpu.Views().reused>=first.reused+16,"repeated shader crop must reuse views");
    gpu.Resample(context.Get(),atlas.texture.Get(),{0,0,64,64},cropped,true);
    Image temporary;gpu.Ensure(temporary,{8,8},DXGI_FORMAT_R32G32B32A32_FLOAT);auto weak=std::weak_ptr(temporary.viewLifetime);temporary={};
    check(weak.expired(),"SRV lookup must not retain retired image ownership");
    auto pixels=read(device.Get(),context.Get(),cropped.texture.Get());
    check(pixels[63][0]==63 && pixels[63*64+63][1]==63, "crop leaks adjacent eye or shifts a texel");
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)
        check(pixels[y*64+x]==pattern[y*128+x],"identity crop must preserve every channel exactly");
    gpu.Resample(context.Get(),atlas.texture.Get(),{0,0,64,64},cropped,true,{2,3});
    auto convertedMotion=read(device.Get(),context.Get(),cropped.texture.Get());
    check(convertedMotion[17*64+13][0]==26&&convertedMotion[17*64+13][1]==51,"motion unit conversion must not use raw copy");
    UINT count=1; D3D11_VIEWPORT after{}; context->RSGetViewports(&count,&after);
    check(after.TopLeftX==prior.TopLeftX && after.Width==prior.Width && after.MinDepth==prior.MinDepth, "viewport state not restored");
    Ptr<ID3D11ShaderResourceView> restored; context->PSGetShaderResources(7,1,&restored);
    check(restored.Get()==atlas.view.Get(), "shader input state not restored");
    gpu.Ensure(central,{64,64},DXGI_FORMAT_R32G32B32A32_FLOAT);
    gpu.Ensure(base,{64,64},DXGI_FORMAT_R32G32B32A32_FLOAT);
    gpu.Ensure(composite,{64,64},DXGI_FORMAT_R32G32B32A32_FLOAT);
    const float red[]{1,0,0,1}, blue[]{0,0,1,1};
    context->ClearRenderTargetView(central.target.Get(),red); context->ClearRenderTargetView(base.target.Get(),blue);
    RegionPlan r{}; r.centerPixels={32,32}; r.halfSizePixels={16,16}; r.transitionPixels=4;
    r.shape=Shape::Ellipse; r.cropScale={1./64,1./64};
    gpu.Composite(context.Get(),base.texture.Get(),central.texture.Get(),r,composite,false);
    pixels=read(device.Get(),context.Get(),composite.texture.Get());
    check(pixels[32*64+32][0]==1 && pixels[0][2]==1,"composite center/periphery");
    const double expected=CentralWeight(r,{49.5,32.5});
    check(std::abs(pixels[32*64+49][0]-expected)<1e-5,"GPU transition differs from analytic mask");
    gpu.Indicator(context.Get(),composite.texture.Get(),{0,0,64,64},r);
    pixels=read(device.Get(),context.Get(),composite.texture.Get());
    check(pixels[32*64+47][1]>0.5f && pixels[32*64+32][1]==0,"indicator coverage");
    auto clean=read(device.Get(),context.Get(),central.texture.Get());
    check(clean[32*64+47][1]==0,"indicator contaminated reconstruction input");
    for(const auto shape:{Shape::Ellipse,Shape::Rectangle}) for(const auto side:{NasalSide::Left,NasalSide::Right})
    for(bool locked:{false,true}) {
        Config cfg;cfg.geometryMode=GeometryMode::Legacy;cfg.central.widthBps=8000;cfg.central.shape=shape;cfg.central.lockHeight=locked;
        cfg.central.verticalRatioBps=9000;cfg.central.upperShareBps=7000;
        const std::array<ViewGeometry,2> views{{{{64,64},{64,64},1,side},{{64,64},{64,64},2,side}}};
        const auto nasal=Plan(cfg,views,FixedPlacement(cfg),1,1,1).views[0].central;
        for(const double scale:{1.,2.}) {
            auto scaled=nasal;scaled.centerPixels={nasal.centerPixels.x*scale,nasal.centerPixels.y*scale};
            scaled.halfSizePixels={nasal.halfSizePixels.x*scale,nasal.halfSizePixels.y*scale};
            scaled.transitionPixels*=scale;scaled.cropScale={1./(64*scale),1./(64*scale)};scaled.cropBias={0,0};
            // Neural-before uses input pixels; its geometry remains in output pixels.
            gpu.Composite(context.Get(),base.texture.Get(),central.texture.Get(),scaled,composite,false,{scale,scale});
            pixels=read(device.Get(),context.Get(),composite.texture.Get());
            for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
                // Independent reference from the approved 80/90/70-30 example.
                const double rx=51.2, ry=locked?32:26.24, cy=locked?32:34.304;
                const double dx=std::abs(x+.5-(side==NasalSide::Right?64:0)),dy=std::abs(y+.5-cy);
                const double distance=shape==Shape::Ellipse?(std::hypot(dx/rx,dy/ry)-1)*ry:std::max(dx-rx,dy-ry);
                const double t=std::clamp(distance/(ry*.1),0.,1.), w=1-t*t*(3-2*t);
                check(std::abs(pixels[y*64+x][0]-w)<2e-5,"GPU differs from independent binocular oval oracle");
                check(std::abs(CentralWeight(nasal,{x+.5,y+.5})-w)<1e-10,"CPU differs from drawing oracle");
            }
            const unsigned inner=side==NasalSide::Right?63:0,outer=63-inner;
            check(pixels[32*64+inner][0]==1 && pixels[32*64+outer][2]==1,"nasal/outer orientation wrong");
            check(locked ? pixels[inner][0]==1 : pixels[inner][2]==1,"height lock/relative reduction not applied on GPU");
        }
        gpu.Composite(context.Get(),base.texture.Get(),central.texture.Get(),nasal,composite,false);
        gpu.Indicator(context.Get(),composite.texture.Get(),{0,0,64,64},nasal);
        pixels=read(device.Get(),context.Get(),composite.texture.Get());
        const unsigned outerArc=side==NasalSide::Right?13:50,inner=side==NasalSide::Right?63:0;
        const unsigned cy=locked?32:34;
        check(pixels[cy*64+outerArc][1]>.5f,"indicator lost outer curve");
        check(pixels[cy*64+inner][1]==0,"indicator drew a vertical nasal seam");
        if(shape==Shape::Ellipse)check(pixels[2*64+32][1]==0,"old straight cut still drawn above oval");
    }
    const std::array<std::array<uint32_t,4>,6> cuts{{{2000,2000,2000,2000},{2000,2000,0,0},
        {0,0,2000,2000},{3500,500,2000,2000},{2000,2000,3500,500},{0,4500,3500,1000}}};
    for(auto sides:cuts)for(auto shape:{Shape::Ellipse,Shape::Rectangle})for(double perspective:{0.,.003})
    for(uint32_t transition:{0u,2000u})for(double scale:{1.,2.}){
        Config cfg;cfg.neuralLinked=false;
        cfg.neural.shape=shape;cfg.neural.topBps=sides[0];cfg.neural.bottomBps=sides[1];
        cfg.neural.rightBps=sides[2];cfg.neural.leftBps=sides[3];cfg.neural.transitionBps=transition;
        const auto size=uint32_t(64*scale);
        std::array<ViewGeometry,2> views{{{{64,64},{size,size},1},{{64,64},{size,size},2}}};
        for(unsigned eye=0;eye<2;++eye)views[eye].binocular={{{.8/(64*scale),0,eye?.2:0.,
            0,1./(64*scale),0,perspective/scale,0,1}},9,1};
        const auto plan=Plan(cfg,views,FixedPlacement(cfg),1,1,1,{1,16384});
        for(unsigned eye=0;eye<2;++eye){
            const auto& m=plan.views[eye].neural;
            gpu.Composite(context.Get(),base.texture.Get(),central.texture.Get(),m,composite,false,{scale,scale});
            pixels=read(device.Get(),context.Get(),composite.texture.Get());
            for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
                const double denominator=1+perspective*(x+.5);
                const double dx=(.8*(x+.5)/64+(eye?.2:0.))/denominator-.5;
                const double dy=((y+.5)/64)/denominator-.5;
                const double rx=.5-sides[dx<0?3:2]/10000.,ry=.5-sides[dy<0?0:1]/10000.;
                const double q=shape==Shape::Ellipse?std::hypot(dx/rx,dy/ry):std::max(std::abs(dx)/rx,std::abs(dy)/ry);
                const double t=transition?std::clamp((q-1)/(transition/10000.),0.,1.):(q>1?1.:0.);
                const double expectedWeight=1-t*t*(3-2*t);
                check(std::abs(pixels[y*64+x][0]-expectedWeight)<3e-5,"GPU four-side projection/transition/orientation disagrees with oracle");
                check(std::abs(CentralWeight(m,{(x+.5)*scale,(y+.5)*scale})-expectedWeight)<1e-10,"CPU projective weight disagrees with oracle");
            }
        }
    }
    // Fixed common contour must not acquire a seam at either projected common center.
    Config oval;
    std::array<ViewGeometry,2> overlap{{{{64,64},{64,64},1},{{64,64},{64,64},2}}};
    for(unsigned eye=0;eye<2;++eye)overlap[eye].binocular={{{.8/64,0,eye?.2:0.,0,1./64,0,0,0,1}},9,1};
    const auto contour=Plan(oval,overlap,FixedPlacement(oval),1,1,1,{1,16384});
    for(unsigned eye=0;eye<2;++eye){
        const auto& m=contour.views[eye].central;
        gpu.Composite(context.Get(),base.texture.Get(),central.texture.Get(),m,composite,false);
        gpu.Indicator(context.Get(),composite.texture.Get(),{0,0,64,64},m);
        pixels=read(device.Get(),context.Get(),composite.texture.Get());
        check(pixels[32*64+(eye?47:16)][1]>.5f,"projected indicator lost outside arc");
        for(unsigned y=24;y<40;++y)check(pixels[y*64+(eye?24:40)][1]==0,"projected indicator drew central seam");
        check(read(device.Get(),context.Get(),central.texture.Get())==clean,"new indicator contaminated reconstruction/history");
    }
    gpu.Sharpen(context.Get(),central.texture.Get(),composite,1.f);
    pixels=read(device.Get(),context.Get(),composite.texture.Get());
    check(pixels==clean,"sharpening changes a uniform area or its alpha");
    bool rejected=false; try { gpu.Resample(context.Get(),atlas.texture.Get(),{127,0,2,1},cropped); }
    catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"out-of-bounds input accepted");
    std::cout << "PASS: WARP crop, asymmetric atlas isolation, analytic composition, indicator and state restoration\n";
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
