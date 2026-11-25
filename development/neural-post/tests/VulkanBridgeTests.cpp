#include "DoomNeuralVulkan.h"
#include "NeuralPostSettings.h"
#include "VulkanTestDevice.h"
#include <iostream>
using namespace es2np;
static void Expect(bool b,const char* what){if(!b)throw std::runtime_error(what);}
static std::vector<const char*> Extensions(VkInstance instance,VkPhysicalDevice physical){if(!physical)return {};return {"VK_KHR_external_memory","VK_KHR_external_memory_win32","VK_KHR_get_memory_requirements2","VK_KHR_dedicated_allocation"};}
int main(int argc,char** argv){try{
 const unsigned diagnostic=argc>1?unsigned(std::atoi(argv[1])):3;
 if(argc>2&&std::string(argv[2])=="config-disabled"){doomnp::VulkanPost p;Expect(!p.enabled(),"bad/absent configuration accepted");std::cout<<"PASS disabled configuration: no GPU/worker initialized\n";return 0;}
 const bool missing=argc>2&&std::string(argv[2])=="missing";
 VulkanTestDevice gpu(false,{160,128},Extensions);KharvoxVulkanDispatch dispatch{};dispatch.getDeviceProcAddr=gpu.resolve;
#define LOAD(field,name) dispatch.field=reinterpret_cast<PFN_##name>(gpu.resolve(gpu.device,#name));Expect(dispatch.field!=nullptr,#name)
 LOAD(createCommandPool,vkCreateCommandPool);LOAD(destroyCommandPool,vkDestroyCommandPool);LOAD(allocateCommandBuffers,vkAllocateCommandBuffers);LOAD(resetCommandBuffer,vkResetCommandBuffer);LOAD(beginCommandBuffer,vkBeginCommandBuffer);LOAD(endCommandBuffer,vkEndCommandBuffer);LOAD(cmdPipelineBarrier,vkCmdPipelineBarrier);LOAD(cmdCopyImage,vkCmdCopyImage);LOAD(createImage,vkCreateImage);LOAD(destroyImage,vkDestroyImage);LOAD(getImageMemoryRequirements,vkGetImageMemoryRequirements);LOAD(allocateMemory,vkAllocateMemory);LOAD(freeMemory,vkFreeMemory);LOAD(bindImageMemory,vkBindImageMemory);LOAD(createFence,vkCreateFence);LOAD(destroyFence,vkDestroyFence);LOAD(resetFences,vkResetFences);LOAD(waitForFences,vkWaitForFences);LOAD(queueSubmit,vkQueueSubmit);
#undef LOAD
 dispatch.getPhysicalDeviceProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gpu.gipa(gpu.instance,"vkGetPhysicalDeviceProperties"));
 dispatch.getPhysicalDeviceProperties2=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(gpu.gipa(gpu.instance,"vkGetPhysicalDeviceProperties2"));
 doomnp::VulkanPost post(diagnostic);Expect(post.enabled(),"test INI disabled");uint64_t changed{},checked{};
 for(int step=0;step<5;++step){gpu.extent=step==4?VkExtent2D{192,160}:VkExtent2D{160,128};auto w=gpu.extent.width,h=gpu.extent.height;
  const auto fmt=step==4?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_B8G8R8A8_UNORM;
  auto left=gpu.MakeTexture(fmt,VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
  auto right=gpu.MakeTexture(fmt,VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
  std::vector<uint8_t> pattern[2];VulkanTestDevice::Buffer upload[2],download[2];
  for(int e=0;e<2;++e){pattern[e].resize(w*h*4*2);for(size_t i=0;i<pattern[e].size();i+=4){pattern[e][i]=uint8_t(25+(i/4)%73);pattern[e][i+1]=uint8_t(40+((i/4)/w)%83);pattern[e][i+2]=e?55:215;pattern[e][i+3]=uint8_t(70+(i/4)%150);}
   upload[e]=gpu.MakeBuffer(pattern[e].size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);download[e]=gpu.MakeBuffer(pattern[e].size(),VK_BUFFER_USAGE_TRANSFER_DST_BIT);gpu.Write(upload[e],pattern[e].data());}
  gpu.Begin();gpu.Upload(left,upload[0],VK_IMAGE_LAYOUT_UNDEFINED);gpu.Upload(right,upload[1],VK_IMAGE_LAYOUT_UNDEFINED);gpu.Barrier(left,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);gpu.Barrier(right,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);gpu.Finish();
  Frame f;f.epoch=1;f.generation=step==4?2:1;f.source=step+1;f.timeNs=uint64_t(step+1)*11000000;
  for(auto& e:f.eyes){e.orientation[3]=1;e.fov[0]=-.8f;e.fov[1]=.8f;e.fov[2]=.8f;e.fov[3]=-.8f;}
  doomnp::EyeImage eyes[2]{{left.handle,{{8,8},{w-16,h-16}},gpu.extent},{right.handle,{{8,8},{w-16,h-16}},gpu.extent}};
  if(step==2)post.suspend();
  if(step==3){auto invalid=f;invalid.eyes[0].fov[0]=NAN;Expect(!post.process(gpu.physical,gpu.device,gpu.queue,gpu.family,dispatch,fmt,eyes,invalid),"invalid pose accepted");Expect(post.enabled()&&post.gpuReady(),"invalid pose faulted session");}
  const auto ok=post.process(gpu.physical,gpu.device,gpu.queue,gpu.family,dispatch,fmt,eyes,f);if(!ok)std::cerr<<post.message()<<'\n';Expect(ok!=missing,"Vulkan/Neural processing");Expect(post.gpuReady(),"GPU outstanding");
  if(!missing)Expect(post.process(gpu.physical,gpu.device,gpu.queue,gpu.family,dispatch,fmt,eyes,f),"repeat frame");
  else Expect(!post.enabled(),"fault retried");
  gpu.Begin();gpu.Barrier(left,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);gpu.Download(left,download[0]);gpu.Barrier(right,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);gpu.Download(right,download[1]);gpu.Finish();
  for(int e=0;e<2;++e){auto floats=gpu.Read(download[e]);const auto* bytes=reinterpret_cast<const uint8_t*>(floats.data());for(size_t i=0;i<pattern[e].size();++i){auto px=i/4;bool inside=px<w*h&&px%w>=8&&px%w<w-8&&px/w>=8&&px/w<h-8;
   if(!inside||i%4==3||diagnostic==3||missing)Expect(bytes[i]==pattern[e][i],"identity/alpha/outside-eye changed");if(inside&&bytes[i]!=pattern[e][i])++changed;++checked;}}
  if(missing){Expect(post.reset(),"fault teardown");std::cout<<"PASS missing model: original pair preserved, no retry, GPU retired\n";return 0;}
 }
 Expect(diagnostic==3||changed>0,"model produced no changed pixels");Expect(post.reset(),"bridge teardown");
 std::cout<<"PASS Vulkan/D3D12/worker/Vulkan checked="<<checked<<" changed="<<changed<<" diagnostic="<<diagnostic<<"; eyes/crops/alpha/repeat/suspend/resize/format verified\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
