#include <dlss_foveado/Reconstruction.h>
#include <dlss_foveado/MemoryPressure.h>
#include <d3d11_4.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_d3d.h>
#include <atomic>
#include <cmath>
#include <deque>
#include <map>
#include <tuple>
#include <dxgi1_4.h>
#include <stdexcept>

namespace dlss_foveado::d3d11 {
template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
namespace {
std::atomic<bool> owned{};
void need(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
void hr(HRESULT h, const char* what) { need(SUCCEEDED(h), what); }
void ngx(NVSDK_NGX_Result result, const char* what) {
    if (NVSDK_NGX_FAILED(result)) throw std::runtime_error(std::string(what) + ": NGX " + std::to_string(uint32_t(result)));
}
NVSDK_NGX_PerfQuality_Value quality(Quality q) {
    switch(q) {
    case Quality::Native: return NVSDK_NGX_PerfQuality_Value_DLAA;
    case Quality::Quality: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case Quality::Balanced: return NVSDK_NGX_PerfQuality_Value_Balanced;
    case Quality::Performance: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case Quality::UltraPerformance: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    default: throw std::invalid_argument("unsupported reconstruction quality");
    }
}
class Backend final : public ReconstructionBackend {
    struct Feature {
        FeatureSpec spec{};
        uint64_t epoch{}, lastFrame{};
        NVSDK_NGX_Handle* handle{};
        NVSDK_NGX_Parameter* parameters{};
        // Retain inputs until a query proves every evaluation is complete.
        std::array<Ptr<ID3D11Texture2D>,4> retained;
    };
    struct Retired { std::unique_ptr<Feature> feature; Ptr<ID3D11Query> completion; };
    Ptr<ID3D11Device> device;
    Ptr<ID3D11DeviceContext1> immediate;
    Ptr<ID3D11Multithread> threading;
    Ptr<ID3DDeviceContextState> isolated;
    NgxOptions options;
    Ptr<IDXGIAdapter3> memory;uint64_t initialNonLocal{};bool haveMemoryBaseline{};
    MemoryPressure pressure;
    NVSDK_NGX_Parameter* capabilities{};
    std::map<std::tuple<uint64_t,Branch,uint32_t>,std::unique_ptr<Feature>> features;
    std::deque<Retired> retired;
    ReconstructionStatistics statistics{};
    uint64_t serial{};
    bool initialized{}, stopping{}, lease{};
    struct Scope {
        Backend& b; Ptr<ID3DDeviceContextState> previous;
        explicit Scope(Backend& value):b(value) {
            b.threading->Enter(); b.immediate->SwapDeviceContextState(b.isolated.Get(),&previous); b.immediate->ClearState();
        }
        ~Scope(){b.immediate->ClearState(); b.immediate->SwapDeviceContextState(previous.Get(),nullptr); b.threading->Leave();}
    };
    void validateContext(ID3D11DeviceContext* context) {
        Ptr<ID3D11DeviceContext> canonical; hr(immediate.As(&canonical),"canonical immediate context");
        need(context==canonical.Get(),"reconstruction must use its device's immediate context");
    }
    void initialize() {
        if(initialized)return;
        const std::wstring path=options.runtimeDirectory.wstring(); const wchar_t* paths[]{path.c_str()};
        NVSDK_NGX_FeatureCommonInfo common{}; common.PathListInfo.Path=paths; common.PathListInfo.Length=1;
        ngx(NVSDK_NGX_D3D11_Init_with_ProjectID(options.projectId.c_str(),NVSDK_NGX_ENGINE_TYPE_CUSTOM,
            "DLSS_FOVEADO-0.1",options.cacheDirectory.c_str(),device.Get(),&common),"foveated NGX initialization");
        initialized=true;
        ngx(NVSDK_NGX_D3D11_GetCapabilityParameters(&capabilities),"foveated NGX capability parameters");
        int available{}; ngx(capabilities->Get(NVSDK_NGX_Parameter_SuperSampling_Available,&available),"foveated SR capability");
        need(available!=0,"DLSS Super Resolution unavailable");
    }
    void release(std::unique_ptr<Feature>& f) {
        if(!f)return;
        if(f->handle){ngx(NVSDK_NGX_D3D11_ReleaseFeature(f->handle),"release foveated feature");f->handle=nullptr;++statistics.released;}
        if(f->parameters){ngx(NVSDK_NGX_D3D11_DestroyParameters(f->parameters),"release foveated parameters");f->parameters=nullptr;}
        f.reset();
    }
    void retire(std::unique_ptr<Feature>& f) {
        if(!f)return;
        Ptr<ID3D11Query> query; const D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};
        hr(device->CreateQuery(&desc,&query),"foveation retirement query"); immediate->End(query.Get());
        retired.push_back({std::move(f),query});
    }
    void poll() {
        while(!retired.empty()) {
            const auto done=immediate->GetData(retired.front().completion.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(done==S_FALSE)break;hr(done,"foveation retirement device failure");
            release(retired.front().feature);retired.pop_front();
        }
    }
    void memoryBudget(bool allocating=false){
        if(!options.budgetReservePercent||!memory)return;
        DXGI_QUERY_VIDEO_MEMORY_INFO local{},shared{};
        if(FAILED(memory->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local)))local={};
        const bool sharedKnown=haveMemoryBaseline&&SUCCEEDED(memory->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&shared));
        const auto result=pressure.Check(GetTickCount64(),local.Budget,local.CurrentUsage,sharedKnown,
            shared.CurrentUsage,initialNonLocal,allocating);
        if(result!=MemoryPressure::Result::None){
            const auto reason=result==MemoryPressure::Result::LocalBudget?"GPU memory budget exceeded":"GPU allocations paging to system memory";
            throw std::runtime_error(std::string("Experimental foveation: ")+reason+
                " (local used/budget MiB="+std::to_string(local.CurrentUsage/(1024*1024))+"/"+std::to_string(local.Budget/(1024*1024))+
                ", shared MiB="+std::to_string(shared.CurrentUsage/(1024*1024))+
                "). Reduce resolution or close other GPU workloads, then Apply.");
        }
    }
    void texture(ID3D11Texture2D* value, Extent extent, unsigned kind) {
        need(value!=nullptr,"missing reconstruction input"); Ptr<ID3D11Device> owner;value->GetDevice(&owner);
        need(owner.Get()==device.Get(),"reconstruction input device mismatch");
        D3D11_TEXTURE2D_DESC d{};value->GetDesc(&d);
        need(d.Width==extent.width&&d.Height==extent.height&&d.MipLevels==1&&d.ArraySize==1&&d.SampleDesc.Count==1,
            "reconstruction input extent mismatch");
        need((d.BindFlags&D3D11_BIND_SHADER_RESOURCE)!=0,"reconstruction SRV binding missing");
        if(kind==3)need((d.BindFlags&D3D11_BIND_UNORDERED_ACCESS)!=0,"reconstruction output UAV binding missing");
        need(kind==1?d.Format==DXGI_FORMAT_R32_FLOAT:kind==2?(d.Format==DXGI_FORMAT_R16G16_FLOAT||d.Format==DXGI_FORMAT_R32G32_FLOAT):
            d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT,"reconstruction format mismatch");
    }
public:
    Backend(ID3D11Device* d,NgxOptions paths):device(d),options(std::move(paths)) {
        need(options.budgetReservePercent<100,"invalid memory reserve");
        need(d&&d->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_0,"foveated NGX device unsupported");
        need(options.runtimeDirectory.is_absolute()&&options.cacheDirectory.is_absolute()&&!options.projectId.empty(),"NGX paths/project must be explicit");
        need(std::filesystem::is_regular_file(options.runtimeDirectory/L"nvngx_dlss.dll"),"declared NGX runtime not found");
        Ptr<ID3D11DeviceContext> ctx;d->GetImmediateContext(&ctx);hr(ctx.As(&immediate),"NGX context1");hr(ctx.As(&threading),"NGX multithread interface");
        Ptr<ID3D11Device1> d1;hr(device.As(&d1),"NGX device1");D3D_FEATURE_LEVEL level=d->GetFeatureLevel(),selected{};
        hr(d1->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&selected,&isolated),"NGX isolated context state");
        threading->SetMultithreadProtected(TRUE);
        if(options.budgetReservePercent){
            Ptr<IDXGIDevice> dxgi;Ptr<IDXGIAdapter> adapter;
            if(SUCCEEDED(device.As(&dxgi))&&SUCCEEDED(dxgi->GetAdapter(&adapter))&&SUCCEEDED(adapter.As(&memory))){
                DXGI_QUERY_VIDEO_MEMORY_INFO shared{};
                haveMemoryBaseline=SUCCEEDED(memory->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&shared));
                initialNonLocal=shared.CurrentUsage;
            }
        }
        bool vacant=false;need(owned.compare_exchange_strong(vacant,true),"foveated NGX already has an owner");lease=true;
    }
    ~Backend() override {
        // Host must call Stop outside loader lock. On contract violation retain
        // feature/resource ownership instead of releasing GPU work prematurely.
        if(initialized||!features.empty()||!retired.empty()) {
            for(auto& item:features)(void)item.second.release();
            for(auto& item:retired)(void)item.feature.release();
        } else if(lease)owned.store(false);
    }
    BackendCapabilities Capabilities() const noexcept override { return {ContractVersion,true,true,false,8,32,16384}; }
    ReconstructionStatistics Statistics() const noexcept override {return statistics;}
    ReconstructionReceipt Evaluate(ID3D11DeviceContext* context,const ReconstructionInput& in) override {
        validateContext(context);need(!stopping&&in.frame&&in.epoch,"invalid reconstruction frame/lifecycle");
        need(std::isfinite(in.frameTimeMs)&&in.frameTimeMs>0&&in.frameTimeMs<=1000&&
            std::isfinite(in.preExposure)&&in.preExposure>0&&std::isfinite(in.exposureScale)&&in.exposureScale>0,
            "invalid reconstruction timing/exposure");
        need(std::isfinite(in.jitter.x)&&std::isfinite(in.jitter.y)&&std::abs(in.jitter.x)<=2&&std::abs(in.jitter.y)<=2,"invalid reconstruction jitter");
        const auto& spec=in.feature; (void)quality(spec.quality);
        need(spec.preset==0||(spec.preset>=10&&spec.preset<=13),"unsupported preset");
        need(spec.input.width>=32&&spec.input.height>=32&&spec.output.width>=spec.input.width&&spec.output.height>=spec.input.height&&
            spec.output.width<=16384&&spec.output.height<=16384,"invalid reconstruction extents");
        need(spec.quality==Quality::Native ? spec.input==spec.output :
            (spec.input.width<spec.output.width&&spec.input.height<spec.output.height),"quality/geometry mismatch");
        texture(in.color,spec.input,0);texture(in.depth,spec.input,1);texture(in.motion,spec.input,2);texture(in.destination,spec.output,3);
        need(in.destination!=in.color&&in.destination!=in.depth&&in.destination!=in.motion,"reconstruction output aliases input");
        Scope scope(*this);poll();initialize();memoryBudget();
        need(in.preparedSlot<3&&(spec.branch==Branch::Central||in.preparedSlot==0),"invalid prepared reconstruction slot");
        const auto key=std::make_tuple(spec.viewId,spec.branch,in.preparedSlot);
        auto found=features.find(key);
        if(found==features.end()){need(features.size()<8,"too many reconstruction contexts");found=features.emplace(key,nullptr).first;}
        auto& f=found->second;
        need(!f||in.epoch>=f->epoch,"stale reconstruction epoch");
        if(f&&f->spec!=spec) {need(retired.size()<16,"foveation resources still retiring");retire(f);}
        // Epoch-only changes invalidate history, not geometry. Reuse the context
        // (notably the unchanged peripheral branch) and explicitly reset below.
        const bool changedEpoch=f&&f->epoch!=in.epoch;
        if(!f) {
            memoryBudget(true); // Do not add an NGX context under actual pressure.
            f=std::make_unique<Feature>();f->spec=spec;f->epoch=in.epoch;
            ngx(NVSDK_NGX_D3D11_AllocateParameters(&f->parameters),"foveated feature parameters");
            if(spec.preset)for(const char* name:{NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance})f->parameters->Set(name,spec.preset);
            NVSDK_NGX_DLSS_Create_Params create{};create.Feature.InWidth=spec.input.width;create.Feature.InHeight=spec.input.height;
            create.Feature.InTargetWidth=spec.output.width;create.Feature.InTargetHeight=spec.output.height;
            create.Feature.InPerfQualityValue=quality(spec.quality);
            create.InFeatureCreateFlags=NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
            if(!spec.encodedLdr)create.InFeatureCreateFlags|=NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
            if(spec.depthInverted)create.InFeatureCreateFlags|=NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
            if(spec.motionJittered)create.InFeatureCreateFlags|=NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
            const auto created=NGX_D3D11_CREATE_DLSS_EXT(immediate.Get(),&f->handle,f->parameters,&create);
            if(NVSDK_NGX_FAILED(created)){retire(f);ngx(created,"create foveated reconstruction context");}
            ++statistics.created;
            memoryBudget();
            for(const auto& other:features)if(other.second&&other.second.get()!=f.get()&&other.second->handle==f->handle){
                f->handle=nullptr;throw std::runtime_error("NGX shared a temporal feature across eyes/branches");
            }
            for(const auto& other:retired)if(other.feature&&other.feature->handle==f->handle){
                f->handle=nullptr;throw std::runtime_error("NGX reused a feature before its GPU retirement");
            }
        }
        need(f->handle&&(changedEpoch||in.frame>f->lastFrame),"duplicate/out-of-order reconstruction frame");
        // Texture geometry is generation-stable. Host retains all frame images
        // for a generation; references here also protect the latest submission.
        f->retained={in.color,in.depth,in.motion,in.destination};
        NVSDK_NGX_D3D11_DLSS_Eval_Params evaluate{};
        evaluate.Feature.pInColor=in.color;evaluate.Feature.pInOutput=in.destination;
        evaluate.pInDepth=in.depth;evaluate.pInMotionVectors=in.motion;
        evaluate.InRenderSubrectDimensions={spec.input.width,spec.input.height};
        evaluate.InJitterOffsetX=float(in.jitter.x);evaluate.InJitterOffsetY=float(in.jitter.y);
        evaluate.InMVScaleX=evaluate.InMVScaleY=1;
        evaluate.InPreExposure=in.preExposure;evaluate.InExposureScale=in.exposureScale;evaluate.InFrameTimeDeltaInMsec=in.frameTimeMs;
        evaluate.InReset=in.reset||changedEpoch||f->lastFrame==0||in.frame!=f->lastFrame+1;
        const auto result=NGX_D3D11_EVALUATE_DLSS_EXT(immediate.Get(),f->handle,f->parameters,&evaluate);
        ngx(result,"evaluate foveated reconstruction");hr(device->GetDeviceRemovedReason(),"NGX device removed");
        f->lastFrame=in.frame;f->epoch=in.epoch;
        ++statistics.evaluated;
        return {++serial,in.epoch,in.frame,spec.viewId,spec.branch,spec.input,spec.output,uint32_t(result)};
    }
    bool Stop(ID3D11DeviceContext* context) override {
        validateContext(context);Scope scope(*this);
        if(!stopping){for(auto& item:features)retire(item.second);features.clear();stopping=true;immediate->Flush();}
        poll();if(!retired.empty())return false;
        if(capabilities){ngx(NVSDK_NGX_D3D11_DestroyParameters(capabilities),"destroy NGX capabilities");capabilities=nullptr;}
        if(initialized){ngx(NVSDK_NGX_D3D11_Shutdown1(device.Get()),"shutdown foveated NGX");initialized=false;}
        return true;
    }
};
}
std::unique_ptr<ReconstructionBackend> CreateNgxBackend(ID3D11Device* device,NgxOptions options) {
    return std::make_unique<Backend>(device,std::move(options));
}
} // namespace dlss_foveado::d3d11
