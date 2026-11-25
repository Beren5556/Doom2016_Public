#include "../src/sfs/TemporalContextVulkan.h"
#include "../src/common/TemporalProjectionObservation.h"
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace tc=kharvox::temporal_context;
static void need(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static std::string read(const std::filesystem::path& path){std::ifstream f(path);return {(std::istreambuf_iterator<char>(f)),{}};}
template<class T> T h(uint64_t value){return reinterpret_cast<T>(uintptr_t(value));}
int main(){try{
    const auto directory=std::filesystem::temp_directory_path()/("kharvox-context-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(directory);
    const auto path=directory/"context.jsonl";tc::Limits limits;limits.intervals=3;auto capture=std::make_shared<tc::Capture>(1,path,limits);need(tc::publish(capture),"Publish failed");need(!tc::publish(capture),"Multiple devices accepted");
    tc::VulkanMetadata metadata(capture);
    VkDescriptorSetLayoutBinding bindings[]={{2,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},{0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,2,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,0,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};layout.bindingCount=3;layout.pBindings=bindings;
    metadata.layout(h<VkDescriptorSetLayout>(10),layout);metadata.allocate(h<VkDescriptorSet>(20),h<VkDescriptorSetLayout>(10),h<VkDescriptorPool>(30));metadata.allocate(h<VkDescriptorSet>(21),h<VkDescriptorSetLayout>(10),h<VkDescriptorPool>(31));
    VkDescriptorImageInfo images[]={{VK_NULL_HANDLE,h<VkImageView>(101),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{VK_NULL_HANDLE,h<VkImageView>(102),VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=h<VkDescriptorSet>(20);write.dstBinding=0;write.dstArrayElement=1;write.descriptorCount=2;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;write.pImageInfo=images;
    metadata.update(1,&write,0,nullptr);
    VkCopyDescriptorSet copy{VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};copy.srcSet=write.dstSet;copy.srcBinding=0;copy.srcArrayElement=1;copy.dstSet=h<VkDescriptorSet>(21);copy.dstBinding=0;copy.descriptorCount=2;metadata.update(0,nullptr,1,&copy);
    metadata.forgetLayout(h<VkDescriptorSetLayout>(10));metadata.pool(h<VkDescriptorPool>(30));
    // Old set contents must not survive a reused handle; allocated copies keep
    // their immutable layout shape even after layout and source pool destruction.
    metadata.layout(h<VkDescriptorSetLayout>(10),layout);metadata.allocate(h<VkDescriptorSet>(20),h<VkDescriptorSetLayout>(10),h<VkDescriptorPool>(30));
    capture->event("shouldNotExist","{}");capture->boundary(8,false);need(read(path).find("shouldNotExist")==std::string::npos,"Events before arming");
    capture->boundary(0x100000007ULL,true);need(capture->active(),"Capture did not arm");
    std::vector<std::thread> workers;for(unsigned t=0;t<4;++t)workers.emplace_back([&,t]{for(unsigned n=0;n<500;++n)capture->event("parallel","["+std::to_string(t)+","+std::to_string(n)+"]");});for(auto& worker:workers)worker.join();
    std::array<unsigned char,0x2000> view{},model{};std::array<uint32_t,16> output{};
    for(unsigned n=0;n<16;++n)output[n]=n==3?0x7fc01234u:n;
    std::memcpy(view.data()+0x1130,output.data(),64);std::memcpy(view.data()+0x1170,output.data(),64);std::memcpy(model.data()+0xb24,output.data(),64);model[0xba]=8;
    size_t reads{};bool outOfBounds=false;auto reader=[&](uintptr_t address,void* target,size_t size){++reads;auto within=[&](const auto& array){const auto begin=reinterpret_cast<uintptr_t>(array.data());return address>=begin&&address-begin<=sizeof(array)&&size<=sizeof(array)-(address-begin);};if(!within(view)&&!within(model)&&!within(output)){outOfBounds=true;return false;}std::memcpy(target,reinterpret_cast<void*>(address),size);return true;};
    auto ignored=tc::ProjectionObservation::modelBefore(false,reader,tc::handle(model.data()),tc::handle(model.data()),tc::handle(view.data()),tc::handle(view.data()),0,0,0,true,9,10);need(!ignored.capture&&!reads,"Unguarded memory read");
    auto observed=tc::ProjectionObservation::modelBefore(true,reader,tc::handle(model.data()),tc::handle(model.data()),tc::handle(view.data()),tc::handle(view.data()),1,2,3,true,9,10);observed.modelAfter(reader,tc::handle(output.data()));need(!outOfBounds,"Observation read outside buffers");
    // Copied views end at 0x11b0. Reading V or VP from that local copy would be
    // an out-of-bounds stack read even if VirtualQuery says the page is readable.
    std::array<unsigned char,0x2000> copiedView{};auto boundedReader=[&](uintptr_t address,void* target,size_t size){const auto begin=tc::handle(copiedView.data());if(address>=begin&&address<begin+0x2000){need(address-begin+size<=0x11b0,"Read past selected view copy");std::memcpy(target,reinterpret_cast<void*>(address),size);return true;}return reader(address,target,size);};
    auto copied=tc::ProjectionObservation::modelBefore(true,boundedReader,tc::handle(model.data()),tc::handle(model.data()),tc::handle(view.data()),tc::handle(copiedView.data()),0,0,0,false,9,10);copied.modelAfter(reader,tc::handle(output.data()));need((copied.readable&12)==0,"Unknown copied matrices qualified");
    for(unsigned n=0;n<3;++n)need(capture->sample("limit",3),"Sampling rejected too soon");need(!capture->sample("limit",3),"Sampling bound ignored");
    VkCommandBuffer command=h<VkCommandBuffer>(1000);VkSemaphore wait=h<VkSemaphore>(2000),signal=h<VkSemaphore>(2001);VkPipelineStageFlags stage=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;uint64_t waitValue=12,signalValue=13;
    VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};timeline.waitSemaphoreValueCount=timeline.signalSemaphoreValueCount=1;timeline.pWaitSemaphoreValues=&waitValue;timeline.pSignalSemaphoreValues=&signalValue;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.pNext=&timeline;submit.commandBufferCount=submit.waitSemaphoreCount=submit.signalSemaphoreCount=1;submit.pCommandBuffers=&command;submit.pWaitSemaphores=&wait;submit.pWaitDstStageMask=&stage;submit.pSignalSemaphores=&signal;
    tc::submitted(1,h<VkQueue>(40),1,&submit,h<VkFence>(41),VK_ERROR_DEVICE_LOST);tc::submitted(2,h<VkQueue>(40),1,&submit,h<VkFence>(41),VK_SUCCESS);
    capture->boundary(0x100000007ULL,true);need(capture->active(),"Same serial consumed interval");capture->boundary(0x100000008ULL,true);capture->boundary(0x100000009ULL,true);capture->boundary(0x10000000aULL,true);need(capture->finished(),"Interval stop failed");
    auto text=read(path);need(text.find("\"intervals\":3,\"losses\":0,\"exceptions\":0")!=std::string::npos,"Unexpected loss");need(text.find("2143294004")!=std::string::npos,"Float payload changed");need(text.find("\"sampledOut\":{\"limit\":1}")!=std::string::npos,"Sampling omission missing");
    const auto size=std::filesystem::file_size(path);capture->event("afterStop","{}");capture->finish("again");need(std::filesystem::file_size(path)==size,"Wrote after finish");tc::unpublish(capture);need(!tc::current(),"Context remained published");
    bool rejected=false;try{tc::Capture duplicate(1,path);}catch(...){rejected=true;}need(rejected&&std::filesystem::file_size(path)==size,"Existing capture overwritten");
    tc::Limits small;small.events=2;tc::Capture capacity(2,directory/"capacity.jsonl",small);capacity.boundary(1,true);capacity.event("one","{}");capacity.event("two","{}");need(!capacity.active(),"Capture exceeded event cap");capacity.boundary(2,true);need(read(directory/"capacity.jsonl").find("\"reason\":\"event-capacity\"")!=std::string::npos,"Capacity loss not explicit");
    tc::Limits tiny;tiny.metadataBytes=1;tc::Capture missing(3,directory/"metadata-loss.jsonl",tiny);missing.define("image",1,"{}");missing.boundary(1,true);missing.finish("test");need(read(directory/"metadata-loss.jsonl").find("\"losses\":1")!=std::string::npos,"Missing metadata qualified");
    tc::Capture partial(4,directory/"partial.jsonl");partial.finish("shutdown");need(read(directory/"partial.jsonl").find("\"started\":false")!=std::string::npos,"Unarmed shutdown misreported");
    auto unsupported=std::make_shared<tc::Capture>(5,directory/"unsupported.jsonl");tc::VulkanMetadata unknown(unsupported);
    unknown.layout(h<VkDescriptorSetLayout>(10),layout);unknown.allocate(h<VkDescriptorSet>(20),h<VkDescriptorSetLayout>(10),h<VkDescriptorPool>(30));
    unknown.update(1,&write,0,nullptr);unsupported->boundary(1,true);
    VkBaseInStructure extension{};write.pNext=&extension;unknown.update(1,&write,0,nullptr);
    unsupported->protect([]{throw std::runtime_error("diagnostic failure");});unsupported->finish("test");
    need(read(directory/"unsupported.jsonl").find("\"unknown\":true")!=std::string::npos,"Unknown descriptor write left qualified cells");
    need(read(directory/"unsupported.jsonl").find("\"exceptions\":1")!=std::string::npos,"Diagnostic exception escaped or disappeared");
    // A live capture exposed losses without any gap rows: pre-arm reasons must
    // survive arming, and metadata/event caps must identify their own cause.
    tc::Limits diagnosticBounds;diagnosticBounds.metadataBytes=4;diagnosticBounds.objects=1;
    tc::Capture diagnostic(6,directory/"loss-reasons.jsonl",diagnosticBounds);
    diagnostic.gap("pipeline-extension");diagnostic.gap("pipeline-extension");
    diagnostic.define("image",1,"{}");diagnostic.define("image",2,"{}");
    diagnostic.define("image",1,"{\"oversize\":true}");diagnostic.boundary(1,true);
    diagnostic.gap("pipeline-extension");diagnostic.define("image",2,"{}");
    diagnostic.finish("test");const auto reasons=read(directory/"loss-reasons.jsonl");
    need(reasons.find("\"pipeline-extension\":{\"beforeArm\":2,\"afterArm\":1}")!=std::string::npos,"Pre-arm gap reasons disappeared");
    need(reasons.find("\"metadata-object-capacity\":{\"beforeArm\":1,\"afterArm\":1}")!=std::string::npos,"Object cap reason missing");
    need(reasons.find("\"metadata-byte-capacity\":{\"beforeArm\":1,\"afterArm\":0}")!=std::string::npos,"Byte cap reason missing");
    need(reasons.find("\"losses\":6")!=std::string::npos&&reasons.find("\"peakMetadataBytes\":2,\"peakObjects\":1")!=std::string::npos,"Loss/peak totals incorrect");
    need(read(directory/"capacity.jsonl").find("\"event-count-capacity\":{\"beforeArm\":0,\"afterArm\":1}")!=std::string::npos,"Event count loss reason missing");
    tc::Limits byteBound;byteBound.eventBytes=1;tc::Capture bytes(7,directory/"event-bytes.jsonl",byteBound);bytes.boundary(1,true);bytes.boundary(2,true);
    need(read(directory/"event-bytes.jsonl").find("\"event-byte-capacity\":{\"beforeArm\":0,\"afterArm\":1}")!=std::string::npos,"Event byte loss reason missing");
    tc::Capture many(8,directory/"bounded-reasons.jsonl");
    workers.clear();for(unsigned t=0;t<4;++t)workers.emplace_back([&,t]{for(unsigned n=0;n<100;++n)many.gap("reason-"+std::to_string(t*100+n));});for(auto& worker:workers)worker.join();
    many.gap(std::string(1000,'x'));many.finish("test");const auto bounded=read(directory/"bounded-reasons.jsonl");
    need(bounded.find("\"losses\":401")!=std::string::npos&&bounded.find("\"other-reasons\":{\"beforeArm\":338,\"afterArm\":0}")!=std::string::npos,"Bounded reasons lost concurrent totals");
    need(bounded.find(std::string(129,'x'))==std::string::npos,"Unbounded reason stored");
    std::cout<<"PASS bounded context, pre-arm descriptor arrays/copies/lifetimes, guarded matrix reads, concurrent records, submissions and explicit loss. Evidence: "<<directory.string()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
