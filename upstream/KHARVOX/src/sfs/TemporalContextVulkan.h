#pragma once
#include "../common/TemporalContext.h"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstring>

namespace kharvox::temporal_context {
inline std::string rect(const VkRect2D& r){return "["+std::to_string(r.offset.x)+","+std::to_string(r.offset.y)+","+std::to_string(r.extent.width)+","+std::to_string(r.extent.height)+"]";}
inline std::string bits(const float* v,size_t n){std::vector<uint32_t> words(n);std::memcpy(words.data(),v,n*4);return numbers(words.data(),n);}
inline std::string range(const VkImageSubresourceRange& r){const uint32_t a[]={r.aspectMask,r.baseMipLevel,r.levelCount,r.baseArrayLayer,r.layerCount};return numbers(a,5);}
inline std::string refs(const VkAttachmentReference* r,uint32_t n){std::string out="[";for(uint32_t i=0;i<n;++i)out+=(i?",":"")+std::string("[")+std::to_string(r[i].attachment)+","+std::to_string(r[i].layout)+"]";return out+"]";}
inline std::string pass(const VkRenderPassCreateInfo& i){
    std::ostringstream o;o<<"{\"flags\":"<<i.flags<<",\"attachments\":[";
    for(uint32_t n=0;n<i.attachmentCount;++n){const auto& a=i.pAttachments[n];o<<(n?",":"")<<'['<<a.flags<<','<<a.format<<','<<a.samples<<','<<a.loadOp<<','<<a.storeOp<<','<<a.stencilLoadOp<<','<<a.stencilStoreOp<<','<<a.initialLayout<<','<<a.finalLayout<<']';}
    o<<"],\"subpasses\":[";for(uint32_t n=0;n<i.subpassCount;++n){const auto& s=i.pSubpasses[n];o<<(n?",":"")<<"{\"flags\":"<<s.flags<<",\"point\":"<<s.pipelineBindPoint<<",\"input\":"<<refs(s.pInputAttachments,s.inputAttachmentCount)<<",\"color\":"<<refs(s.pColorAttachments,s.colorAttachmentCount)<<",\"resolve\":"<<refs(s.pResolveAttachments,s.pResolveAttachments?s.colorAttachmentCount:0)<<",\"depth\":"<<refs(s.pDepthStencilAttachment,s.pDepthStencilAttachment?1:0)<<",\"preserve\":"<<numbers(s.pPreserveAttachments,s.preserveAttachmentCount)<<'}';}
    o<<"],\"dependencies\":[";for(uint32_t n=0;n<i.dependencyCount;++n){const auto& d=i.pDependencies[n];o<<(n?",":"")<<'['<<d.srcSubpass<<','<<d.dstSubpass<<','<<d.srcStageMask<<','<<d.dstStageMask<<','<<d.srcAccessMask<<','<<d.dstAccessMask<<','<<d.dependencyFlags<<']';}
    o<<"],\"extensions\":[";bool comma=false;for(auto p=static_cast<const VkBaseInStructure*>(i.pNext);p;p=p->pNext){o<<(comma?",":"")<<"{\"type\":"<<p->sType;comma=true;
        if(p->sType==VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO){const auto& m=*reinterpret_cast<const VkRenderPassMultiviewCreateInfo*>(p);o<<",\"viewMasks\":"<<numbers(m.pViewMasks,m.subpassCount)<<",\"viewOffsets\":"<<numbers(m.pViewOffsets,m.dependencyCount)<<",\"correlationMasks\":"<<numbers(m.pCorrelationMasks,m.correlationMaskCount);}else o<<",\"unknown\":true";o<<'}';}
    return o.str()+"]}";
}
// Used under NativeSfs's state lock. Canonical array cells survive pre-arm
// updates/copies, and layout destruction does not destroy allocated set layout
// knowledge. Reset/free discards every cell, including on handle reuse.
class VulkanMetadata {
    struct Binding {uint32_t binding,count;VkDescriptorType type;VkShaderStageFlags stages;std::vector<uint64_t> samplers;};
    struct Layout {std::vector<Binding> bindings;};
    struct Set {uint64_t layout,pool,allocation;std::shared_ptr<Layout> shape;std::map<std::pair<uint32_t,uint32_t>,std::string> cells;bool unknown{};};
    std::map<uint64_t,std::shared_ptr<Layout>> layouts;
    std::map<uint64_t,Set> sets;
    uint64_t allocations{};size_t cells{};
    using Coordinate=std::pair<uint32_t,uint32_t>;
    std::vector<Coordinate> coordinates(const Set& set,uint32_t binding,uint32_t element,uint32_t count,VkDescriptorType type){
        std::vector<Coordinate> result;if(!set.shape||count>4096)return result;
        const auto& bs=set.shape->bindings;auto b=std::find_if(bs.begin(),bs.end(),[&](const auto& v){return v.binding==binding;});
        if(b==bs.end()||element>=b->count)return result;const auto stages=b->stages;const bool immutable=!b->samplers.empty();
        while(count){if(b==bs.end()||b->type!=type||b->stages!=stages||(!b->samplers.empty())!=immutable)return {};
            while(element<b->count&&count){result.emplace_back(b->binding,element++);--count;}++b;element=0;
            while(b!=bs.end()&&!b->count)++b;
        }return result;
    }
    void snapshot(uint64_t id,Set& set){std::ostringstream o;o<<"{\"layout\":"<<set.layout<<",\"pool\":"<<set.pool<<",\"allocation\":"<<set.allocation<<",\"unknown\":"<<(set.unknown?"true":"false")<<",\"cells\":[";bool comma=false;for(const auto& cell:set.cells){o<<(comma?",":"")<<'['<<cell.first.first<<','<<cell.first.second<<','<<cell.second<<']';comma=true;}o<<"],\"bindings\":[";comma=false;if(set.shape)for(const auto& b:set.shape->bindings){o<<(comma?",":"")<<'['<<b.binding<<','<<b.count<<','<<b.type<<','<<b.stages<<']';comma=true;}o<<"]}";capture->define("set",id,o.str());}
    void assign(Set& set,const Coordinate& cell,std::string value){if(!set.cells.count(cell)){if(cells>=262144){set.unknown=true;capture->gap("descriptor-cell-capacity");return;}++cells;}set.cells[cell]=std::move(value);}
public:
    std::shared_ptr<Capture> capture;
    explicit VulkanMetadata(std::shared_ptr<Capture> c):capture(std::move(c)){}
    void layout(VkDescriptorSetLayout id,const VkDescriptorSetLayoutCreateInfo& i){
        if(capture->finished())return;if(layouts.size()>=4096||i.bindingCount>4096){capture->gap("descriptor-layout-capacity");return;}
        auto l=std::make_shared<Layout>();std::ostringstream o;o<<"{\"flags\":"<<i.flags<<",\"bindings\":[";
        for(uint32_t n=0;n<i.bindingCount;++n){const auto& b=i.pBindings[n];if(b.descriptorCount>4096){capture->gap("descriptor-array-capacity");return;}Binding v{b.binding,b.descriptorCount,b.descriptorType,b.stageFlags,{}};
            if(b.pImmutableSamplers)for(uint32_t k=0;k<b.descriptorCount;++k)v.samplers.push_back(handle(b.pImmutableSamplers[k]));
            o<<(n?",":"")<<'['<<b.binding<<','<<b.descriptorCount<<','<<b.descriptorType<<','<<b.stageFlags<<','<<numbers(v.samplers.data(),v.samplers.size())<<']';l->bindings.push_back(std::move(v));}
        std::sort(l->bindings.begin(),l->bindings.end(),[](const auto& a,const auto& b){return a.binding<b.binding;});layouts[handle(id)]=l;capture->define("setLayout",handle(id),o.str()+"]}");
    }
    void forgetLayout(VkDescriptorSetLayout id){layouts.erase(handle(id));capture->forget("setLayout",handle(id));}
    void allocate(VkDescriptorSet id,VkDescriptorSetLayout layout,VkDescriptorPool pool){
        if(capture->finished())return;if(sets.size()>=32768){capture->gap("descriptor-set-capacity");return;}forget(id);
        auto l=layouts.find(handle(layout));Set s{handle(layout),handle(pool),++allocations,l==layouts.end()?nullptr:l->second,{},l==layouts.end()};
        sets.emplace(handle(id),std::move(s));snapshot(handle(id),sets.at(handle(id)));
    }
    void forget(VkDescriptorSet id){auto it=sets.find(handle(id));if(it!=sets.end()){cells-=it->second.cells.size();sets.erase(it);}capture->forget("set",handle(id));}
    void pool(VkDescriptorPool id){for(auto it=sets.begin();it!=sets.end();)if(it->second.pool==handle(id)){cells-=it->second.cells.size();capture->forget("set",it->first);it=sets.erase(it);}else ++it;}
    void update(uint32_t count,const VkWriteDescriptorSet* writes,uint32_t copies,const VkCopyDescriptorSet* copy){
        if(capture->finished())return;
        for(uint32_t j=0;j<count;++j){const auto& w=writes[j];auto it=sets.find(handle(w.dstSet));if(it==sets.end()){capture->gap("untracked-write-set");continue;}auto& set=it->second;
            const auto where=coordinates(set,w.dstBinding,w.dstArrayElement,w.descriptorCount,w.descriptorType);
            if(w.pNext||where.size()!=w.descriptorCount){set.unknown=true;capture->gap("unsupported-descriptor-write");snapshot(it->first,set);continue;}
            for(uint32_t n=0;n<w.descriptorCount;++n){std::ostringstream value;value<<"{\"type\":"<<w.descriptorType;
                switch(w.descriptorType){
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                    value<<",\"view\":"<<handle(w.pImageInfo[n].imageView)<<",\"layout\":"<<w.pImageInfo[n].imageLayout;break;
                case VK_DESCRIPTOR_TYPE_SAMPLER:value<<",\"samplerOnly\":true";break;
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                    value<<",\"buffer\":"<<handle(w.pBufferInfo[n].buffer)<<",\"offset\":"<<w.pBufferInfo[n].offset<<",\"range\":"<<w.pBufferInfo[n].range;break;
                case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:value<<",\"bufferView\":"<<handle(w.pTexelBufferView[n]);break;
                default:value<<",\"unknown\":true";set.unknown=true;capture->gap("unsupported-descriptor-type");break;
                }assign(set,where[n],value.str()+"}");}
            snapshot(it->first,set);
        }
        for(uint32_t j=0;j<copies;++j){const auto& c=copy[j];auto src=sets.find(handle(c.srcSet)),dst=sets.find(handle(c.dstSet));
            if(dst==sets.end()){capture->gap("untracked-copy-destination");continue;}auto& d=dst->second;
            const auto bad=[&]{d.unknown=true;capture->gap("unsupported-descriptor-copy");snapshot(dst->first,d);};
            if(c.pNext||src==sets.end()||!src->second.shape||src->second.unknown){bad();continue;}
            const auto& bindings=src->second.shape->bindings;auto binding=std::find_if(bindings.begin(),bindings.end(),[&](const auto& b){return b.binding==c.srcBinding;});if(binding==bindings.end()){bad();continue;}
            auto from=coordinates(src->second,c.srcBinding,c.srcArrayElement,c.descriptorCount,binding->type),to=coordinates(d,c.dstBinding,c.dstArrayElement,c.descriptorCount,binding->type);
            if(from.size()!=c.descriptorCount||to.size()!=c.descriptorCount){bad();continue;}
            std::vector<std::string> values;for(const auto& key:from){auto cell=src->second.cells.find(key);values.push_back(cell==src->second.cells.end()?"{\"uninitialized\":true}":cell->second);}
            for(size_t n=0;n<to.size();++n)assign(d,to[n],values[n]);snapshot(dst->first,d);
        }
    }
};
inline void submitted(uint64_t device,VkQueue queue,uint32_t count,const VkSubmitInfo* info,VkFence fence,VkResult result)noexcept{
    auto c=current();if(!c||!c->active()||c->device!=device)return;c->protect([&]{
        std::ostringstream o;o<<"{\"queue\":"<<handle(queue)<<",\"fence\":"<<handle(fence)<<",\"result\":"<<result<<",\"batches\":[";
        for(uint32_t n=0;n<count;++n){const auto& i=info[n];const VkTimelineSemaphoreSubmitInfo* timeline=nullptr;
            for(auto p=static_cast<const VkBaseInStructure*>(i.pNext);p;p=p->pNext){if(p->sType==VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO)timeline=reinterpret_cast<const VkTimelineSemaphoreSubmitInfo*>(p);else c->gap("unknown-submit-extension");}
            o<<(n?",":"")<<"{\"commands\":[";for(uint32_t k=0;k<i.commandBufferCount;++k)o<<(k?",":"")<<handle(i.pCommandBuffers[k]);o<<"],\"waits\":[";
            for(uint32_t k=0;k<i.waitSemaphoreCount;++k)o<<(k?",":"")<<'['<<handle(i.pWaitSemaphores[k])<<','<<i.pWaitDstStageMask[k]<<','<<(timeline&&k<timeline->waitSemaphoreValueCount?timeline->pWaitSemaphoreValues[k]:0)<<']';o<<"],\"signals\":[";
            for(uint32_t k=0;k<i.signalSemaphoreCount;++k)o<<(k?",":"")<<'['<<handle(i.pSignalSemaphores[k])<<','<<(timeline&&k<timeline->signalSemaphoreValueCount?timeline->pSignalSemaphoreValues[k]:0)<<']';o<<"]}";}
        c->event("submit",o.str()+"]}");
    });
}
inline void submitted2(uint64_t device,VkQueue queue,uint32_t count,const VkSubmitInfo2* info,VkFence fence,VkResult result)noexcept{
    auto c=current();if(!c||!c->active()||c->device!=device)return;c->protect([&]{
        std::ostringstream o;o<<"{\"queue\":"<<handle(queue)<<",\"fence\":"<<handle(fence)<<",\"result\":"<<result<<",\"batches\":[";
        for(uint32_t n=0;n<count;++n){const auto& i=info[n];if(i.pNext)c->gap("unknown-submit2-extension");o<<(n?",":"")<<"{\"flags\":"<<i.flags<<",\"commands\":[";
            for(uint32_t k=0;k<i.commandBufferInfoCount;++k){const auto& b=i.pCommandBufferInfos[k];o<<(k?",":"")<<'['<<handle(b.commandBuffer)<<','<<b.deviceMask<<']';if(b.pNext)c->gap("unknown-submit2-command-extension");}o<<"],\"waits\":[";
            for(uint32_t k=0;k<i.waitSemaphoreInfoCount;++k){const auto& w=i.pWaitSemaphoreInfos[k];o<<(k?",":"")<<'['<<handle(w.semaphore)<<','<<w.stageMask<<','<<w.value<<','<<w.deviceIndex<<']';if(w.pNext)c->gap("unknown-submit2-wait-extension");}o<<"],\"signals\":[";
            for(uint32_t k=0;k<i.signalSemaphoreInfoCount;++k){const auto& w=i.pSignalSemaphoreInfos[k];o<<(k?",":"")<<'['<<handle(w.semaphore)<<','<<w.stageMask<<','<<w.value<<','<<w.deviceIndex<<']';if(w.pNext)c->gap("unknown-submit2-signal-extension");}o<<"]}";}
        c->event("submit2",o.str()+"]}");
    });
}
}
