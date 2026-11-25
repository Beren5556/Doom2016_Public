#include "VulkanReconstruction.h"
#include "VulkanFunctions.h"
#include <map>
#include <cmath>
#include <algorithm>

namespace kharvox::dlss {
namespace fv=dlss_foveado;
namespace vkf=fv::vulkan;
namespace {
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
fv::Extent size(VkExtent2D s){return {s.width,s.height};}
VkExtent2D size(fv::Extent s){return {s.width,s.height};}
fv::Rect full(VkExtent2D s){return {0,0,s.width,s.height};}
vkf::Image borrow(const Image& i){return {i.device,i.image,i.view,i.format,size(i.extent),i.range,i.layout,i.usage,i.samples};}
fv::RegionPlan afterRegion(const fv::ViewPlan& v){
    auto r=v.neural;const auto c=r.inputCrop;
    const double sx=double(v.geometry.output.width)/v.geometry.input.width,sy=double(v.geometry.output.height)/v.geometry.input.height;
    const auto x=uint32_t(std::floor(c.x*sx)),y=uint32_t(std::floor(c.y*sy));
    const auto right=std::min(v.geometry.output.width,uint32_t(std::ceil((c.x+c.width)*sx))),bottom=std::min(v.geometry.output.height,uint32_t(std::ceil((c.y+c.height)*sy)));
    r.inputCrop={x,y,right-x,bottom-y};r.reconstruction={right-x,bottom-y};
    r.cropScale={1./r.inputCrop.width,1./r.inputCrop.height};r.cropBias={-double(x)/r.inputCrop.width,-double(y)/r.inputCrop.height};return r;
}
}
struct VulkanReconstruction::State {
    Options options;Functions f;VkPhysicalDeviceMemoryProperties memory{};
    std::unique_ptr<vkf::Operations> operations;
    std::array<std::unique_ptr<VulkanDlss>,5> providers; // SR mid, peripheral, Neural, SR inner/outer
    struct Owned {Image image;VkDeviceMemory allocation{};};
    std::map<std::string,Owned> scratch;
    std::vector<std::unique_ptr<vkf::Recorded>> commands;
    std::array<uint64_t,5> tickets{};
    uint64_t serial{},pending{},lastRevision{},lastFrame{},lastEpoch{};
    bool failed{},resetNext{true};std::string error;
    State(Options o,bool extended):options(std::move(o)){
        f.Load(options.device,options.gdpa);
        const auto query=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(options.gipa(options.instance,"vkGetPhysicalDeviceMemoryProperties"));
        require(query!=nullptr,"Foveation physical memory query missing");query(options.physical,&memory);
        operations=std::make_unique<vkf::Operations>(vkf::Context{options.device,options.gdpa,memory,extended});
    }
    void destroy(Owned& t){if(t.image.view)f.vkDestroyImageView(options.device,t.image.view,nullptr);if(t.image.image)f.vkDestroyImage(options.device,t.image.image,nullptr);if(t.allocation)f.vkFreeMemory(options.device,t.allocation,nullptr);t={};}
    ~State(){for(auto& [key,value]:scratch)destroy(value);}
    Image& image(const std::string& name,VkExtent2D extent,VkFormat format,Identity id){
        require(extent.width>=1&&extent.height>=1&&extent.width<=16384&&extent.height<=16384,"Scratch dimensions invalid");
        auto& t=scratch[name];auto& i=t.image;
        if(i.image&&(i.extent.width!=extent.width||i.extent.height!=extent.height||i.format!=format))destroy(t);
        if(!i.image){
            i.device=options.device;i.extent=extent;i.format=format;i.layout=VK_IMAGE_LAYOUT_UNDEFINED;
            i.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.extent={extent.width,extent.height,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.format=format;ci.usage=i.usage;
            vkCheck(f.vkCreateImage(options.device,&ci,nullptr,&i.image),"Foveation scratch image");
            VkMemoryRequirements req{};f.vkGetImageMemoryRequirements(options.device,i.image,&req);uint32_t type=UINT32_MAX;
            for(uint32_t n=0;n<memory.memoryTypeCount;++n)if((req.memoryTypeBits&(1u<<n))&&(memory.memoryTypes[n].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=n;break;}
            require(type!=UINT32_MAX,"Foveation memory unavailable");VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;
            vkCheck(f.vkAllocateMemory(options.device,&ai,nullptr,&t.allocation),"Foveation scratch memory");vkCheck(f.vkBindImageMemory(options.device,i.image,t.allocation,0),"Foveation scratch bind");
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=i.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=format;vi.subresourceRange=i.range;vkCheck(f.vkCreateImageView(options.device,&vi,nullptr,&i.view),"Foveation scratch view");
        }
        i.identity=id;return i;
    }
    Image resample(VkCommandBuffer cb,const std::string& name,const Image& source,fv::Rect crop,VkExtent2D extent,bool nearest=false,fv::Point scale={1,1}){
        auto& target=image(name,extent,source.format,source.identity);
        commands.push_back(operations->Resample(cb,borrow(source),crop,borrow(target),nearest,scale));target.layout=VK_IMAGE_LAYOUT_GENERAL;return target;
    }
    EyeInput crop(VkCommandBuffer cb,const std::string& name,const EyeInput& in,fv::Rect rect,VkExtent2D extent){
        auto out=in;const fv::Point scale{double(extent.width)/rect.width,double(extent.height)/rect.height};
        out.color=resample(cb,name+"c",in.color,rect,extent);
        out.depth=resample(cb,name+"d",in.depth,rect,extent,true);
        out.motion=resample(cb,name+"m",in.motion,rect,extent,true,scale);
        out.jitterX*=float(scale.x);out.jitterY*=float(scale.y);return out;
    }
    Output evaluate(unsigned branch,const PairInput& pair,VkCommandBuffer cb){
        if(!providers[branch])providers[branch]=std::make_unique<VulkanDlss>(options);
        Output out;const auto result=providers[branch]->Record(pair,cb,out);tickets[branch]=out.ticket;
        if(result!=Status::Recorded)throw std::runtime_error(providers[branch]->Error());return out;
    }
    std::array<Image,2> neural(VkCommandBuffer cb,const PairInput& input,const std::array<Image,2>& colors,const ReconstructionSettings& settings,const fv::FramePlan* plan,bool before){
        PairInput request=input;request.mode=Mode::Neural;request.hdr=false;request.neuralModel=settings.neuralModel;request.appearance=settings.appearance;
        std::array<Image,2> originals;
        std::array<fv::RegionPlan,2> regions{};
        for(unsigned e=0;e<2;++e){const auto name="n"+std::to_string(e);auto guides=input.eyes[e];
            const auto target=colors[e].extent;
            if(!before){guides=crop(cb,name+"up",guides,full(guides.color.extent),target);}
            guides.color=colors[e];
            const auto rect=plan?(before?plan->views[e].neural:afterRegion(plan->views[e])).inputCrop:full(target);
            if(plan)regions[e]=before?plan->views[e].neural:afterRegion(plan->views[e]);
            auto working=[&](uint32_t n){return settings.neuralPercent==100?n:std::max(32u,uint32_t(std::round(n*settings.neuralPercent/100.))&~1u);};
            const VkExtent2D extent{working(rect.width),working(rect.height)};
            originals[e]=resample(cb,name+"original",colors[e],rect,{rect.width,rect.height});
            request.eyes[e]=crop(cb,name,guides,rect,extent);request.output[e]=extent;
        }
        auto result=evaluate(2,request,cb).eyes;
        for(unsigned e=0;e<2;++e){const auto name="nr"+std::to_string(e);
            {auto& restored=image(name+"residual",originals[e].extent,originals[e].format,originals[e].identity);
                commands.push_back(operations->NeuralResolve(cb,borrow(originals[e]),borrow(request.eyes[e].color),borrow(result[e]),borrow(restored),settings.detail,settings.color,settings.neuralSharpness));restored.layout=VK_IMAGE_LAYOUT_GENERAL;result[e]=restored;}
            if(plan){auto& out=image(name,colors[e].extent,colors[e].format,colors[e].identity);
                const auto& v=plan->views[e];fv::Point scale=before?fv::Point{double(v.geometry.output.width)/v.geometry.input.width,double(v.geometry.output.height)/v.geometry.input.height}:fv::Point{1,1};
                commands.push_back(operations->Composite(cb,borrow(colors[e]),borrow(result[e]),regions[e],borrow(out),true,scale));out.layout=VK_IMAGE_LAYOUT_GENERAL;result[e]=out;
            }else result[e]=resample(cb,name,result[e],full(result[e].extent),colors[e].extent);
        }
        return result;
    }
    void finish(bool completed){
        for(unsigned i=0;i<tickets.size();++i)if(tickets[i]){
            require(completed?providers[i]->RetireRecordedAfterCompletion(tickets[i]):providers[i]->CancelRecorded(tickets[i]),"Branch retirement mismatch");tickets[i]=0;
        }
        if(completed)for(auto& command:commands)command->RetiredAfterHostCompletion();
        commands.clear();pending=0;
        if(!completed){for(auto& [key,value]:scratch)destroy(value);resetNext=true;}
        if(failed)resetNext=true;
    }
    void readable(VkCommandBuffer cb,Image& image){
        if(image.layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)return;
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image.image;b.subresourceRange=image.range;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.oldLayout=image.layout;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        f.vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
        image.layout=b.newLayout;for(auto& [name,owned]:scratch)if(owned.image.image==image.image)owned.image.layout=image.layout;
    }
};
VulkanReconstruction::VulkanReconstruction(Options o,bool extended):state(std::make_unique<State>(std::move(o),extended)){}
VulkanReconstruction::~VulkanReconstruction()=default;
Status VulkanReconstruction::Record(const PairInput& original,const ReconstructionSettings& settings,VkCommandBuffer cb,Output& out){
    auto& s=*state;out={};if(s.pending)return Status::Busy;
    if(original.mode==Mode::Off)return Status::Disabled;
    try{
        require(cb&&original.temporalContractValidated,"Reconstruction requires qualified host inputs");
        require(settings.neuralPercent>=50&&settings.neuralPercent<=100&&settings.neuralModel<=2,"Neural control range");
        require(!settings.neural||(!original.hdr&&original.mode!=Mode::Dlaa),"Neural requires the LDR SR route");
        require(!settings.foveation.enabled||original.mode!=Mode::Dlaa,"DLAA foveation excluded by common consumer policy");
        require(settings.revision&&original.eyes[0].color.identity.frame,"Reconstruction identity missing");
        if(settings.foveation.enabled)for(unsigned e=0;e<2;++e)require(settings.geometry[e].input==size(original.eyes[e].color.extent)&&settings.geometry[e].output==size(original.output[e]),"Foveation geometry differs from qualified images");
    }catch(const std::exception& e){s.error=e.what();return Status::Rejected;}
    s.pending=++s.serial;out.ticket=s.pending;s.failed=false;s.error.clear();
    try{
        auto input=original;const auto id=input.eyes[0].color.identity;
        input.reset|=s.resetNext||s.lastRevision!=settings.revision||s.lastEpoch!=id.epoch||s.lastFrame+1!=id.frame;
        std::optional<fv::FramePlan> plan;
        if(settings.foveation.enabled)plan=fv::Plan(settings.foveation,settings.geometry,fv::FixedPlacement(settings.foveation),settings.revision,id.frame,id.epoch);
        std::optional<fv::FramePlan> processing;
        if(plan&&settings.dynamic){auto config=settings.foveation;auto& r=config.central;r.topBps=settings.processingCuts[0];r.bottomBps=settings.processingCuts[1];r.rightBps=settings.processingCuts[2];r.leftBps=settings.processingCuts[3];processing=fv::Plan(config,settings.geometry,fv::FixedPlacement(config),settings.revision,id.frame,id.epoch);}
        if(settings.neural&&settings.neuralBefore){const auto colors=s.neural(cb,input,{input.eyes[0].color,input.eyes[1].color},settings,plan?&*plan:nullptr,true);for(unsigned e=0;e<2;++e)input.eyes[e].color=colors[e];}
        if(!plan)out.eyes=s.evaluate(0,input,cb).eyes;
        else{
            PairInput central=input,peripheral=input;peripheral.mode=Mode::Dlaa;
            central.preset=settings.foveation.centralPreset?settings.foveation.centralPreset:input.preset;peripheral.preset=settings.foveation.peripheralPreset;
            const unsigned branch=settings.dynamic?(settings.processingLevel==1?0:settings.processingLevel==0?3:4):0;
            for(unsigned e=0;e<2;++e){const auto& v=processing?processing->views[e]:plan->views[e];const auto c=v.central.inputCrop;
                central.eyes[e]=s.crop(cb,"c"+std::to_string(e)+"l"+std::to_string(branch),input.eyes[e],c,{c.width,c.height});central.output[e]=size(v.central.reconstruction);
                peripheral.eyes[e]=s.crop(cb,"p"+std::to_string(e),input.eyes[e],full(input.eyes[e].color.extent),size(v.peripheral));peripheral.output[e]=size(v.peripheral);
            }
            const auto detail=s.evaluate(branch,central,cb).eyes;
            std::array<Image,2> base{peripheral.eyes[0].color,peripheral.eyes[1].color};
            if(settings.foveation.periphery==fv::Periphery::Temporal)base=s.evaluate(1,peripheral,cb).eyes;
            for(unsigned e=0;e<2;++e){auto& target=s.image("o"+std::to_string(e),input.output[e],VK_FORMAT_R16G16B16A16_SFLOAT,id);target.identity.eye=e;
                auto mask=plan->views[e].central;if(processing){mask.cropScale=processing->views[e].central.cropScale;mask.cropBias=processing->views[e].central.cropBias;}
                s.commands.push_back(s.operations->Composite(cb,borrow(base[e]),borrow(detail[e]),mask,borrow(target),!input.hdr));target.layout=VK_IMAGE_LAYOUT_GENERAL;out.eyes[e]=target;
            }
        }
        if(settings.neural&&!settings.neuralBefore)out.eyes=s.neural(cb,input,out.eyes,settings,plan?&*plan:nullptr,false);
        for(unsigned e=0;e<2;++e){
            if(settings.sharpen>0){auto& sharpened=s.image("sh"+std::to_string(e),out.eyes[e].extent,out.eyes[e].format,out.eyes[e].identity);s.commands.push_back(s.operations->Sharpen(cb,borrow(out.eyes[e]),borrow(sharpened),settings.sharpen));sharpened.layout=VK_IMAGE_LAYOUT_GENERAL;out.eyes[e]=sharpened;}
            if(plan&&settings.foveation.showRegion){
                for(unsigned region=0;region<2;++region){if((settings.foveation.indicatorRegions!=2&&settings.foveation.indicatorRegions!=region)||(region&&!settings.neural))continue;
                    auto& marked=s.image("indicator"+std::to_string(e)+std::to_string(region),out.eyes[e].extent,out.eyes[e].format,out.eyes[e].identity);s.commands.push_back(s.operations->Indicator(cb,borrow(out.eyes[e]),region?plan->views[e].neural:plan->views[e].central,borrow(marked),region!=0));marked.layout=VK_IMAGE_LAYOUT_GENERAL;out.eyes[e]=marked;
                }
            }
        }
        for(auto& image:out.eyes)s.readable(cb,image);
        s.lastRevision=settings.revision;s.lastEpoch=id.epoch;s.lastFrame=id.frame;s.resetNext=false;
        return Status::Recorded;
    }catch(const std::exception& e){s.failed=true;s.error=e.what();out.eyes={};return Status::Failed;}
}
bool VulkanReconstruction::RetireRecordedAfterCompletion(uint64_t ticket){if(!ticket||ticket!=state->pending)return false;state->finish(true);return true;}
bool VulkanReconstruction::CancelRecorded(uint64_t ticket){if(!ticket||ticket!=state->pending)return false;state->finish(false);return true;}
bool VulkanReconstruction::ReleaseResources(){auto& s=*state;
    if(s.pending)return false;
    for(auto& p:s.providers)if(p&&!p->ReleaseResources())return false;
    for(auto& [name,value]:s.scratch)s.destroy(value);s.scratch.clear();
    s.resetNext=true;s.lastFrame=s.lastEpoch=0;s.error.clear();return true;
}
const std::string& VulkanReconstruction::Error()const{return state->error;}
uint64_t VulkanReconstruction::Evaluations()const{uint64_t n{};for(unsigned i=0;i<state->providers.size();++i)if(i!=2&&state->providers[i])n+=state->providers[i]->Evaluations();return n;}
uint64_t VulkanReconstruction::NeuralEvaluations()const{return state->providers[2]?state->providers[2]->Evaluations():0;}
uint64_t VulkanReconstruction::ResetEvaluations()const{uint64_t n{};for(const auto& p:state->providers)if(p)n+=p->ResetEvaluations();return n;}
}
