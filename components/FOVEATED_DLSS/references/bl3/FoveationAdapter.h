#pragma once
#include <dlss_foveado/Pipeline.h>
#include "NeuralPreparation.h"
#include "../imported/ac7_r37/src/PanelControls.h"

namespace b3::temporal {
using FoveationBackendFactory=std::unique_ptr<dlss_foveado::d3d11::ReconstructionBackend>(*)(ID3D11Device*,dlss_foveado::d3d11::NgxOptions);
// Only this adapter knows BL3's private runtime layout and Neural transport.
// The exported engine accepts textures, placement and versioned receipts.
class FoveationNeural final : public dlss_foveado::d3d11::NeuralRegionBackend {
    using Spec=dlss_foveado::d3d11::NeuralRegionSpec;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    std::filesystem::path root;
    NeuralFactory factory;
    NeuralPreparation::Hosts hosts;
    std::shared_ptr<NeuralPreparation> job;
    std::array<Spec,2> selected{};
    nvidia_dlss::PanelValues settings{},selectedSettings{};
    bool preparing{};
    uint64_t serial{};
    static void Need(bool v,const char* e){if(!v)throw std::runtime_error(e);}
    bool Matches(const std::array<Spec,2>& r)const {
        return selected==r&&settings.neuralPercent==selectedSettings.neuralPercent&&
            settings.preset==selectedSettings.preset&&settings.appearance==selectedSettings.appearance;
    }
public:
    FoveationNeural(ID3D11Device* d,std::filesystem::path r,NeuralFactory f):device(d),root(std::move(r)),factory(f){}
    ~FoveationNeural(){try{(void)Stop();}catch(...) {}}
    void Settings(const nvidia_dlss::PanelValues& v){settings=v;}
    bool Stop(){
        if(job&&preparing){job->Cancel();preparing=false;}
        if(job){if(!job->Finished())return false;const auto e=job->Error();job.reset();Need(e.empty(),e.c_str());}
        if(hosts[0]||hosts[1]){job=NeuralPreparation::RetireHosts(std::move(hosts));return false;}
        return true;
    }
    bool Prepare(const std::array<Spec,2>& r)override {
        const bool matches=Matches(r);
        if(matches&&hosts[0]&&hosts[1])return true;
        if(matches&&preparing&&job&&job->Ready()){
            hosts=job->Adopt();preparing=false;return true;
        }
        if(job&&preparing&&!matches){job->Cancel();preparing=false;}
        if(job){
            if(!job->Finished())return false;
            const auto e=job->Error();job.reset();preparing=false;Need(e.empty(),e.c_str());
        }
        if(hosts[0]||hosts[1]){job=NeuralPreparation::RetireHosts(std::move(hosts));return false;}
        std::array<NeuralRequest,2> requests;
        for(unsigned eye=0;eye<2;++eye){const auto size=r[eye].extent;
            Need(r[eye].viewId==eye+1&&size.width>64&&size.height>64,"Neural region too small or view identity invalid");
            requests[eye]={device.Get(),root,eye,size.width,size.height,size.width,size.height,
                settings.neuralPercent,settings.preset,r[eye].before,settings.appearance,true};
        }
        selected=r;selectedSettings=settings;job=NeuralPreparation::Start(std::move(requests),factory);preparing=true;return false;
    }
    dlss_foveado::d3d11::NeuralRegionReceipt Evaluate(ID3D11DeviceContext* c,const dlss_foveado::d3d11::NeuralRegionInput& in)override {
        Need(c&&in.region.viewId>=1&&in.region.viewId<=2,"Neural regional view invalid");
        const auto eye=unsigned(in.region.viewId-1);Need(hosts[eye]&&in.region==selected[eye],"Neural regional generation unavailable");
        const NeuralEvaluation request{in.color,in.depth,in.motion,in.destination,0,0,
            float(in.jitter.x),float(in.jitter.y),in.reset,0,in.frameTimeMs};
        Need(hosts[eye]->Evaluate(request)>0,"Neural regional evaluation not verified");
        return {in.epoch,in.frame,in.region.viewId,++serial,in.region.extent};
    }
};
class FoveationAdapter {
    using Config=dlss_foveado::Config;
    using Plan=dlss_foveado::FramePlan;
    using Pipeline=dlss_foveado::d3d11::FixedPipeline;
    using Output=dlss_foveado::d3d11::EyeOutput;
    template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context;
    std::filesystem::path runtime,cache;
    std::unique_ptr<dlss_foveado::d3d11::ReconstructionBackend> backend;
    std::unique_ptr<Pipeline> pipeline;
    FoveationBackendFactory createBackend;
    FoveationNeural neural;
    Plan plan{};dlss_foveado::d3d11::PipelineSettings settings{};
    struct Guides {Ptr<ID3D11Texture2D> depth,motion;dlss_foveado::Point jitter{0,0};float ms{};bool reset{};};
    std::array<Guides,2> guides;
    std::array<Output,2> outputs;
    unsigned prepared{};bool stopping{};
    static void Need(bool v,const char* e){if(!v)throw std::runtime_error(e);}
public:
    FoveationAdapter(ID3D11Device* d,const std::filesystem::path& r,const std::filesystem::path& log,NeuralFactory f,
        FoveationBackendFactory create=dlss_foveado::d3d11::CreateNgxBackend)
        :device(d),runtime(r),cache(log.parent_path()/L"foveation-cache"),createBackend(create),neural(d,r/L"neural",f){d->GetImmediateContext(&context);}
    bool Prepare(const Config& config,uint64_t revision,uint64_t frame,uint64_t epoch,
        UINT leftW,UINT leftH,UINT rightW,UINT rightH,UINT outputW,UINT outputH,const nvidia_dlss::PanelValues& values){
        Need(!stopping,"foveation retirement pending");
        if(!rightW||!rightH)return false;
        // BL3's decoded views are left then right, with X increasing to image right.
        // The two open oval halves join at the right edge of L / left edge of R.
        const std::array<dlss_foveado::ViewGeometry,2> geometry{{
            {{leftW,leftH},{outputW,outputH},1,dlss_foveado::NasalSide::Right},
            {{rightW,rightH},{outputW,outputH},2,dlss_foveado::NasalSide::Left}}};
        plan=dlss_foveado::Plan(config,geometry,dlss_foveado::FixedPlacement(config),revision,frame,epoch);
        constexpr dlss_foveado::d3d11::Quality quality[]{dlss_foveado::d3d11::Quality::Quality,
            dlss_foveado::d3d11::Quality::Balanced,dlss_foveado::d3d11::Quality::Performance};
        settings={config,values.preset,quality[values.quality],values.mode==4,values.neuralBefore!=0};
        settings.sharpness=values.sharpnessPercent/100.f;
        neural.Settings(values);
        if(settings.neuralEnabled){if(!neural.Prepare(Pipeline::NeuralRegions(plan,settings.neuralBefore)))return false;}
        else if(!neural.Stop())return false;
        if(!backend){
            backend=createBackend(device.Get(),{runtime,cache,"e8e24dfb-0694-4a23-a8c8-133ff60d845f"});
            pipeline=std::make_unique<Pipeline>(device.Get(),*backend);
        }
        prepared=0;outputs={};return true;
    }
    void PrepareEye(unsigned eye,ID3D11Texture2D* depth,ID3D11Texture2D* motion,
        float jitterX,float jitterY,float ms,bool reset,UINT w,UINT h){
        Need(pipeline&&eye<2&&prepared==eye&&plan.views[eye].geometry.input==dlss_foveado::Extent{w,h},"foveation guide order/extent mismatch");
        if(!eye)pipeline->Begin(plan,settings,settings.neuralEnabled?&neural:nullptr);
        guides[eye]={depth,motion,{jitterX,jitterY},ms,reset||(!eye?false:guides[0].reset)};++prepared;
    }
    ID3D11Texture2D* Evaluate(ID3D11DeviceContext* c,unsigned eye,ID3D11Texture2D* color,UINT x,UINT y){
        Need(pipeline&&eye<2&&prepared>eye,"foveation guides unavailable");
        const auto& g=guides[eye];
        outputs[eye]=pipeline->Evaluate(c,eye,{color,g.depth.Get(),g.motion.Get(),x,y,g.jitter,g.ms,g.reset});
        Need(pipeline->CanPublish(outputs[eye]),"foveation publication receipt rejected");return outputs[eye].image.texture.Get();
    }
    void Finish(ID3D11DeviceContext* c,ID3D11Texture2D* scene,UINT w,UINT h){
        Need(pipeline&&pipeline->Complete()&&pipeline->CanPublish(outputs[0])&&pipeline->CanPublish(outputs[1]),"incomplete foveated pair");
        pipeline->DrawIndicators(c,scene,{{{0,0,w,h},{w,0,w,h}}});
    }
    void Abandon()noexcept{if(pipeline)pipeline->Abandon();outputs={};prepared=0;}
    bool Stop(){
        stopping=true;Abandon();const bool nr=neural.Stop();
        const bool sr=!backend||backend->Stop(context.Get());
        if(!nr||!sr)return false;
        pipeline.reset();backend.reset();guides={};stopping=false;return true;
    }
    bool Present()const{return backend!=nullptr;}
    std::string Details()const{
        std::string text="Motor "+std::string(dlss_foveado::EngineVersion)+" / contrato "+std::to_string(dlss_foveado::ContractVersion);
        if(!backend)return text;
        for(unsigned eye=0;eye<2;++eye){const auto& v=plan.views[eye];
            text+=" | "+std::string(eye?"R":"L")+" centro "+std::to_string(v.central.inputCrop.width)+"x"+std::to_string(v.central.inputCrop.height)+
                " -> "+std::to_string(v.central.reconstruction.width)+"x"+std::to_string(v.central.reconstruction.height)+
                "; periferia "+std::to_string(v.peripheral.width)+"x"+std::to_string(v.peripheral.height);
        }
        return text;
    }
};
} // namespace b3::temporal
