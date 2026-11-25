#include "../src/sfs/MappedUniforms.h"
#include "../src/sfs/DoomTemporalUniforms.h"
#include <array>
#include <iostream>
#include <stdexcept>
static void need(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main()try{
    using namespace kharvox::sfs;MappedUniforms a,otherDevice;
    auto memory=reinterpret_cast<VkDeviceMemory>(uintptr_t(1));auto buffer=reinterpret_cast<VkBuffer>(uintptr_t(2));
    std::array<unsigned char,1024> data{};for(size_t i=0;i<data.size();++i)data[i]=static_cast<unsigned char>(i%251);
    constexpr auto flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    a.allocate(memory,1024,flags);a.create(buffer,512,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,true);a.bind(buffer,memory,128);a.map(memory,64,768,data.data()+64);
    VkDescriptorBufferInfo info{buffer,32,128};std::array<unsigned char,64> result{};
    need(a.read(info,64,result.data(),result.size()),"Mapped suballocation/dynamic offset rejected");
    need(!std::memcmp(result.data(),data.data()+224,result.size()),"Wrong actual byte range");
    need(!otherDevice.read(info,64,result.data(),result.size()),"Cross-device memory accepted");
    need(!a.read(info,400,result.data(),result.size()),"Descriptor range overflow accepted");
    auto invalid=info;invalid.offset=VK_WHOLE_SIZE-20;need(!a.read(invalid,64,result.data(),result.size()),"Integer overflow accepted");
    invalid=info;invalid.range=VK_WHOLE_SIZE;need(!a.read(invalid,1,result.data(),result.size()),"Whole-size dynamic range overflow accepted");
    need(a.read(invalid,0,result.data(),result.size()),"Whole-size static binding rejected");
    a.map(memory,256,64,data.data()+256);need(!a.read(info,64,result.data(),result.size()),"Unmapped prefix accepted");
    a.map(memory,64,768,data.data()+64);a.unmap(memory);need(!a.read(info,64,result.data(),result.size()),"Stale unmapped pointer accepted");
    a.map(memory,0,VK_WHOLE_SIZE,data.data());a.free(memory);a.allocate(memory,1024,flags);a.map(memory,0,VK_WHOLE_SIZE,data.data());
    need(!a.read(info,64,result.data(),result.size()),"Reused allocation handle accepted without rebind");
    a.bind(buffer,memory,128);need(a.read(info,64,result.data(),result.size()),"New allocation rebind rejected");
    a.destroy(buffer);a.create(buffer,512,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,true);need(!a.read(info,64,result.data(),result.size()),"Reused buffer handle retained binding");
    a.bind(buffer,memory,128);a.free(memory);a.allocate(memory,1024,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);a.bind(buffer,memory,128);a.map(memory,0,VK_WHOLE_SIZE,data.data());
    need(!a.read(info,64,result.data(),result.size()),"Noncoherent bytes qualified without visibility proof");
    DoomTemporalUniforms uniforms;
    auto layout=reinterpret_cast<VkDescriptorSetLayout>(uintptr_t(3));auto pool=reinterpret_cast<VkDescriptorPool>(uintptr_t(4));
    auto set=reinterpret_cast<VkDescriptorSet>(uintptr_t(5)),copySet=reinterpret_cast<VkDescriptorSet>(uintptr_t(6));
    VkDescriptorSetLayoutBinding bindings[7]{};for(unsigned n=0;n<7;++n)bindings[n]={n,n<2?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};layoutInfo.bindingCount=7;layoutInfo.pBindings=bindings;
    uniforms.layout(layout,layoutInfo);uniforms.allocate(set,layout,pool);uniforms.allocate(copySet,layout,pool);
    uniforms.memory.allocate(memory,data.size(),flags);uniforms.memory.create(buffer,data.size(),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,true);uniforms.memory.bind(buffer,memory,0);uniforms.memory.map(memory,0,VK_WHOLE_SIZE,data.data());
    VkDescriptorBufferInfo ranges[]{{buffer,0,224},{buffer,512,112}};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set;write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;write.descriptorCount=2;write.pBufferInfo=ranges;
    uniforms.update(1,&write,0,nullptr);DoomTemporalUniformSnapshot snapshot;
    need(uniforms.read(set,{64,128},snapshot),"Dynamic descriptor read rejected");
    need(!std::memcmp(snapshot.high.data(),data.data()+64,224)&&!std::memcmp(snapshot.low.data(),data.data()+640,112),"Dynamic descriptor offsets/order incorrect");
    need(!uniforms.read(set,{64},snapshot),"Missing dynamic offset accepted");
    VkCopyDescriptorSet copy{VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};copy.srcSet=set;copy.dstSet=copySet;copy.descriptorCount=2;uniforms.update(0,nullptr,1,&copy);
    uniforms.forget(set);need(uniforms.read(copySet,{64,128},snapshot),"Descriptor copy did not retain independent binding state");
    uniforms.forgetLayout(layout);need(uniforms.read(copySet,{64,128},snapshot),"Destroying layout incorrectly invalidated existing set");
    copy.srcSet=copySet;copy.dstSet=copySet;copy.srcBinding=1;copy.dstBinding=0;copy.descriptorCount=1;uniforms.update(0,nullptr,1,&copy);
    need(!uniforms.read(copySet,{64,128},snapshot),"Short copied range accepted for high constants");
    uniforms.pool(pool);need(!uniforms.read(copySet,{64,128},snapshot),"Pool reset retained descriptor binding");
    std::cout<<"PASS mapped uniform ranges, dynamic descriptors/copies, device separation, overflow and allocation/buffer/map/pool lifetime\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
