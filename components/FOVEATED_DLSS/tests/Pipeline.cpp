#include <dlss_foveado/Pipeline.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace dlss_foveado;using namespace dlss_foveado::d3d11;
template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Backend:ReconstructionBackend {
    ID3D11Device* device;bool corrupt{};unsigned calls{},resetCalls{};
    std::vector<FeatureSpec> specs;
    explicit Backend(ID3D11Device* d):device(d){}
    BackendCapabilities Capabilities()const noexcept override{return {ContractVersion,true,true,false,8,32,16384};}
    ReconstructionReceipt Evaluate(ID3D11DeviceContext* c,const ReconstructionInput& in)override{
        specs.push_back(in.feature);
        Ptr<ID3D11RenderTargetView> target;check(SUCCEEDED(device->CreateRenderTargetView(in.destination,nullptr,&target)),"mock target");
        const float central0[]{1,0,0,1},central1[]{0,0,1,1},peripheral[]{0,1,0,1};
        c->ClearRenderTargetView(target.Get(),in.feature.branch==Branch::Peripheral?peripheral:in.feature.viewId==1?central0:central1);
        if(in.reset)++resetCalls;
        ++calls;return {calls,in.epoch,in.frame+(corrupt?1:0),in.feature.viewId,in.feature.branch,in.feature.input,in.feature.output,1};
    }
    bool Stop(ID3D11DeviceContext*)override{return true;}
};
std::array<float,4> pixel(ID3D11Device* d,ID3D11DeviceContext* c,Operations& gpu,ID3D11Texture2D* image,unsigned x,unsigned y){
    Image converted;D3D11_TEXTURE2D_DESC desc{};image->GetDesc(&desc);
    gpu.Ensure(converted,{desc.Width,desc.Height},DXGI_FORMAT_R32G32B32A32_FLOAT);
    gpu.Resample(c,image,{0,0,desc.Width,desc.Height},converted);
    converted.texture->GetDesc(&desc);desc.BindFlags=desc.MiscFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Ptr<ID3D11Texture2D> read;check(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&read)),"read staging");c->CopyResource(read.Get(),converted.texture.Get());
    D3D11_MAPPED_SUBRESOURCE m{};check(SUCCEEDED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&m)),"map");
    const auto* values=reinterpret_cast<const float*>(static_cast<const char*>(m.pData)+size_t(y)*m.RowPitch)+x*4;
    std::array<float,4> result;std::copy_n(values,4,result.begin());c->Unmap(read.Get(),0);return result;
}
struct Neural:NeuralRegionBackend {
    std::array<NeuralRegionSpec,2> specs;unsigned calls{},resetCalls{};
    bool Prepare(const std::array<NeuralRegionSpec,2>& r)override{specs=r;return true;}
    NeuralRegionReceipt Evaluate(ID3D11DeviceContext* c,const NeuralRegionInput& in)override{
        check(in.region==specs[in.region.viewId-1],"NR preparation/processing extent mismatch");
        if(in.reset)++resetCalls;
        c->CopyResource(in.destination,in.color);return {in.epoch,in.frame,in.region.viewId,++calls,in.region.extent};
    }
};
int main()try {
    Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP");
    Operations gpu(device.Get());Backend backend(device.Get());Neural neural;FixedPipeline pipeline(device.Get(),backend);
    Image atlas,depth,motion;gpu.Ensure(atlas,{512,256});gpu.Ensure(depth,{256,256},DXGI_FORMAT_R32_FLOAT);gpu.Ensure(motion,{256,256},DXGI_FORMAT_R32G32_FLOAT);
    const float zero[]{0,0,0,0};context->ClearRenderTargetView(atlas.target.Get(),zero);context->ClearRenderTargetView(depth.target.Get(),zero);context->ClearRenderTargetView(motion.target.Get(),zero);
    Config config;config.geometryMode=GeometryMode::Legacy;config.enabled=true;config.periphery=Periphery::Temporal;
    const std::array<ViewGeometry,2> geometry{{{{256,256},{512,512},1},{{256,256},{512,512},2}}};
    std::array<EyeOutput,2> prior;
    for(unsigned scenario=0;scenario<9;++scenario){
        const auto mode=scenario%3;const bool protectedNose=scenario>=3;
        auto views=geometry;
        if(protectedNose){views[0].nasalSide=NasalSide::Right;views[1].nasalSide=NasalSide::Left;}
        config.central.shape=mode==0?Shape::Ellipse:Shape::Rectangle;config.neuralLinked=mode!=2;config.neural.widthBps=8000;
        config.central.lockHeight=config.neural.lockHeight=scenario<6;
        config.central.verticalRatioBps=config.neural.verticalRatioBps=9000;
        config.central.upperShareBps=config.neural.upperShareBps=7000;
        const auto plan=Plan(config,views,FixedPlacement(config),1,scenario+1,scenario+1);
        const bool nr=mode>0,before=mode==1;neural.Prepare(FixedPipeline::NeuralRegions(plan,before));
        pipeline.Begin(plan,{config,11,Quality::Performance,nr,before},nr?&neural:nullptr);
        check(!pipeline.CanPublish(prior[0]),"old receipt survived Begin");
        for(unsigned eye=0;eye<2;++eye){
            prior[eye]=pipeline.Evaluate(context.Get(),eye,{atlas.texture.Get(),depth.texture.Get(),motion.texture.Get(),eye*256,0,{.25,-.25},11,true});
            const auto center=pixel(device.Get(),context.Get(),gpu,prior[eye].image.texture.Get(),256,256);
            const auto border=pixel(device.Get(),context.Get(),gpu,prior[eye].image.texture.Get(),protectedNose&&eye?511:0,0);
            check(center[eye?2:0]>.99f&&border[1]>.99f,"eye identity or central/peripheral composition broken");
            if(protectedNose){
                const auto inner=pixel(device.Get(),context.Get(),gpu,prior[eye].image.texture.Get(),eye?0:511,0);
                if(scenario<6){
                    check(inner[eye?2:0]>.99f&&inner[1]<.01f,"locked SR/NR pipeline loses nasal central quality");
                    if(nr)check(neural.specs[eye].extent.height==(before?256u:512u),"locked regional NR omitted full nasal height");
                }else{
                    check(inner[1]>.99f,"unlocked pipeline did not reduce top coverage");
                    if(nr)check(neural.specs[eye].extent.height<(before?256u:512u),"unlocked NR crop still uses entire height");
                }
            }
        }
        check(pipeline.Complete()&&pipeline.CanPublish(prior[0])&&pipeline.CanPublish(prior[1]),"stereo receipt");
        auto tampered=prior[0];tampered.image=prior[1].image;check(!pipeline.CanPublish(tampered),"receipt allowed an output from another eye");
    }
    check(neural.calls==12,"both NR order paths not executed for symmetric, locked and unlocked geometry");
    auto plan=Plan(config,geometry,FixedPlacement(config),2,10,10);pipeline.Begin(plan,{config});
    auto left=pipeline.Evaluate(context.Get(),0,{atlas.texture.Get(),depth.texture.Get(),motion.texture.Get(),0,0,{0,0},11,true});
    backend.corrupt=true;bool rejected{};try{pipeline.Evaluate(context.Get(),1,{atlas.texture.Get(),depth.texture.Get(),motion.texture.Get(),256,0,{0,0},11,true});}catch(...){rejected=true;}
    check(rejected&&!pipeline.Complete()&&!pipeline.CanPublish(left),"failed right eye did not invalidate entire pair");
    backend.corrupt=false;config=Config{};config.enabled=true;config.periphery=Periphery::Temporal;
    config.central.topBps=3500;config.central.bottomBps=500;config.neuralLinked=false;
    config.neural.rightBps=0;config.neural.leftBps=3500;
    auto views=geometry;
    for(unsigned eye=0;eye<2;++eye)views[eye].binocular={{{.8/512,0,eye?.2:0.,0,1./512,0,0,0,1}},3,1};
    for(unsigned mode=0;mode<3;++mode){
        plan=Plan(config,views,FixedPlacement(config),3,20+mode,20+mode);
        const bool nr=mode>0,before=mode==1;neural.Prepare(FixedPipeline::NeuralRegions(plan,before));
        pipeline.Begin(plan,{config,11,Quality::Performance,nr,before},nr?&neural:nullptr);
        const auto resetStart=backend.resetCalls,neuralResetStart=neural.resetCalls;
        for(unsigned eye=0;eye<2;++eye){
            prior[eye]=pipeline.Evaluate(context.Get(),eye,{atlas.texture.Get(),depth.texture.Get(),motion.texture.Get(),eye*256,0,{0,0},11,false});
            const unsigned centerX=eye?192:320;
            const auto center=pixel(device.Get(),context.Get(),gpu,prior[eye].image.texture.Get(),centerX,256);
            const auto top=pixel(device.Get(),context.Get(),gpu,prior[eye].image.texture.Get(),centerX,20);
            check(center[eye?2:0]>.99f && top[1]>.99f,"four-side pipeline lost common center/top cut");
        }
        check(pipeline.Complete() && pipeline.CanPublish(prior[0]) && pipeline.CanPublish(prior[1]),"four-side stereo publication");
        check(backend.resetCalls-resetStart==4 && (!nr || neural.resetCalls-neuralResetStart==2),"new epoch did not reset both eye histories/providers");
    }
    auto changed=views;for(auto& view:changed)++view.binocular.revision;
    rejected=false;try{pipeline.Begin(Plan(config,changed,FixedPlacement(config),3,23,22),{config,11,Quality::Performance,true,false},&neural);}catch(...){rejected=true;}
    check(rejected,"projection revision changed without epoch reset");
    config.central.topBps=1000;
    rejected=false;try{pipeline.Begin(Plan(config,views,FixedPlacement(config),4,23,22),{config,11,Quality::Performance,true,false},&neural);}catch(...){rejected=true;}
    check(rejected,"mask changed without epoch reset");
    // Real pipeline routing: native scene, supersampled native AA center and
    // reduced-input SR periphery; no change to the common-plane projection.
    config=Config{};config.enabled=true;config.periphery=Periphery::Temporal;config.peripheralScaleBps=5000;
    auto nativeViews=views;for(auto& v:nativeViews)v.input=v.output;
    gpu.Ensure(atlas,{1024,512});gpu.Ensure(depth,{512,512},DXGI_FORMAT_R32_FLOAT);gpu.Ensure(motion,{512,512},DXGI_FORMAT_R32G32_FLOAT);
    context->ClearRenderTargetView(atlas.target.Get(),zero);context->ClearRenderTargetView(depth.target.Get(),zero);context->ClearRenderTargetView(motion.target.Get(),zero);
    for(unsigned variant=0;variant<4;++variant){
        config.supersampleBps=variant%2?15000:10000;config.showRegion=true;
        plan=Plan(config,nativeViews,FixedPlacement(config),5,30+variant,30+variant);
        PipelineSettings settings{config};settings.centralDlaa=true;settings.dynamic=variant>=2;
        backend.specs.clear();pipeline.Begin(plan,settings);
        for(unsigned eye=0;eye<2;++eye)pipeline.Evaluate(context.Get(),eye,{atlas.texture.Get(),depth.texture.Get(),motion.texture.Get(),eye*512,0,{.25,-.25},11,false});
        check(backend.specs.size()==(settings.dynamic?8u:4u),"hybrid preparation/branch count");
        for(unsigned eye=0;eye<2;++eye){const auto& central=backend.specs[settings.dynamic?eye*4+1:eye*2];const auto& outer=backend.specs[settings.dynamic?eye*4+3:eye*2+1];
            check(central.quality==Quality::Native&&central.input==central.output,"hybrid center is not native AA");
            check(outer.quality==Quality::Balanced&&outer.input==Extent{256,256}&&outer.output==Extent{512,512},"hybrid periphery is not full-output SR");}
        pipeline.DrawIndicators(context.Get(),atlas.texture.Get(),{{{0,0,512,512},{512,0,512,512}}});
        if(settings.dynamic){
            const auto outer=pixel(device.Get(),context.Get(),gpu,atlas.texture.Get(),64,256);
            const auto inner=pixel(device.Get(),context.Get(),gpu,atlas.texture.Get(),192,256);
            const auto active=pixel(device.Get(),context.Get(),gpu,atlas.texture.Get(),128,256);
            check(outer[0]>.5&&inner[0]>.5&&active[1]>.5,"dynamic red bounds or green active contour missing");
        }
    }
    PipelineSettings invalid{config};invalid.dynamic=true;invalid.neuralEnabled=true;
    rejected=false;try{pipeline.Begin(Plan(config,nativeViews,FixedPlacement(config),5,40,40),invalid,&neural);}catch(...){rejected=true;}
    check(rejected,"pipeline accepted Dynamic with Neural");
    invalid={config};invalid.centralDlaa=true;
    rejected=false;try{pipeline.Begin(Plan(config,views,FixedPlacement(config),5,41,41),invalid);}catch(...){rejected=true;}
    check(rejected,"pipeline accepted DLAA from a reduced scene");
    std::cout<<"PASS: both eyes, ellipse/rectangle, peripheral AA, NR before/after, independent region and stale/failed receipts\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
