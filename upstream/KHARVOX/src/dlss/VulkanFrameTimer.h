#pragma once
#include "VulkanDlss.h"
#include <vector>
#include <cmath>
namespace kharvox::dlss {
// One outstanding recording, retired with the host's existing last-reader proof.
// End-to-end GPU timestamp interval includes queue waits; processingMs does not.
class VulkanFrameTimer {
    VkDevice device{};VkQueryPool pool{};uint64_t previous{},mask{~uint64_t(0)},sequence{};double period{};
    PFN_vkDestroyQueryPool destroy{};PFN_vkCmdResetQueryPool reset{};PFN_vkCmdWriteTimestamp stamp{};PFN_vkGetQueryPoolResults read{};
    bool recorded{};
public:
    double intervalMs{},processingMs{};
    explicit VulkanFrameTimer(const Options& o):device(o.device){
        const auto props=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(o.gipa(o.instance,"vkGetPhysicalDeviceProperties"));
        const auto queues=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(o.gipa(o.instance,"vkGetPhysicalDeviceQueueFamilyProperties"));
        if(!props||!queues)return;uint32_t count{};queues(o.physical,&count,nullptr);std::vector<VkQueueFamilyProperties> q(count);queues(o.physical,&count,q.data());
        if(o.queueFamily>=count||!q[o.queueFamily].timestampValidBits)return;
        const auto bits=q[o.queueFamily].timestampValidBits;mask=bits==64?~uint64_t(0):(uint64_t(1)<<bits)-1;
        VkPhysicalDeviceProperties p{};props(o.physical,&p);period=p.limits.timestampPeriod;
        const auto create=reinterpret_cast<PFN_vkCreateQueryPool>(o.gdpa(device,"vkCreateQueryPool"));
        destroy=reinterpret_cast<PFN_vkDestroyQueryPool>(o.gdpa(device,"vkDestroyQueryPool"));reset=reinterpret_cast<PFN_vkCmdResetQueryPool>(o.gdpa(device,"vkCmdResetQueryPool"));stamp=reinterpret_cast<PFN_vkCmdWriteTimestamp>(o.gdpa(device,"vkCmdWriteTimestamp"));read=reinterpret_cast<PFN_vkGetQueryPoolResults>(o.gdpa(device,"vkGetQueryPoolResults"));
        if(!create||!destroy||!reset||!stamp||!read||period<=0)return;
        VkQueryPoolCreateInfo ci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};ci.queryType=VK_QUERY_TYPE_TIMESTAMP;ci.queryCount=2;if(create(device,&ci,nullptr,&pool)!=VK_SUCCESS)pool={};
    }
    ~VulkanFrameTimer(){if(pool)destroy(device,pool,nullptr);}
    void begin(VkCommandBuffer cb){if(pool){reset(cb,pool,0,2);stamp(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,pool,0);recorded=false;}}
    void end(VkCommandBuffer cb){if(pool){stamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,pool,1);recorded=true;}}
    uint64_t completed(){
        if(!pool||!recorded)return 0;recorded=false;uint64_t values[2]{};
        if(read(device,pool,0,2,sizeof(values),values,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT)!=VK_SUCCESS){previous=0;return 0;}
        const auto delta=[&](uint64_t a,uint64_t b){return double((a-b)&mask)*period/1e6;};
        processingMs=delta(values[1],values[0]);intervalMs=previous?delta(values[1],previous):0;previous=values[1];
        if(!std::isfinite(intervalMs)||intervalMs>250)intervalMs=0;return ++sequence;
    }
    void discarded(){recorded=false;previous=0;intervalMs=processingMs=0;}
};
}
