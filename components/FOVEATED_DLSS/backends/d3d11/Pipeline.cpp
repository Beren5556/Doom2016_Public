#include <dlss_foveado/Pipeline.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dlss_foveado::d3d11 {
namespace {
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Rect full(Extent e){return {0,0,e.width,e.height};}
Rect outputCrop(const ViewPlan& v) {
    const auto r=v.neural.inputCrop;
    const double x=double(v.geometry.output.width)/v.geometry.input.width, y=double(v.geometry.output.height)/v.geometry.input.height;
    const uint32_t left=uint32_t(std::floor(r.x*x)),top=uint32_t(std::floor(r.y*y));
    const uint32_t right=std::min(v.geometry.output.width,uint32_t(std::ceil((r.x+r.width)*x)));
    const uint32_t bottom=std::min(v.geometry.output.height,uint32_t(std::ceil((r.y+r.height)*y)));
    return {left,top,right-left,bottom-top};
}
RegionPlan afterRegion(const ViewPlan& v) {
    auto r=v.neural;const auto rect=outputCrop(v);r.inputCrop=rect;r.reconstruction={rect.width,rect.height};
    r.cropScale={1./rect.width,1./rect.height};r.cropBias={-double(rect.x)/rect.width,-double(rect.y)/rect.height};return r;
}
PipelineSettings resourceSettings(PipelineSettings v) {
    v.config.showRegion=false;v.config.indicatorRegions=0;v.sharpness=0;
    if(v.dynamic)experimental::Write(v.config.central,{0,0,0,0});
    return v;
}
}
struct FixedPipeline::State {
    struct Central {Image color,depth,motion,output;bool warm{};};
    struct Scratch {
        Image color;
        std::array<Central,3> central;
        std::array<RegionPlan,3> regions;
        std::array<unsigned,3> aliases{0,1,2};
        unsigned selected{},blendFrames{};int outgoing{-1};
        Image peripheralColor,peripheralDepth,peripheralMotion,peripheralOutput;
        Image composed,neuralColor,neuralDepth,neuralMotion,neuralOutput,neuralComposed;
        Image sharpened;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> published;
        uint64_t ticket{};
    };
    Operations operations;ReconstructionBackend& backend;
    std::array<Scratch,2> eyes;
    FramePlan plan{};PipelineSettings settings{};NeuralRegionBackend* neural{};
    uint64_t lastFrame{},lastEpoch{},serial{},indicatorFrame{},indicatorEpoch{};
    unsigned evaluated{};bool valid{},open{},resetPair{};
    State(ID3D11Device* d,ReconstructionBackend& b):operations(d),backend(b){}
    void receipt(const ReconstructionReceipt& r,const ReconstructionInput& input) {
        need(r.ticket&&r.epoch==input.epoch&&r.frame==input.frame&&r.viewId==input.feature.viewId&&
            r.branch==input.feature.branch&&r.input==input.feature.input&&r.output==input.feature.output,
            "foveated reconstruction receipt mismatch");
    }
    void prepareGuides(ID3D11DeviceContext* c,const EyeInput& input,Rect crop,Extent extent,Image& depth,Image& motion,Point scale={1,1}){
        operations.Ensure(depth,extent,DXGI_FORMAT_R32_FLOAT);operations.Ensure(motion,extent,DXGI_FORMAT_R32G32_FLOAT);
        operations.Resample(c,input.depth,crop,depth,true);operations.Resample(c,input.motion,crop,motion,true,scale);
    }
    NeuralRegionReceipt evaluateNeural(ID3D11DeviceContext* c,unsigned eye,const EyeInput& in,ID3D11Texture2D* color,bool before) {
        auto& e=eyes[eye];const auto& v=plan.views[eye];const auto region=before?v.neural:afterRegion(v);const auto r=region.inputCrop;
        const Extent size{r.width,r.height};
        operations.Ensure(e.neuralColor,size);operations.Ensure(e.neuralOutput,size);
        operations.Resample(c,color,r,e.neuralColor);
        if(before)prepareGuides(c,in,r,size,e.neuralDepth,e.neuralMotion);
        else {
            const auto input=v.geometry.input,output=v.geometry.output;
            operations.Ensure(e.neuralDepth,size,DXGI_FORMAT_R32_FLOAT);operations.Ensure(e.neuralMotion,size,DXGI_FORMAT_R32G32_FLOAT);
            operations.GuideCrop(c,in.depth,output,r,e.neuralDepth);
            operations.GuideCrop(c,in.motion,output,r,e.neuralMotion,{double(output.width)/input.width,double(output.height)/input.height});
        }
        NeuralRegionInput request;request.region={v.geometry.viewId,size,before};request.epoch=plan.epoch;request.frame=plan.frame;
        request.color=e.neuralColor.texture.Get();request.depth=e.neuralDepth.texture.Get();request.motion=e.neuralMotion.texture.Get();
        request.destination=e.neuralOutput.texture.Get();request.frameTimeMs=in.frameTimeMs;request.reset=in.reset||resetPair;
        request.jitter=before?in.jitter:Point{in.jitter.x*v.geometry.output.width/v.geometry.input.width,in.jitter.y*v.geometry.output.height/v.geometry.input.height};
        const auto result=neural->Evaluate(c,request);
        need(result.ticket&&result.epoch==request.epoch&&result.frame==request.frame&&result.viewId==request.region.viewId&&result.extent==size,
            "regional Neural receipt mismatch");
        const auto target=before?v.geometry.input:v.geometry.output;
        operations.Ensure(e.neuralComposed,target);
        const Point scale=before?Point{double(v.geometry.output.width)/target.width,double(v.geometry.output.height)/target.height}:Point{1,1};
        operations.Composite(c,color,e.neuralOutput.texture.Get(),region,e.neuralComposed,true,scale);
        return result;
    }
};
FixedPipeline::FixedPipeline(ID3D11Device* d,ReconstructionBackend& backend):state(std::make_unique<State>(d,backend)){}
FixedPipeline::~FixedPipeline()=default;
std::array<NeuralRegionSpec,2> FixedPipeline::NeuralRegions(const FramePlan& plan,bool before){
    std::array<NeuralRegionSpec,2> result;
    for(unsigned eye=0;eye<2;++eye){const auto& v=plan.views[eye];const auto r=before?v.neural.inputCrop:outputCrop(v);
        result[eye]={v.geometry.viewId,{r.width,r.height},before};}
    return result;
}
void FixedPipeline::Begin(FramePlan plan,PipelineSettings settings,NeuralRegionBackend* neural){
    const bool priorOpen=state->open;Abandon();
    need(std::isfinite(settings.sharpness)&&settings.sharpness>=0&&settings.sharpness<=1,"invalid full-output sharpness");
    auto& s=*state;
    need(!priorOpen&&plan.version==ContractVersion&&plan.config.Valid()&&plan.config.enabled&&plan.config==settings.config,"invalid foveated pair start");
    need(plan.epoch&&plan.frame&&plan.configRevision&&plan.epoch>=s.lastEpoch&&
        (plan.epoch!=s.lastEpoch||plan.frame>s.lastFrame),"stale foveated pair");
    if(s.lastEpoch && plan.epoch==s.lastEpoch && (plan.config.geometryMode==GeometryMode::BinocularFourSides ||
        s.plan.config.geometryMode==GeometryMode::BinocularFourSides)) {
        auto previous=s.plan.config, current=plan.config;
        previous.showRegion=current.showRegion=false; previous.indicatorRegions=current.indicatorRegions=0;
        if(settings.dynamic&&s.settings.dynamic){
            experimental::Write(previous.central,{0,0,0,0});
            experimental::Write(current.central,{0,0,0,0});
        }
        need(previous==current && plan.placementRevision==s.plan.placementRevision &&
            plan.views[0].geometry==s.plan.views[0].geometry && plan.views[1].geometry==s.plan.views[1].geometry &&
            settings.neuralEnabled==s.settings.neuralEnabled && settings.neuralBefore==s.settings.neuralBefore &&
            settings.inheritedPreset==s.settings.inheritedPreset && settings.centralQuality==s.settings.centralQuality &&
            settings.centralDlaa==s.settings.centralDlaa && settings.dynamic==s.settings.dynamic,
            "changed binocular configuration/projection requires a new epoch");
    }
    const auto caps=s.backend.Capabilities();need(caps.contractVersion==ContractVersion&&caps.ldrSuperResolution,"incompatible reconstruction backend");
    need(settings.config.periphery!=Periphery::Temporal||caps.ldrNativeAA,"backend has no peripheral LDR AA");
    need(!settings.neuralEnabled||neural,"regional Neural provider unavailable");
    need(!settings.dynamic||!settings.neuralEnabled,"dynamic foveation is unavailable with Neural");
    if(settings.dynamic){experimental::Settings limits;limits.mode=experimental::Mode::Dynamic;limits.inner=settings.inner;limits.outer=settings.outer;
        need(limits.Valid(),"invalid dynamic limits");const auto cuts=experimental::Read(plan.config.central);
        for(unsigned i=0;i<4;++i)need(cuts[i]>=limits.outer[i]&&cuts[i]<=limits.inner[i],"dynamic region outside configured bounds");}
    if(settings.centralDlaa){
        need(caps.ldrNativeAA&&settings.config.periphery==Periphery::Temporal&&settings.config.peripheralScaleBps<10000,"native central AA needs temporal SR periphery below 100 percent");
        for(const auto& v:plan.views)need(v.geometry.input==v.geometry.output,"central DLAA requires observed native scene input");
    }
    const bool sameResources=s.lastEpoch&&resourceSettings(settings)==resourceSettings(s.settings)&&
        plan.views[0].geometry==s.plan.views[0].geometry&&plan.views[1].geometry==s.plan.views[1].geometry;
    const bool continuous=sameResources&&plan.epoch==s.lastEpoch&&plan.frame==s.lastFrame+1;
    const std::array<unsigned,2> previousSelected{s.eyes[0].selected,s.eyes[1].selected};
    if(!sameResources)for(auto& eye:s.eyes)for(auto& central:eye.central)central.warm=false;
    if(settings.dynamic){
        need(caps.maximumContexts>=8,"Dynamic requires eight prepared reconstruction contexts");
        const auto cuts=experimental::Read(plan.config.central);
        const std::array<ViewGeometry,2> views{plan.views[0].geometry,plan.views[1].geometry};
        for(unsigned slot=0;slot<3;++slot){
            auto c=plan.config;experimental::Cuts level{};
            for(unsigned i=0;i<4;++i)level[i]=slot==0?settings.outer[i]:slot==2?settings.inner[i]:
                ((settings.outer[i]+settings.inner[i]+100)/200)*100;
            experimental::Write(c.central,level);
            const auto prepared=Plan(c,views,FixedPlacement(c),plan.configRevision,plan.frame,plan.epoch);
            bool contains=true;for(unsigned i=0;i<4;++i)contains=contains&&cuts[i]>=level[i];
            for(unsigned eye=0;eye<2;++eye){s.eyes[eye].regions[slot]=prepared.views[eye].central;
                if(slot==0||contains)s.eyes[eye].selected=slot;}
        }
        for(unsigned i=0;i<2;++i){auto& eye=s.eyes[i];
            eye.aliases={0,1,2};
            for(unsigned slot=1;slot<3;++slot)for(unsigned previous=0;previous<slot;++previous)
                if(eye.regions[slot].inputCrop==eye.regions[previous].inputCrop&&eye.regions[slot].reconstruction==eye.regions[previous].reconstruction){eye.aliases[slot]=eye.aliases[previous];break;}
            eye.selected=eye.aliases[eye.selected];
            if(!continuous){eye.outgoing=-1;eye.blendFrames=0;}
            else if(eye.selected!=previousSelected[i]){
                // A quick reversal keeps the already converged visible context.
                const auto visible=eye.outgoing>=0&&eye.blendFrames<8?unsigned(eye.outgoing):previousSelected[i];
                eye.outgoing=visible==eye.selected?-1:int(visible);eye.blendFrames=0;
            }
        }
    }else for(unsigned eye=0;eye<2;++eye){s.eyes[eye].aliases={0,0,0};s.eyes[eye].selected=0;s.eyes[eye].regions[0]=plan.views[eye].central;}
    s.resetPair=plan.epoch!=s.lastEpoch;
    s.plan=std::move(plan);s.settings=std::move(settings);s.neural=neural;s.lastFrame=s.plan.frame;s.lastEpoch=s.plan.epoch;
    s.evaluated=0;s.valid=s.open=true;for(auto& eye:s.eyes)eye.ticket=0;
}
EyeOutput FixedPipeline::Evaluate(ID3D11DeviceContext* context,unsigned eye,const EyeInput& input){
    auto& s=*state;
    try {
        need(s.valid&&s.open&&eye<2&&s.evaluated==eye,"foveated stereo order mismatch");
        auto& e=s.eyes[eye];const auto& v=s.plan.views[eye];const auto& config=s.plan.config;
        if(input.reset||s.resetPair)e.outgoing=-1;
        EyeOutput result{};result.eye=eye;result.epoch=s.plan.epoch;result.frame=s.plan.frame;result.configRevision=s.plan.configRevision;
        s.operations.Ensure(e.color,v.geometry.input);
        s.operations.Resample(context,input.color,{input.colorX,input.colorY,v.geometry.input.width,v.geometry.input.height},e.color);
        ID3D11Texture2D* color=e.color.texture.Get();
        if(s.settings.neuralEnabled&&s.settings.neuralBefore){result.neural=s.evaluateNeural(context,eye,input,color,true);color=e.neuralComposed.texture.Get();}
        // Prepare every bounded variant once, using real inputs from this eye.
        // Subsequent frames evaluate only the smallest prepared crop containing
        // the visible mask. The mask keeps the original binocular geometry.
        auto centralEvaluate=[&](unsigned slot){
            auto& central=e.central[slot];const auto& region=e.regions[slot];const auto crop=region.inputCrop;
            const Extent size=s.settings.centralDlaa?region.reconstruction:Extent{crop.width,crop.height};
            const Point scale{double(size.width)/crop.width,double(size.height)/crop.height};
            s.operations.Ensure(central.color,size);s.operations.Ensure(central.output,region.reconstruction);
            s.operations.Resample(context,color,crop,central.color);
            s.prepareGuides(context,input,crop,size,central.depth,central.motion,scale);
            ReconstructionInput r;r.feature={v.geometry.viewId,Branch::Central,size,region.reconstruction,
                config.centralPreset?config.centralPreset:s.settings.inheritedPreset,s.settings.centralDlaa?Quality::Native:s.settings.centralQuality,true,true,false};
            r.epoch=s.plan.epoch;r.frame=s.plan.frame;r.frameTimeMs=input.frameTimeMs;r.jitter={input.jitter.x*scale.x,input.jitter.y*scale.y};
            r.reset=input.reset||s.resetPair||!central.warm;r.preparedSlot=slot;
            r.color=central.color.texture.Get();r.depth=central.depth.texture.Get();r.motion=central.motion.texture.Get();r.destination=central.output.texture.Get();
            const auto receipt=s.backend.Evaluate(context,r);s.receipt(receipt,r);central.warm=true;return receipt;
        };
        bool selectedEvaluated=false;
        if(s.settings.dynamic)for(unsigned slot=0;slot<3;++slot)if(e.aliases[slot]==slot&&!e.central[slot].warm){
            const auto r=centralEvaluate(slot);if(slot==e.selected){result.reconstruction[0]=r;selectedEvaluated=true;}}
        if(!selectedEvaluated)result.reconstruction[0]=centralEvaluate(e.selected);
        // During a switch, keep the outgoing context current with THIS frame's
        // eye inputs. Never display cached pixels or reuse history across gaps.
        if(e.outgoing>=0)centralEvaluate(unsigned(e.outgoing));
        ReconstructionInput request;request.epoch=s.plan.epoch;request.frame=s.plan.frame;request.frameTimeMs=input.frameTimeMs;request.reset=input.reset||s.resetPair;
        s.operations.Ensure(e.peripheralColor,v.peripheral);s.operations.Resample(context,color,full(v.geometry.input),e.peripheralColor);
        ID3D11Texture2D* peripheral=e.peripheralColor.texture.Get();
        if(config.periphery==Periphery::Temporal){
            const Point scale{double(v.peripheral.width)/v.geometry.input.width,double(v.peripheral.height)/v.geometry.input.height};
            s.prepareGuides(context,input,full(v.geometry.input),v.peripheral,e.peripheralDepth,e.peripheralMotion,scale);
            const auto peripheralOutput=s.settings.centralDlaa?v.geometry.output:v.peripheral;
            s.operations.Ensure(e.peripheralOutput,peripheralOutput);
            request.feature={v.geometry.viewId,Branch::Peripheral,v.peripheral,peripheralOutput,config.peripheralPreset,s.settings.centralDlaa?s.settings.centralQuality:Quality::Native,true,true,false};
            request.color=e.peripheralColor.texture.Get();request.depth=e.peripheralDepth.texture.Get();request.motion=e.peripheralMotion.texture.Get();
            request.destination=e.peripheralOutput.texture.Get();request.jitter={input.jitter.x*scale.x,input.jitter.y*scale.y};
            result.reconstruction[1]=s.backend.Evaluate(context,request);s.receipt(result.reconstruction[1],request);
            peripheral=e.peripheralOutput.texture.Get();
        }
        auto composite=v.central;const auto& prepared=e.regions[e.selected];
        composite.inputCrop=prepared.inputCrop;composite.reconstruction=prepared.reconstruction;
        composite.cropScale=prepared.cropScale;composite.cropBias=prepared.cropBias;
        s.operations.Ensure(e.composed,v.geometry.output);s.operations.Composite(context,peripheral,e.central[e.selected].output.texture.Get(),composite,e.composed);
        if(e.outgoing>=0){
            const auto outgoing=unsigned(e.outgoing);const auto& old=e.regions[outgoing];
            // The smaller nested mask bounds the overlap; sampling still uses
            // the outgoing crop. Neither the visible mask nor indicators move.
            auto overlap=outgoing>e.selected?old:v.central;
            overlap.inputCrop=old.inputCrop;overlap.reconstruction=old.reconstruction;
            overlap.cropScale=old.cropScale;overlap.cropBias=old.cropBias;
            ++e.blendFrames;
            const float opacity=e.blendFrames<=8?1.f:std::max(0.f,1.f-float(e.blendFrames-8)/8.f);
            if(opacity>0)s.operations.BlendRegion(context,e.central[outgoing].output.texture.Get(),overlap,e.composed,opacity);
            if(e.blendFrames>=16)e.outgoing=-1;
        }
        result.image=e.composed;
        if(s.settings.neuralEnabled&&!s.settings.neuralBefore){
            result.neural=s.evaluateNeural(context,eye,input,e.composed.texture.Get(),false);result.image=e.neuralComposed;
        }
        if(s.settings.sharpness>0){
            s.operations.Ensure(e.sharpened,v.geometry.output);s.operations.Sharpen(context,result.image.texture.Get(),e.sharpened,s.settings.sharpness);result.image=e.sharpened;
        }
        result.ticket=e.ticket=++s.serial;e.published=result.image.texture;
        if(++s.evaluated==2)s.open=false;
        return result;
    }catch(...){Abandon();throw;}
}
bool FixedPipeline::CanPublish(const EyeOutput& r) const noexcept{
    const auto& s=*state;return s.valid&&r.eye<2&&r.ticket&&r.ticket==s.eyes[r.eye].ticket&&
        r.epoch==s.plan.epoch&&r.frame==s.plan.frame&&r.configRevision==s.plan.configRevision&&r.image.texture&&r.image.texture==s.eyes[r.eye].published;
}
bool FixedPipeline::Complete()const noexcept{return state->valid&&state->evaluated==2&&!state->open;}
void FixedPipeline::Abandon()noexcept{state->valid=state->open=false;for(auto& eye:state->eyes)eye.ticket=0;}
const FramePlan& FixedPipeline::CurrentPlan()const{return state->plan;}
bool FixedPipeline::DynamicPrepared()const noexcept{
    if(!state->settings.dynamic)return false;
    for(const auto& eye:state->eyes)for(const auto slot:eye.aliases)if(!eye.central[slot].warm)return false;
    return true;
}
void FixedPipeline::DrawIndicators(ID3D11DeviceContext* c,ID3D11Texture2D* scene,const std::array<Rect,2>& viewports){
    auto& s=*state;need(Complete(),"indicator requires a complete foveated pair");
    if(!s.plan.config.showRegion||(s.indicatorFrame==s.plan.frame&&s.indicatorEpoch==s.plan.epoch))return;
    if(s.settings.dynamic){
        auto min=s.plan.config,max=s.plan.config;experimental::Write(min.central,s.settings.inner);experimental::Write(max.central,s.settings.outer);
        const std::array<ViewGeometry,2> views{s.plan.views[0].geometry,s.plan.views[1].geometry};
        const auto inner=Plan(min,views,FixedPlacement(min),s.plan.configRevision,s.plan.frame,s.plan.epoch);
        const auto outer=Plan(max,views,FixedPlacement(max),s.plan.configRevision,s.plan.frame,s.plan.epoch);
        for(unsigned eye=0;eye<2;++eye){
            s.operations.ColoredIndicator(c,scene,viewports[eye],inner.views[eye].central,{1,0,0,.9f},2.5f);
            s.operations.ColoredIndicator(c,scene,viewports[eye],outer.views[eye].central,{1,0,0,.9f},2.5f);
            s.operations.ColoredIndicator(c,scene,viewports[eye],s.plan.views[eye].central,{0,1,0,.95f},1.25f);
        }
        s.indicatorFrame=s.plan.frame;s.indicatorEpoch=s.plan.epoch;return;
    }
    for(unsigned eye=0;eye<2;++eye){
        if(s.plan.config.indicatorRegions!=1)s.operations.Indicator(c,scene,viewports[eye],s.plan.views[eye].central);
        if(s.settings.neuralEnabled&&s.plan.config.indicatorRegions!=0)s.operations.Indicator(c,scene,viewports[eye],s.plan.views[eye].neural,true);
    }
    s.indicatorFrame=s.plan.frame;s.indicatorEpoch=s.plan.epoch;
}
} // namespace dlss_foveado::d3d11
