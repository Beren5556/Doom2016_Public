#include <dlss_foveado/Vulkan.h>
#include "VulkanShaders.h"
#include <array>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <mutex>

namespace dlss_foveado::vulkan {
namespace {
void need(bool yes,const char* text){if(!yes)throw std::invalid_argument(text);}
void check(VkResult result){if(result!=VK_SUCCESS)throw std::runtime_error("Foveation Vulkan failure: "+std::to_string(result));}
struct Parameters {std::array<float,4> sourceRect{},sizes{},crop{},centerHalf{},options{},componentScale{1,1,1,0},commonX{},commonY{},commonW{},radii{};};
static_assert(sizeof(Parameters)==160);
void regionParameters(Parameters& p,const RegionPlan& r){
    p.crop={float(r.cropScale.x),float(r.cropScale.y),float(r.cropBias.x),float(r.cropBias.y)};
    p.centerHalf={float(r.centerPixels.x),float(r.centerPixels.y),float(r.halfSizePixels.x),float(r.halfSizePixels.y)};
    p.options[0]=r.shape==Shape::Ellipse?1.f:0.f;p.options[1]=float(r.transitionPixels);
    if(r.geometryMode==GeometryMode::BinocularFourSides){const auto& h=r.binocular.outputToCommon;
        p.commonX={float(h[0]),float(h[1]),float(h[2]),1};p.commonY={float(h[3]),float(h[4]),float(h[5]),0};p.commonW={float(h[6]),float(h[7]),float(h[8]),0};
        for(unsigned n=0;n<4;++n)p.radii[n]=float(r.radii[n]);p.options[1]=float(r.transitionFraction);}
}
unsigned formatIndex(VkFormat f){switch(f){case VK_FORMAT_R16G16B16A16_SFLOAT:return 0;case VK_FORMAT_R32G32B32A32_SFLOAT:return 1;case VK_FORMAT_R32G32_SFLOAT:return 2;case VK_FORMAT_R32_SFLOAT:return 3;default:throw std::invalid_argument("Unsupported Vulkan foveation format");}}
}
struct Operations::State {
    Context c;VkDescriptorSetLayout layout{};VkPipelineLayout pipelineLayout{};std::array<VkPipeline,4> pipelines{};VkSampler sampler{};
    struct Slot {VkDescriptorPool pool{};VkDescriptorSet set{};VkBuffer buffer{};VkDeviceMemory memory{};};
    std::vector<Slot> cached;std::mutex cacheMutex;
#define FUNCTIONS(X) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateComputePipelines) X(vkDestroyPipeline) X(vkCreateSampler) X(vkDestroySampler) \
    X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) X(vkUnmapMemory) \
    X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDispatch) X(vkCmdPipelineBarrier) X(vkCmdCopyImage) X(vkWaitForFences) X(vkGetFenceStatus)
#define DECLARE(n) PFN_##n n{};
    FUNCTIONS(DECLARE)
#undef DECLARE
    explicit State(Context context):c(context){}
    void Initialize(){need(c.device&&c.resolve,"Missing Vulkan foveation context");
#define LOAD(n) n=reinterpret_cast<PFN_##n>(c.resolve(c.device,#n));need(n!=nullptr,#n);
        FUNCTIONS(LOAD)
#undef LOAD
#undef FUNCTIONS
        VkDescriptorSetLayoutBinding bindings[5]{};for(unsigned n=0;n<5;++n){bindings[n].binding=n;bindings[n].descriptorCount=1;bindings[n].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;bindings[n].descriptorType=n==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:n==3?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;}
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dl.bindingCount=5;dl.pBindings=bindings;check(vkCreateDescriptorSetLayout(c.device,&dl,nullptr,&layout));
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&layout;check(vkCreatePipelineLayout(c.device,&pl,nullptr,&pipelineLayout));
        const uint32_t* codes[]{shader_rgba16f,shader_rgba32f,shader_rg32f,shader_r32f};const size_t sizes[]{sizeof(shader_rgba16f),sizeof(shader_rgba32f),sizeof(shader_rg32f),sizeof(shader_r32f)};
        for(unsigned n=0;n<4;++n){if(n==2&&!c.storageImageExtendedFormatsEnabled)continue;VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=sizes[n];mi.pCode=codes[n];VkShaderModule module{};check(vkCreateShaderModule(c.device,&mi,nullptr,&module));
            VkComputePipelineCreateInfo pi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pi.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pi.stage.module=module;pi.stage.pName="main";pi.layout=pipelineLayout;const auto result=vkCreateComputePipelines(c.device,VK_NULL_HANDLE,1,&pi,nullptr,&pipelines[n]);vkDestroyShaderModule(c.device,module,nullptr);check(result);}
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_LINEAR;si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;check(vkCreateSampler(c.device,&si,nullptr,&sampler));
    }
    void Validate(const Image& i,bool output)const{need(i.device==c.device&&i.image&&i.view,"Vulkan foveation image/device mismatch");need(i.extent.width&&i.extent.height&&i.extent.width<=16384&&i.extent.height<=16384,"Invalid foveation extent");need(i.samples==VK_SAMPLE_COUNT_1_BIT&&i.range.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT&&i.range.levelCount==1&&i.range.layerCount==1,"Expected single eye/layer/mip/color sample");(void)formatIndex(i.format);
        need(!output||i.format!=VK_FORMAT_R32G32_SFLOAT||c.storageImageExtendedFormatsEnabled,"RG32F output requires enabled storageImageExtendedFormats");
        need((i.usage&(output?VK_IMAGE_USAGE_STORAGE_BIT:VK_IMAGE_USAGE_SAMPLED_BIT))!=0,"Missing foveation image usage");need(i.layout==VK_IMAGE_LAYOUT_GENERAL||i.layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL||(output&&i.layout==VK_IMAGE_LAYOUT_UNDEFINED),"Unknown foveation image layout");}
    void Barrier(VkCommandBuffer command,const Image& i,VkImageLayout before,VkImageLayout after){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=i.image;b.subresourceRange=i.range;b.oldLayout=before;b.newLayout=after;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.srcAccessMask=before==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);}
    void destroy(Slot slot){if(slot.pool)vkDestroyDescriptorPool(c.device,slot.pool,nullptr);if(slot.buffer)vkDestroyBuffer(c.device,slot.buffer,nullptr);if(slot.memory)vkFreeMemory(c.device,slot.memory,nullptr);}
    void recycle(Slot slot){std::lock_guard<std::mutex> guard(cacheMutex);if(cached.size()<96)cached.push_back(slot);else destroy(slot);}
    ~State(){for(auto slot:cached)destroy(slot);for(auto p:pipelines)if(p)vkDestroyPipeline(c.device,p,nullptr);if(sampler)vkDestroySampler(c.device,sampler,nullptr);if(pipelineLayout)vkDestroyPipelineLayout(c.device,pipelineLayout,nullptr);if(layout)vkDestroyDescriptorSetLayout(c.device,layout,nullptr);}
};
struct Recorded::State {
    std::shared_ptr<Operations::State> owner;VkDescriptorPool pool{};VkDescriptorSet set{};VkBuffer buffer{};VkDeviceMemory memory{};VkFence fence{};bool copied{},hostCompleted{};
    ~State(){auto& s=*owner;const bool safe=!fence||s.vkWaitForFences(s.c.device,1,&fence,VK_TRUE,UINT64_MAX)==VK_SUCCESS;Operations::State::Slot slot{pool,set,buffer,memory};if(safe&&pool&&set&&buffer&&memory)s.recycle(slot);else s.destroy(slot);}
};
Recorded::Recorded(std::unique_ptr<State> s):state(std::move(s)){}
Recorded::~Recorded()=default;
void Recorded::Submitted(VkFence completion){need(completion&&!state->fence&&!state->hostCompleted,"Missing or repeated completion fence");state->fence=completion;}
void Recorded::RetiredAfterHostCompletion(){need(!state->hostCompleted,"Repeated host completion");state->fence=VK_NULL_HANDLE;state->hostCompleted=true;}
bool Recorded::Complete()const{return state->hostCompleted||(state->fence&&state->owner->vkGetFenceStatus(state->owner->c.device,state->fence)==VK_SUCCESS);}
bool Recorded::DirectCopy()const{return state->copied;}
Operations::Operations(Context c):state(std::make_shared<State>(c)){state->Initialize();}
Operations::~Operations()=default;
static std::unique_ptr<Recorded> dispatch(std::shared_ptr<Operations::State> owner,VkCommandBuffer command,const Image& a,const Image& b,const Image& target,const Parameters& parameters,const Image* third=nullptr){
    auto job=std::make_unique<Recorded::State>();job->owner=owner;auto& s=*owner;need(command!=VK_NULL_HANDLE,"Missing recording command");s.Validate(a,false);s.Validate(b,false);s.Validate(target,true);need(a.image!=target.image&&b.image!=target.image,"Foveation cannot overwrite its input image");
    const auto& c=third?*third:a;s.Validate(c,false);need(c.image!=target.image,"Resolve cannot overwrite model input");
    {std::lock_guard<std::mutex> guard(s.cacheMutex);if(!s.cached.empty()){const auto slot=s.cached.back();s.cached.pop_back();job->pool=slot.pool;job->set=slot.set;job->buffer=slot.buffer;job->memory=slot.memory;}}
    if(!job->buffer){
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=sizeof(Parameters);bi.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;check(s.vkCreateBuffer(s.c.device,&bi,nullptr,&job->buffer));
    VkMemoryRequirements req{};s.vkGetBufferMemoryRequirements(s.c.device,job->buffer,&req);uint32_t type=UINT32_MAX;for(uint32_t n=0;n<s.c.memory.memoryTypeCount;++n)if((req.memoryTypeBits&(1u<<n))&&(s.c.memory.memoryTypes[n].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=n;break;}need(type!=UINT32_MAX,"No coherent foveation constants memory");
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;check(s.vkAllocateMemory(s.c.device,&ai,nullptr,&job->memory));check(s.vkBindBufferMemory(s.c.device,job->buffer,job->memory,0));
    VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,3},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=1;dp.poolSizeCount=3;dp.pPoolSizes=sizes;check(s.vkCreateDescriptorPool(s.c.device,&dp,nullptr,&job->pool));
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=job->pool;da.descriptorSetCount=1;da.pSetLayouts=&s.layout;check(s.vkAllocateDescriptorSets(s.c.device,&da,&job->set));
    }
    void* mapped{};check(s.vkMapMemory(s.c.device,job->memory,0,sizeof(parameters),0,&mapped));std::memcpy(mapped,&parameters,sizeof(parameters));s.vkUnmapMemory(s.c.device,job->memory);
    const auto set=job->set;
    VkDescriptorBufferInfo buffer{job->buffer,0,sizeof(parameters)};VkDescriptorImageInfo images[]{{s.sampler,a.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{s.sampler,b.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{VK_NULL_HANDLE,target.view,VK_IMAGE_LAYOUT_GENERAL},{s.sampler,c.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet writes[5]{};for(unsigned n=0;n<5;++n){auto& w=writes[n];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=n;w.descriptorCount=1;w.descriptorType=n==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:n==3?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;if(n==0)w.pBufferInfo=&buffer;else w.pImageInfo=&images[n-1];}s.vkUpdateDescriptorSets(s.c.device,5,writes,0,nullptr);
    const bool separate=c.image!=a.image&&c.image!=b.image;if(separate)s.Barrier(command,c,c.layout,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    s.Barrier(command,a,a.layout,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);if(a.image!=b.image||a.range.baseArrayLayer!=b.range.baseArrayLayer)s.Barrier(command,b,b.layout,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);s.Barrier(command,target,target.layout,VK_IMAGE_LAYOUT_GENERAL);
    s.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,s.pipelines[formatIndex(target.format)]);s.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,s.pipelineLayout,0,1,&set,0,nullptr);s.vkCmdDispatch(command,(target.extent.width+7)/8,(target.extent.height+7)/8,1);
    s.Barrier(command,a,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,a.layout);if(a.image!=b.image||a.range.baseArrayLayer!=b.range.baseArrayLayer)s.Barrier(command,b,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,b.layout);s.Barrier(command,target,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL);
    if(separate)s.Barrier(command,c,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,c.layout);
    return std::make_unique<Recorded>(std::move(job));
}
std::unique_ptr<Recorded> Operations::Resample(VkCommandBuffer command,const Image& source,Rect crop,const Image& target,bool nearest,Point scale){
    state->Validate(source,false);state->Validate(target,true);need(command&&source.image!=target.image,"Invalid crop command/alias");need(crop.width&&crop.height&&uint64_t(crop.x)+crop.width<=source.extent.width&&uint64_t(crop.y)+crop.height<=source.extent.height,"Crop out of bounds");need(std::isfinite(scale.x)&&std::isfinite(scale.y),"Invalid component scale");
    if(source.format==target.format&&crop.width==target.extent.width&&crop.height==target.extent.height&&scale==Point{1,1}&&(source.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)&&(target.usage&VK_IMAGE_USAGE_TRANSFER_DST_BIT)){
        auto job=std::make_unique<Recorded::State>();job->owner=state;job->copied=true;state->Barrier(command,source,source.layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);state->Barrier(command,target,target.layout,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageCopy c{};c.srcSubresource={source.range.aspectMask,source.range.baseMipLevel,source.range.baseArrayLayer,1};c.dstSubresource={target.range.aspectMask,target.range.baseMipLevel,target.range.baseArrayLayer,1};c.srcOffset={int32_t(crop.x),int32_t(crop.y),0};c.extent={crop.width,crop.height,1};state->vkCmdCopyImage(command,source.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,target.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&c);state->Barrier(command,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,source.layout);state->Barrier(command,target,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);return std::make_unique<Recorded>(std::move(job));}
    Parameters p;p.sourceRect={float(crop.x),float(crop.y),float(crop.width),float(crop.height)};p.sizes={float(source.extent.width),float(source.extent.height),float(target.extent.width),float(target.extent.height)};p.options[0]=nearest?1.f:0.f;p.componentScale[0]=float(scale.x);p.componentScale[1]=float(scale.y);return dispatch(state,command,source,source,target,p);
}
std::unique_ptr<Recorded> Operations::Composite(VkCommandBuffer command,const Image& peripheral,const Image& central,const RegionPlan& region,const Image& target,bool srgb,Point scale){
    need(std::isfinite(scale.x)&&std::isfinite(scale.y)&&scale.x>0&&scale.y>0,"Invalid composition region scale");
    Parameters p;regionParameters(p,region);p.sizes={0,0,float(target.extent.width),float(target.extent.height)};p.options[2]=srgb?1.f:0.f;p.componentScale={float(scale.x),float(scale.y),1,1};
    for(const auto* fields:{&p.sourceRect,&p.sizes,&p.crop,&p.centerHalf,&p.options,&p.componentScale,&p.commonX,&p.commonY,&p.commonW,&p.radii})for(float value:*fields)need(std::isfinite(value),"Non-finite region parameters");
    need(region.shape==Shape::Ellipse||region.shape==Shape::Rectangle,"Unknown region shape");
    if(region.geometryMode==GeometryMode::BinocularFourSides){for(auto r:region.radii)need(r>0,"Invalid binocular radii");need(region.binocular.spaceId&&region.binocular.revision,"Missing binocular identity");
        const auto& h=region.binocular.outputToCommon;for(double x:{0.,double(target.extent.width)*scale.x})for(double y:{0.,double(target.extent.height)*scale.y})need(h[6]*x+h[7]*y+h[8]>1e-6,"Singular binocular mapping over output");
    }else {need(region.geometryMode==GeometryMode::Legacy,"Unknown region geometry");need(region.halfSizePixels.x>0&&region.halfSizePixels.y>0,"Invalid region size");}
    return dispatch(state,command,peripheral,central,target,p);
}
std::unique_ptr<Recorded> Operations::NeuralResolve(VkCommandBuffer command,const Image& original,const Image& smallInput,const Image& smallOutput,const Image& target,float detail,float color,float sharpness){
    need(std::isfinite(detail)&&detail>=0&&detail<=2&&std::isfinite(color)&&color>=0&&color<=1&&std::isfinite(sharpness)&&sharpness>=0&&sharpness<=1,"Neural resolve controls out of range");
    need(original.extent==target.extent&&smallInput.extent==smallOutput.extent,"Neural residual geometry mismatch");
    Parameters p;p.sizes={0,0,float(target.extent.width),float(target.extent.height)};p.componentScale[3]=2;p.options={detail,color,sharpness,0};return dispatch(state,command,original,smallInput,target,p,&smallOutput);
}
std::unique_ptr<Recorded> Operations::Sharpen(VkCommandBuffer command,const Image& source,const Image& target,float strength){
    need(std::isfinite(strength)&&strength>=0&&strength<=1&&source.extent==target.extent,"Sharpen controls/geometry mismatch");Parameters p;p.sizes={0,0,float(target.extent.width),float(target.extent.height)};p.componentScale[3]=3;p.options[0]=strength;return dispatch(state,command,source,source,target,p);
}
std::unique_ptr<Recorded> Operations::Indicator(VkCommandBuffer command,const Image& source,const RegionPlan& region,const Image& target,bool neural){
    need(source.extent==target.extent,"Indicator geometry mismatch");Parameters p;regionParameters(p,region);p.sizes={0,0,float(target.extent.width),float(target.extent.height)};p.componentScale[3]=4;p.options[2]=neural?1:0;return dispatch(state,command,source,source,target,p);
}
}
