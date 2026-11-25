#pragma once
#include <vulkan/vulkan.h>
namespace kharvox::sfs {
// DOOM's final scene pass retains a read-only depth/stencil attachment even
// though its scene shader only writes color. Splitting must preserve both.
inline bool doomFinalPass(const VkRenderPassCreateInfo& i){
    if(i.pNext||i.flags||i.subpassCount!=1||!i.pSubpasses||!i.pAttachments||i.attachmentCount<1||i.attachmentCount>2)return false;
    const auto& s=i.pSubpasses[0];
    if(s.flags||s.pipelineBindPoint!=VK_PIPELINE_BIND_POINT_GRAPHICS||s.colorAttachmentCount!=1||!s.pColorAttachments||
       s.inputAttachmentCount||s.pResolveAttachments||s.preserveAttachmentCount||s.pColorAttachments[0].attachment!=0||
       s.pColorAttachments[0].layout!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)return false;
    const auto& color=i.pAttachments[0];
    if(color.samples!=VK_SAMPLE_COUNT_1_BIT||color.storeOp!=VK_ATTACHMENT_STORE_OP_STORE||
       !((color.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD&&color.initialLayout==color.finalLayout&&
          (color.initialLayout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL||color.initialLayout==VK_IMAGE_LAYOUT_GENERAL))||
         (color.loadOp==VK_ATTACHMENT_LOAD_OP_DONT_CARE&&color.initialLayout==VK_IMAGE_LAYOUT_UNDEFINED)))return false;
    const auto* ref=s.pDepthStencilAttachment;
    if(!ref||ref->attachment==VK_ATTACHMENT_UNUSED)return i.attachmentCount==1;
    if(i.attachmentCount!=2||ref->attachment!=1||ref->layout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)return false;
    const auto& depth=i.pAttachments[1];
    return depth.samples==VK_SAMPLE_COUNT_1_BIT&&depth.initialLayout==ref->layout&&depth.finalLayout==ref->layout&&
        depth.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD&&depth.storeOp==VK_ATTACHMENT_STORE_OP_STORE&&
        depth.stencilLoadOp==VK_ATTACHMENT_LOAD_OP_LOAD&&depth.stencilStoreOp==VK_ATTACHMENT_STORE_OP_STORE;
}
}
