#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace kharvox::sfs {
// Device-local metadata, externally synchronized by the SFS state mutex.
// Reads only currently mapped, coherent, host-visible uniform memory. No map,
// GPU copy, wait, fallback zero values or retention of an unmapped pointer.
// Observing these bytes does not prove that a later GPU draw consumed them.
class MappedUniforms {
    struct Memory {VkDeviceSize size{},offset{},mappedSize{};uint64_t generation{};const unsigned char* pointer{};bool coherent{};};
    struct Buffer {VkDeviceSize size{},offset{};VkDeviceMemory memory{};uint64_t generation{};bool uniform{};};
    std::unordered_map<VkDeviceMemory,Memory> memories;
    std::unordered_map<VkBuffer,Buffer> buffers;
    uint64_t generation{};
    static bool contains(VkDeviceSize size,VkDeviceSize offset,VkDeviceSize bytes){return offset<=size&&bytes<=size-offset;}
public:
    void allocate(VkDeviceMemory id,VkDeviceSize size,VkMemoryPropertyFlags flags){
        memories.erase(id);if(!id||!size||memories.size()>=65536)return;
        const auto required=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        memories.emplace(id,Memory{size,0,0,++generation,nullptr,(flags&required)==required});
    }
    void free(VkDeviceMemory id){memories.erase(id);}
    void create(VkBuffer id,VkDeviceSize size,VkBufferUsageFlags usage,bool supported){
        buffers.erase(id);if(!id||!size||buffers.size()>=65536)return;
        buffers.emplace(id,Buffer{size,0,{},0,supported&&bool(usage&VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)});
    }
    void destroy(VkBuffer id){buffers.erase(id);}
    void bind(VkBuffer id,VkDeviceMemory memory,VkDeviceSize offset){
        auto b=buffers.find(id);if(b==buffers.end())return;b->second.memory={};b->second.generation=0;
        const auto m=memories.find(memory);if(m==memories.end()||!contains(m->second.size,offset,b->second.size))return;
        b->second.memory=memory;b->second.offset=offset;b->second.generation=m->second.generation;
    }
    void map(VkDeviceMemory id,VkDeviceSize offset,VkDeviceSize size,const void* pointer){
        auto m=memories.find(id);if(m==memories.end())return;auto& v=m->second;v.pointer=nullptr;v.mappedSize=0;
        if(offset>v.size)return;if(size==VK_WHOLE_SIZE)size=v.size-offset;
        if(!pointer||!size||!contains(v.size,offset,size)||size>std::numeric_limits<size_t>::max())return;
        v.offset=offset;v.mappedSize=size;v.pointer=static_cast<const unsigned char*>(pointer);
    }
    void unmap(VkDeviceMemory id){auto m=memories.find(id);if(m!=memories.end()){m->second.pointer=nullptr;m->second.mappedSize=0;}}
    bool read(const VkDescriptorBufferInfo& descriptor,uint32_t dynamic,void* output,size_t bytes)const{
        const auto b=buffers.find(descriptor.buffer);if(!output||!bytes||b==buffers.end()||!b->second.uniform)return false;
        const auto& buffer=b->second;const auto m=memories.find(buffer.memory);
        if(m==memories.end()||m->second.generation!=buffer.generation||!m->second.coherent||!m->second.pointer)return false;
        if(descriptor.offset>buffer.size||dynamic>buffer.size-descriptor.offset)return false;
        const VkDeviceSize effective=descriptor.offset+dynamic;
        const VkDeviceSize range=descriptor.range==VK_WHOLE_SIZE?buffer.size-descriptor.offset:descriptor.range;
        // Dynamic offset translates the complete descriptor range, not just the
        // bytes requested by the observer. Invalid bindings never qualify.
        if(!contains(buffer.size,effective,range)||bytes>range||!contains(buffer.size,effective,bytes))return false;
        const auto& memory=m->second;const auto absolute=buffer.offset+effective;
        if(absolute<memory.offset||!contains(memory.mappedSize,absolute-memory.offset,bytes))return false;
        std::memcpy(output,memory.pointer+size_t(absolute-memory.offset),bytes);return true;
    }
};
}
