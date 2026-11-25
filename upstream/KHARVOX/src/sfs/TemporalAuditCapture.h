#pragma once
#include "DoomTemporalAudit.h"
#include "NativeDispatch.h"
#include "TemporalAuditOperation.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace kharvox::sfs {
// Owned, bounded capture storage. All calls are protected by NativeSfs::mutex.
// retire() requires actual device completion AND shader->host barriers recorded
// after render passes. It never introduces a queue submission or wait itself.
class TemporalAuditCapture {
    static constexpr uint32_t slots=321, captureBatches=32, timeoutBatches=2400;
    struct Metadata {
        VkDescriptorSet set{};
        std::array<std::string,7> resources{};
        std::array<uint32_t,7> updates{};
        uint64_t generation{},operation{},parentGeneration{};
        uint32_t parent{};bool released{};
        std::string operationJson;
    };
    VkDevice device{};const NativeDispatch& fn;
    VkBuffer buffer{};VkDeviceMemory memory{};void* mapped{};
    VkDeviceSize stride{};
    std::array<Metadata,slots> metadata{};
    std::unordered_map<VkDescriptorSet,uint32_t> owners;
    std::vector<uint32_t> reusable;
    std::mutex operationMutex;
    uint64_t generationSequence{},operationSequence{},operationExhausted{};
    bool perOperation{},geometry{};
    uint32_t geometryOperations{};uint64_t geometrySkipped{};
    uint32_t next{1},batches{},observed{};uint64_t exhausted{};bool stopped{};
    std::ofstream file;
    std::filesystem::path armFile;
    bool armed{true};uint32_t armPoll{};
    TemporalAuditSlice& slice(uint32_t i){return *reinterpret_cast<TemporalAuditSlice*>(static_cast<char*>(mapped)+stride*i);}
    static void check(VkResult r){if(r!=VK_SUCCESS)throw std::runtime_error("Temporal audit Vulkan allocation/map failed");}
    uint32_t reserve(){
        if(!reusable.empty()){const auto i=reusable.back();reusable.pop_back();return i;}
        return next<slots?next++:0;
    }
    void reclaimMetadata(){
        for(uint32_t i=1;i<next;++i){auto& m=metadata[i];if(!m.set)continue;
            if(m.operation){if(m.released){m={};reusable.push_back(i);}continue;}
            const auto live=owners.find(m.set);
            if(live==owners.end()||live->second!=i){m={};reusable.push_back(i);}
        }
    }
public:
    TemporalAuditCapture(VkDevice d,const NativeDispatch& dispatch):device(d),fn(dispatch){}
    void initialize(const VkPhysicalDeviceMemoryProperties& properties,const VkPhysicalDeviceLimits& limits,const std::filesystem::path& path,const std::filesystem::path& trigger={},bool operations=false,bool geometryMode=false){
        perOperation=operations;geometry=geometryMode;
        if(geometry&&!perOperation)throw std::runtime_error("Geometry capture requires operation identity");
        if(!path.is_absolute()||std::filesystem::exists(path))throw std::runtime_error("Audit path must be absolute and new");
        if(!trigger.empty()&&(!trigger.is_absolute()||std::filesystem::exists(trigger)))throw std::runtime_error("Audit arm marker must be absolute and initially absent");
        armFile=trigger;armed=trigger.empty();
        if(limits.maxStorageBufferRange<sizeof(TemporalAuditSlice)||!limits.maxPerStageDescriptorStorageBuffers||!limits.maxDescriptorSetStorageBuffers)
            throw std::runtime_error("Audit storage buffer limits unavailable");
        if(perOperation&&!limits.maxDescriptorSetStorageBuffersDynamic)throw std::runtime_error("Audit dynamic storage buffer unavailable");
        const auto alignment=std::max<VkDeviceSize>(16,limits.minStorageBufferOffsetAlignment);
        stride=(sizeof(TemporalAuditSlice)+alignment-1)/alignment*alignment;
        if(stride*slots>UINT32_MAX)throw std::runtime_error("Audit dynamic storage offset exceeds uint32");
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=stride*slots;bi.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        check(fn.vkCreateBuffer(device,&bi,nullptr,&buffer));
        VkMemoryRequirements req{};fn.vkGetBufferMemoryRequirements(device,buffer,&req);uint32_t index=UINT32_MAX;
        for(uint32_t i=0;i<properties.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&
            (properties.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){index=i;break;}
        if(index==UINT32_MAX)throw std::runtime_error("Audit coherent memory unavailable");
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=index;
        check(fn.vkAllocateMemory(device,&ai,nullptr,&memory));check(fn.vkBindBufferMemory(device,buffer,memory,0));
        check(fn.vkMapMemory(device,memory,0,bi.size,0,&mapped));std::memset(mapped,0,size_t(bi.size));
        // Header writes occur only at device-idle frame boundaries, including
        // unused slices: allocate() may run concurrently with GPU execution.
        file.open(path,std::ios::out);if(!file)throw std::runtime_error("Cannot open audit evidence path");
        file<<"{\"kind\":\"header\",\"schema\":"<<(geometry?3:2)<<",\"mode\":\""<<(geometry?"geometry":"temporal")<<"\",\"maxGeometryOperationsPerBoundary\":64,\"recordsPerSlice\":8,\"maxSlices\":320,\"maxBatches\":32,\"timeoutBatches\":2400,\"operationBinding\":"<<(perOperation?"true":"false")<<",\"requiresArm\":"<<(armed?"false":"true")<<",\"jitterQualified\":false}\n";file.flush();
    }
    static bool supports(const VkDescriptorSetLayoutCreateInfo& info){
        if(info.pNext||info.flags||info.bindingCount!=7)return false;
        uint32_t mask{};
        for(uint32_t i=0;i<info.bindingCount;++i){const auto& b=info.pBindings[i];
            const bool uniform=b.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER||b.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            if(b.binding>=7||b.descriptorCount!=1||!(b.stageFlags&VK_SHADER_STAGE_FRAGMENT_BIT)||
                (b.binding<2?!uniform:b.descriptorType!=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER))return false;
            mask|=1u<<b.binding;
        }return mask==127;
    }
    bool geometryMode()const{return geometry;}
    // Exact layout seen with f70ac337f3d62c58. Keeping this narrow bounds parent
    // descriptor metadata and ensures binding 32 follows every game offset.
    static bool supportsGeometry(const VkDescriptorSetLayoutCreateInfo& info){
        if(info.pNext||info.flags||info.bindingCount!=24)return false;
        uint32_t mask{};
        for(uint32_t i=0;i<info.bindingCount;++i){const auto& b=info.pBindings[i];
            if(b.binding>=24||b.descriptorCount!=1||b.pImmutableSamplers)return false;
            const auto type=b.binding<4?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:b.binding<11?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                b.binding<21?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:b.binding<23?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            if(b.descriptorType!=type||b.stageFlags!=(b.binding<2?VK_SHADER_STAGE_VERTEX_BIT:VK_SHADER_STAGE_FRAGMENT_BIT))return false;
            mask|=1u<<b.binding;
        }return mask==0xffffff;
    }
    VkDescriptorBufferInfo allocate(VkDescriptorSet set){
        uint32_t slot{};
        if(!stopped&&(!reusable.empty()||next<slots)){
            slot=reserve();metadata[slot].set=set;metadata[slot].generation=++generationSequence;owners[set]=slot;
        }
        else if(!stopped)++exhausted;
        // Never recycle a slice before GPU retirement. On exhaustion use the
        // permanently disabled slice and report the bounded capacity.
        return {buffer,perOperation?0:stride*slot,sizeof(TemporalAuditSlice)};
    }
    // The private dynamic offset selects immutable per-recording storage. Its
    // lifetime includes executable command buffer reuse, not just one frame.
    // New/reclaimed slices have no GPU users; only their immutable token is
    // written here. Control/counter/frame writes remain at device-idle retire.
    uint32_t operation(VkDescriptorSet set,const TemporalAuditOperation& operation,std::vector<uint32_t>& owned){
        std::lock_guard<std::mutex> guard(operationMutex);
        if(!perOperation||stopped)return 0;
        const auto found=owners.find(set);if(found==owners.end())return 0;
        if(geometry&&geometryOperations++>=64){++geometrySkipped;return 0;}
        const auto i=reserve();if(!i){++operationExhausted;return 0;}
        auto& m=metadata[i];m.set=set;m.parent=found->second;m.parentGeneration=metadata[m.parent].generation;
        m.operation=++operationSequence;m.operationJson=operation.json();owned.push_back(i);
        auto& s=slice(i);s.operationLow=uint32_t(m.operation);s.operationHigh=uint32_t(m.operation>>32);
        return uint32_t(stride*i);
    }
    void releaseOperations(std::vector<uint32_t>& owned){
        std::lock_guard<std::mutex> guard(operationMutex);
        for(auto i:owned)metadata[i].released=true;owned.clear();
    }
    void forget(VkDescriptorSet set){owners.erase(set);}
    bool tracks(VkDescriptorSet set)const{return owners.find(set)!=owners.end();}
    bool finished()const{return stopped;}
    bool collecting()const{return armed&&!stopped;}
    void update(VkDescriptorSet set,uint32_t binding,const std::string& resource){
        const auto found=owners.find(set);if(found==owners.end()||binding>=7||stopped)return;
        auto& m=metadata[found->second];m.resources[binding]=resource;++m.updates[binding];
    }
    void copy(VkDescriptorSet src,uint32_t from,VkDescriptorSet dst,uint32_t to){
        auto found=owners.find(src);
        update(dst,to,found!=owners.end()&&from<7?metadata[found->second].resources[from]:"null");
    }
    void hostBarrier(VkCommandBuffer command)const{
        if(stopped)return;
        VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.buffer=buffer;b.size=VK_WHOLE_SIZE;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        fn.vkCmdPipelineBarrier(command,geometry?VK_PIPELINE_STAGE_VERTEX_SHADER_BIT:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&b,0,nullptr);
    }
    void retire(uint64_t nextSerial){
        if(stopped)return;
        geometryOperations=0;
        if(!armed){
            reclaimMetadata();
            if(++armPoll%60)return;
            std::error_code error;
            if(!std::filesystem::is_regular_file(armFile,error))return;
            armed=true;file<<"{\"kind\":\"armed\",\"nextFrame\":\""<<nextSerial<<"\"}\n";
        }
        bool hadSamples=false;
        for(uint32_t i=1;i<next;++i){auto& s=slice(i);auto& m=metadata[i];if(!s.count)continue;hadSamples=true;
            const bool matches=!m.operation||(m.parent&&metadata[m.parent].generation==m.parentGeneration);
            const auto& resources=m.operation&&matches?metadata[m.parent]:m;
            const auto serial=uint64_t(s.frameLow)|(uint64_t(s.frameHigh)<<32);
            file<<"{\"kind\":\"batch\",\"frame\":\""<<serial<<"\",\"slot\":"<<i<<",\"set\":\""<<reinterpret_cast<uintptr_t>(m.set)
                <<"\",\"count\":"<<s.count<<",\"overflow\":"<<(s.count>8?"true":"false")
                <<",\"operationId\":\""<<m.operation<<"\",\"descriptorGenerationMatches\":"<<(matches?"true":"false")
                <<",\"operation\":"<<(m.operationJson.empty()?"null":m.operationJson)<<",\"updates\":[";
            for(unsigned b=0;b<7;++b)file<<(b?",":"")<<resources.updates[b];
            file<<"],\"resources\":[";for(unsigned b=0;b<7;++b)file<<(b?",":"")<<(resources.resources[b].empty()?"null":resources.resources[b]);file<<"],\"records\":[";
            for(uint32_t n=0;n<std::min(s.count,8u);++n){file<<(n?",[":"[");for(unsigned w=0;w<160;++w)file<<(w?",":"")<<s.records[n].words[w];file<<']';}file<<"]}\n";
        }
        if(hadSamples)++observed;
        if(++batches>=timeoutBatches||observed>=captureBatches||!file){
            stopped=true;file<<"{\"kind\":\"complete\",\"observedBatches\":"<<observed<<",\"elapsedBatches\":"<<batches<<",\"allocatedSlices\":"<<next-1<<",\"exhaustedSets\":"<<exhausted<<",\"exhaustedOperations\":"<<operationExhausted<<",\"geometryOperationsSkipped\":"<<geometrySkipped<<"}\n";
        }
        for(uint32_t i=1;i<slots;++i){auto& s=slice(i);s.active=stopped?0:1;s.count=0;s.frameLow=uint32_t(nextSerial);s.frameHigh=uint32_t(nextSerial>>32);s.reserved0=s.reserved1=0;metadata[i].updates={};}
        reclaimMetadata();
        file.flush();
    }
    // Caller retires GPU first. Destructor also serves allocation-failure cleanup
    // before this buffer has ever been published to a descriptor set.
    ~TemporalAuditCapture(){if(mapped)fn.vkUnmapMemory(device,memory);if(buffer)fn.vkDestroyBuffer(device,buffer,nullptr);if(memory)fn.vkFreeMemory(device,memory,nullptr);}
};
}
