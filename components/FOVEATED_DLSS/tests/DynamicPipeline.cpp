#include <dlss_foveado/Pipeline.h>
#include <dlss_foveado/MemoryPressure.h>
#include <map>
#include <tuple>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace dlss_foveado;using namespace dlss_foveado::d3d11;
template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
void need(bool value,const char* text){if(!value)throw std::runtime_error(text);}
struct Spatial:ReconstructionBackend {
    Operations operations;uint64_t calls{};bool enforceStable{};
    using Key=std::tuple<uint64_t,Branch,uint32_t>;
    std::map<Key,std::array<ID3D11Texture2D*,4>> resources;
    explicit Spatial(ID3D11Device* d):operations(d){}
    BackendCapabilities Capabilities()const noexcept override{return {ContractVersion,true,true,false,8,32,16384};}
    ReconstructionReceipt Evaluate(ID3D11DeviceContext* c,const ReconstructionInput& in)override{
        const auto key=Key{in.feature.viewId,in.feature.branch,in.preparedSlot};
        const std::array<ID3D11Texture2D*,4> pointers{in.color,in.depth,in.motion,in.destination};
        if(enforceStable&&resources.contains(key))need(resources.at(key)==pointers,"dynamic mask recreated prepared textures");
        resources[key]=pointers;
        Image target;target.texture=in.destination;
        Ptr<ID3D11Device> device;in.destination->GetDevice(&device);
        need(SUCCEEDED(device->CreateRenderTargetView(in.destination,nullptr,&target.target)),"test output target");
        operations.Resample(c,in.color,{0,0,in.feature.input.width,in.feature.input.height},target);
        return {++calls,in.epoch,in.frame,in.feature.viewId,in.feature.branch,in.feature.input,in.feature.output,1};
    }
    bool Stop(ID3D11DeviceContext*)override{return true;}
};
std::vector<float> read(ID3D11Device* d,ID3D11DeviceContext* c,Operations& ops,ID3D11Texture2D* image){
    D3D11_TEXTURE2D_DESC desc{};image->GetDesc(&desc);Image converted;
    ops.Ensure(converted,{desc.Width,desc.Height},DXGI_FORMAT_R32G32B32A32_FLOAT);
    ops.Resample(c,image,{0,0,desc.Width,desc.Height},converted);
    converted.texture->GetDesc(&desc);desc.BindFlags=desc.MiscFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Ptr<ID3D11Texture2D> copy;need(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&copy)),"test staging");c->CopyResource(copy.Get(),converted.texture.Get());
    D3D11_MAPPED_SUBRESOURCE map{};need(SUCCEEDED(c->Map(copy.Get(),0,D3D11_MAP_READ,0,&map)),"test map");
    std::vector<float> pixels(size_t(desc.Width)*desc.Height*4);
    for(unsigned y=0;y<desc.Height;++y)std::copy_n(reinterpret_cast<const float*>(static_cast<const char*>(map.pData)+size_t(y)*map.RowPitch),desc.Width*4,pixels.begin()+size_t(y)*desc.Width*4);
    c->Unmap(copy.Get(),0);return pixels;
}
struct TemporalSignal:Spatial {
    struct History {uint64_t frame{},epoch{};unsigned age{};};
    std::map<Key,History> histories;unsigned peripheralResets{};
    explicit TemporalSignal(ID3D11Device* d):Spatial(d){enforceStable=true;}
    ReconstructionReceipt Evaluate(ID3D11DeviceContext* c,const ReconstructionInput& in)override{
        auto receipt=Spatial::Evaluate(c,in);auto& h=histories[{in.feature.viewId,in.feature.branch,in.preparedSlot}];
        const bool reset=in.reset||h.epoch!=in.epoch||h.frame+1!=in.frame;
        if(reset){h.age=0;if(in.feature.branch==Branch::Peripheral)++peripheralResets;}
        h.frame=in.frame;h.epoch=in.epoch;++h.age;
        // Cold history is deliberately a conspicuous wrong color. A converged
        // context follows the current frame, proving no stale output is reused.
        const float live=float(in.frame)*.001f;
        const float value[]{h.age<8?0.f:.7f+live,in.feature.viewId==1?.2f:.5f,live,1};
        Ptr<ID3D11Device> d;in.destination->GetDevice(&d);Ptr<ID3D11RenderTargetView> target;
        need(SUCCEEDED(d->CreateRenderTargetView(in.destination,nullptr,&target)),"temporal signal target");
        c->ClearRenderTargetView(target.Get(),value);return receipt;
    }
};
void CheckPressure(){
    MemoryPressure p;using R=MemoryPressure::Result;constexpr uint64_t mib=1024*1024,budget=8000*mib;
    for(uint64_t t=0;t<5000;t+=10)need(p.Check(t,budget,7600*mib,true,0,0,false)==R::None,"95 percent budget must not switch off prepared rendering");
    need(p.Check(5000,budget,8100*mib,true,0,0,false)==R::None,"one residency spike must not fail rendering");
    need(p.Check(5500,budget,7000*mib,true,0,0,false)==R::None,"pressure clearance");
    need(p.Check(6000,budget,8100*mib,true,0,0,false)==R::None,"sustained pressure starts fresh");
    need(p.Check(6999,budget,8100*mib,true,0,0,false)==R::None,"short pressure duration");
    need(p.Check(7000,budget,8100*mib,true,0,0,false)==R::LocalBudget,"real sustained budget overflow remains guarded");
    need(p.Check(7100,budget,7000*mib,true,0,0,false)==R::None,"healthy budget clears hold");
    need(p.Check(7200,budget,8100*mib,true,0,0,true)==R::LocalBudget,"new contexts cannot allocate above budget");
    need(p.Check(8000,budget,7000*mib,true,300*mib,0,false)==R::None,"paging spike grace");
    need(p.Check(9000,budget,7000*mib,true,300*mib,0,false)==R::Paging,"sustained system memory paging remains guarded");
    std::cout<<"PASS: memory near budget, transient pressure, sustained overflow, paging and allocation guard\n";
}
void CheckTemporal(ID3D11Device* d,ID3D11DeviceContext* c,Operations& ops,Image& atlas,Image& depth,Image& motion,const std::array<ViewGeometry,2>& views){
    TemporalSignal backend(d);FixedPipeline pipeline(d,backend);
    Config config;config.enabled=true;config.periphery=Periphery::Temporal;
    PipelineSettings settings{config};settings.dynamic=true;
    for(uint64_t frame=1;frame<=148;++frame){
        uint32_t cut=frame<=32?1000:frame<=52?1000+uint32_t(frame-32)*100:frame<=84?3000:frame<=104?3000-uint32_t(frame-84)*100:1000;
        if(frame==141)cut=2000;if(frame==143)cut=3000;
        experimental::Write(config.central,{cut,cut,cut,cut});settings.config=config;
        const auto before=backend.calls;
        pipeline.Begin(Plan(config,views,FixedPlacement(config),1,frame,1),settings);
        for(unsigned eye=0;eye<2;++eye){
            auto out=pipeline.Evaluate(c,eye,{atlas.texture.Get(),depth.texture.Get(),motion.texture.Get(),eye*256,0,{0,0},13.89f,false});
            need(pipeline.CanPublish(out),"temporal transition receipt");
            if(frame>=16){const auto pixels=read(d,c,ops,out.image.texture.Get());const unsigned x=eye?192:320;
                const auto index=(256*512+x)*4;
                need(std::abs(pixels[index]-(.7f+float(frame)*.001f))<.003f,"central history became cold or stale during a mask/processing transition");
                need(std::abs(pixels[index+1]-(eye?.5f:.2f))<.003f,"transition mixed eye histories");}
        }
        need(backend.calls-before<=(frame==1?8u:6u),"transition evaluated more than two central contexts per eye");
        if(frame==130)need(backend.calls-before==4,"steady frames must return to one central evaluation per eye");
        need(backend.resources.size()==8,"transition allocated more prepared resources");
    }
    need(backend.peripheralResets==2,"mask changes reset unchanged peripheral histories");
    // A genuine new epoch must still reset history; transition protection cannot
    // hide camera cuts by showing history from the previous scene.
    pipeline.Begin(Plan(config,views,FixedPlacement(config),1,149,2),settings);
    for(unsigned eye=0;eye<2;++eye){auto out=pipeline.Evaluate(c,eye,{atlas.texture.Get(),depth.texture.Get(),motion.texture.Get(),eye*256,0,{0,0},13.89f,true});
        const auto pixels=read(d,c,ops,out.image.texture.Get());need(pixels[(256*512+(eye?192:320))*4]<.01f,"camera reset reused old central history");}
    need(backend.peripheralResets==4,"real epoch reset was suppressed");
    std::cout<<"PASS: 148 stereo frames with continuous live center, contraction/expansion, reversals, bounded resources and genuine camera reset\n";
}
int main()try{
    CheckPressure();
    Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context;
    need(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP device");
    Operations ops(device.Get());Image atlas,depth,motion;
    ops.Ensure(atlas,{512,256},DXGI_FORMAT_R32G32B32A32_FLOAT);ops.Ensure(depth,{256,256},DXGI_FORMAT_R32_FLOAT);ops.Ensure(motion,{256,256},DXGI_FORMAT_R32G32_FLOAT);
    std::vector<float> colors(512*256*4);const float z[]{.5,0,0,0},mv[]{-.5,0,0,0};
    context->ClearRenderTargetView(depth.target.Get(),z);context->ClearRenderTargetView(motion.target.Get(),mv);
    std::array<ViewGeometry,2> views{{{{256,256},{512,512},1},{{256,256},{512,512},2}}};
    for(unsigned eye=0;eye<2;++eye)views[eye].binocular={{{.8/512,0,eye?.2:0.,0,1./512,0,0,0,1}},3,1};
    CheckTemporal(device.Get(),context.Get(),ops,atlas,depth,motion,views);
    unsigned comparisons{};double maximumError{};
    for(const auto shape:{Shape::Ellipse,Shape::Rectangle}){
        Spatial dynamicBackend(device.Get()),referenceBackend(device.Get());dynamicBackend.enforceStable=true;
        FixedPipeline dynamic(device.Get(),dynamicBackend),reference(device.Get(),referenceBackend);
        Config config;config.enabled=true;config.periphery=Periphery::Temporal;config.peripheralScaleBps=5000;config.central.shape=shape;
        PipelineSettings settings{config};settings.dynamic=true;settings.outer={1000,1200,800,1400};settings.inner={3000,2800,3400,2600};
        for(uint64_t frame=1;frame<=43;++frame){
            const unsigned step=unsigned(frame-1)<=21?unsigned(frame-1):43-unsigned(frame);
            experimental::Cuts cuts{};for(unsigned i=0;i<4;++i)cuts[i]=settings.outer[i]+(settings.inner[i]-settings.outer[i])*step/21;
            experimental::Write(config.central,cuts);settings.config=config;
            for(unsigned y=0;y<256;++y)for(unsigned x=0;x<512;++x){const auto p=(y*512+x)*4;colors[p]=.15f+float((x%256)+frame)/600;colors[p+1]=.1f+float(y)/350;colors[p+2]=x<256?.2f:.75f;colors[p+3]=1;}
            context->UpdateSubresource(atlas.texture.Get(),0,nullptr,colors.data(),512*4*sizeof(float),0);
            const auto plan=Plan(config,views,FixedPlacement(config),1,frame,frame);
            dynamic.Begin(plan,settings);reference.Begin(plan,{config});
            const auto before=dynamicBackend.calls;
            for(unsigned eye=0;eye<2;++eye){
                const EyeInput in{atlas.texture.Get(),depth.texture.Get(),motion.texture.Get(),eye*256,0,{0,0},11,false};
                const auto a=dynamic.Evaluate(context.Get(),eye,in),b=reference.Evaluate(context.Get(),eye,in);
                need(dynamic.CanPublish(a)&&reference.CanPublish(b),"current output receipt");
                if(frame%7==1){
                    const auto actual=read(device.Get(),context.Get(),ops,a.image.texture.Get()),expected=read(device.Get(),context.Get(),ops,b.image.texture.Get());
                    need(actual.size()==expected.size(),"output extent");
                    for(size_t i=0;i<actual.size();++i){need(std::isfinite(actual[i]),"nonfinite composition");maximumError=std::max(maximumError,double(std::abs(actual[i]-expected[i])));}
                    ++comparisons;
                }
            }
            need(dynamic.DynamicPrepared()&&dynamicBackend.resources.size()==8,"bounded three-size stereo preparation");
            need(dynamicBackend.calls-before==(frame==1?8u:4u),"unused size evaluated after preparation");
            need(dynamic.CurrentPlan().config==config,"visible contour replaced by processing contour");
        }
    }
    need(maximumError<.004,"prepared crop UV/mask differs from the original full-resolution mapping");
    // Equal bounds must share one prepared context per eye rather than create
    // identical NGX features repeatedly.
    Spatial collapsedBackend(device.Get());FixedPipeline collapsed(device.Get(),collapsedBackend);
    Config c;c.enabled=true;c.periphery=Periphery::Temporal;PipelineSettings s{c};s.dynamic=true;s.inner=s.outer={2000,2000,2000,2000};
    collapsed.Begin(Plan(c,views,FixedPlacement(c),1,1,1),s);
    for(unsigned eye=0;eye<2;++eye)collapsed.Evaluate(context.Get(),eye,{atlas.texture.Get(),depth.texture.Get(),motion.texture.Get(),eye*256,0,{0,0},11,false});
    need(collapsed.DynamicPrepared()&&collapsedBackend.resources.size()==4,"equal bounds did not deduplicate contexts");
    std::cout<<"PASS: "<<comparisons<<" full-image moving-gradient comparisons, maximum error "<<maximumError<<"; two shapes/eyes, stable textures, bounded evaluations and collapsed bounds\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
