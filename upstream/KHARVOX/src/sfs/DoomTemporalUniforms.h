#pragma once
#include "MappedUniforms.h"
#include <array>
#include <vector>
#include <type_traits>

namespace kharvox::sfs {
struct DoomTemporalUniformSnapshot {std::array<float,56> high{};std::array<float,28> low{};};
struct DoomFinalUniformSnapshot {std::array<float,12> high{},low{};};
// Exact seven-binding TAA layout. Descriptor and dynamic-offset ownership is
// tracked independently of debug captures; unknown updates invalidate the read.
class DoomTemporalUniforms {
    struct Shape {std::array<bool,2> dynamic{};bool finalScene{};};
    struct Set {Shape shape;VkDescriptorPool pool{};std::array<VkDescriptorBufferInfo,2> buffers{};};
    std::unordered_map<VkDescriptorSetLayout,Shape> layouts;
    std::unordered_map<VkDescriptorSet,Set> sets;
public:
    MappedUniforms memory;
    void layout(VkDescriptorSetLayout id,const VkDescriptorSetLayoutCreateInfo& info){
        layouts.erase(id);if(info.pNext||info.flags||(info.bindingCount!=7&&info.bindingCount!=4)||layouts.size()>=4096)return;
        Shape shape;shape.finalScene=info.bindingCount==4;uint32_t mask=0;
        for(uint32_t n=0;n<info.bindingCount;++n){const auto& b=info.pBindings[n];
            if(b.binding>=info.bindingCount||b.descriptorCount!=1||!(b.stageFlags&VK_SHADER_STAGE_FRAGMENT_BIT)||(mask&(1u<<b.binding)))return;
            mask|=1u<<b.binding;
            if(b.binding<2){if(b.descriptorType!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER&&b.descriptorType!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)return;shape.dynamic[b.binding]=b.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;}
            else if(b.descriptorType!=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)return;
        }
        layouts.emplace(id,shape);
    }
    void forgetLayout(VkDescriptorSetLayout id){layouts.erase(id);}
    void allocate(VkDescriptorSet id,VkDescriptorSetLayout layout,VkDescriptorPool pool){
        sets.erase(id);const auto l=layouts.find(layout);if(l!=layouts.end()&&sets.size()<32768)sets.emplace(id,Set{l->second,pool,{}});
    }
    void forget(VkDescriptorSet id){sets.erase(id);}
    void pool(VkDescriptorPool id){for(auto i=sets.begin();i!=sets.end();)if(i->second.pool==id)i=sets.erase(i);else ++i;}
    void update(uint32_t count,const VkWriteDescriptorSet* writes,uint32_t copies,const VkCopyDescriptorSet* copy){
        for(uint32_t n=0;n<count;++n){const auto& w=writes[n];auto found=sets.find(w.dstSet);if(found==sets.end()||w.dstBinding>=2)continue;auto& s=found->second;
            for(uint32_t k=w.dstBinding;k<2&&uint64_t(k)<uint64_t(w.dstBinding)+w.descriptorCount;++k){
                s.buffers[k]={};const auto expected=s.shape.dynamic[k]?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                if(!w.pNext&&!w.dstArrayElement&&w.pBufferInfo&&w.descriptorType==expected&&w.descriptorCount<=2-w.dstBinding)
                    s.buffers[k]=w.pBufferInfo[k-w.dstBinding];
            }
        }
        for(uint32_t n=0;n<copies;++n){const auto& c=copy[n];auto dst=sets.find(c.dstSet);if(dst==sets.end()||c.dstBinding>=2)continue;
            auto src=sets.find(c.srcSet);std::array<VkDescriptorBufferInfo,2> values{};
            if(!c.pNext&&!c.srcArrayElement&&!c.dstArrayElement&&c.srcBinding<2&&c.descriptorCount<=2-c.srcBinding&&c.descriptorCount<=2-c.dstBinding&&src!=sets.end())
                for(uint32_t k=0;k<c.descriptorCount;++k)if(src->second.shape.dynamic[c.srcBinding+k]==dst->second.shape.dynamic[c.dstBinding+k])values[k]=src->second.buffers[c.srcBinding+k];
            for(uint32_t k=c.dstBinding;k<2&&uint64_t(k)<uint64_t(c.dstBinding)+c.descriptorCount;++k)dst->second.buffers[k]=values[k-c.dstBinding];
        }
    }
    template<class Snapshot>bool read(VkDescriptorSet id,const std::vector<uint32_t>& dynamic,Snapshot& output)const{
        const auto s=sets.find(id);if(s==sets.end()||s->second.shape.finalScene!=std::is_same_v<Snapshot,DoomFinalUniformSnapshot>||dynamic.size()!=size_t(s->second.shape.dynamic[0])+size_t(s->second.shape.dynamic[1]))return false;
        std::array<uint32_t,2> offsets{};unsigned n=0;for(unsigned k=0;k<2;++k)if(s->second.shape.dynamic[k])offsets[k]=dynamic[n++];
        Snapshot value;
        if(!memory.read(s->second.buffers[0],offsets[0],value.high.data(),sizeof(value.high))||!memory.read(s->second.buffers[1],offsets[1],value.low.data(),sizeof(value.low)))return false;
        output=value;return true;
    }
};
}
