#include "VulkanTestDevice.h"
#include <dlss_foveado/Vulkan.h>
#include <array>
#include <cmath>
using namespace dlss_foveado;
static void need(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(){try{
    VulkanTestDevice g;vulkan::Operations ops({g.device,g.resolve,g.memory,true});
    const auto sourceUsage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const auto targetUsage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto a=g.MakeTexture(VK_FORMAT_R32G32B32A32_SFLOAT,sourceUsage),b=g.MakeTexture(VK_FORMAT_R32G32B32A32_SFLOAT,sourceUsage),out=g.MakeTexture(VK_FORMAT_R32G32B32A32_SFLOAT,targetUsage);
    const auto uploadA=g.MakeBuffer(512*sizeof(float),VK_BUFFER_USAGE_TRANSFER_SRC_BIT),uploadB=g.MakeBuffer(512*sizeof(float),VK_BUFFER_USAGE_TRANSFER_SRC_BIT),read=g.MakeBuffer(512*sizeof(float),VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    std::vector<VkImageView> views;
    auto eyeView=[&](VulkanTestDevice::Texture t,unsigned eye,VkImageUsageFlags usage,VkImageLayout layout){vulkan::Image result;result.device=g.device;result.image=t.handle;result.format=t.format;result.extent={8,8};result.usage=usage;result.layout=layout;result.range.baseArrayLayer=eye;
        VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};ci.image=t.handle;ci.viewType=VK_IMAGE_VIEW_TYPE_2D;ci.format=t.format;ci.subresourceRange=result.range;VulkanTestDevice::Check(g.vkCreateImageView(g.device,&ci,nullptr,&result.view));views.push_back(result.view);return result;};
    std::array<vulkan::Image,2> inputs0,inputs1,outputs;
    for(unsigned e=0;e<2;++e){inputs0[e]=eyeView(a,e,sourceUsage,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);inputs1[e]=eyeView(b,e,sourceUsage,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);outputs[e]=eyeView(out,e,targetUsage,VK_IMAGE_LAYOUT_UNDEFINED);}
    std::array<float,512> dataA{},dataB{};
    for(unsigned e=0;e<2;++e)for(unsigned n=0;n<64;++n){const auto k=(e*64+n)*4;dataA[k]=e?.3f:.1f;dataA[k+2]=e?.1f:.7f;dataA[k+3]=.4f;dataB[k]=e?.2f:.9f;dataB[k+2]=e?.8f:.2f;dataB[k+3]=1;}
    g.Write(uploadA,dataA.data());g.Write(uploadB,dataB.data());g.Begin();g.Upload(a,uploadA,VK_IMAGE_LAYOUT_UNDEFINED);g.Upload(b,uploadB,VK_IMAGE_LAYOUT_UNDEFINED);g.Finish();
    auto readOutput=[&](){g.Barrier(out,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);g.Download(out,read);g.Barrier(out,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);};
    auto retire=[&](auto& jobs){g.Finish();for(auto& job:jobs){job->Submitted(g.done);need(job->Complete(),"Foveated operation has not retired");}jobs.clear();for(auto& output:outputs)output.layout=VK_IMAGE_LAYOUT_GENERAL;};
    for(auto shape:{Shape::Ellipse,Shape::Rectangle})for(bool srgb:{false,true}){
        Config config;config.enabled=true;config.central.shape=shape;config.central.topBps=1500;config.central.bottomBps=2500;config.central.rightBps=2000;config.central.leftBps=3000;config.central.transitionBps=2500;
        std::array<ViewGeometry,2> geometry{};for(unsigned e=0;e<2;++e){auto& v=geometry[e];v.viewId=e+1;v.input=v.output={8,8};v.binocular={{.125,0,e?.1:-.1,0,.125,0,0,0,1},42,1};}
        const auto plan=Plan(config,geometry,FixedPlacement(config),1,1,1,{1,64});g.Begin();std::vector<std::unique_ptr<vulkan::Recorded>> jobs;
        for(unsigned e=0;e<2;++e)jobs.push_back(ops.Composite(g.command,inputs0[e],inputs1[e],plan.views[e].central,outputs[e],srgb));readOutput();retire(jobs);auto pixels=g.Read(read);
        auto decode=[](double c){return c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4);};auto encode=[](double c){return c<=.0031308?c*12.92:1.055*std::pow(c,1./2.4)-.055;};
        for(unsigned e=0;e<2;++e)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){const auto weight=CentralWeight(plan.views[e].central,{x+.5,y+.5});const auto k=(e*64+y*8+x)*4;for(unsigned c=0;c<4;++c){const auto expected=srgb&&c<3?encode(decode(dataA[k+c])*(1-weight)+decode(dataB[k+c])*weight):dataA[k+c]*(1-weight)+dataB[k+c]*weight;need(std::isfinite(pixels[k+c])&&std::abs(pixels[k+c]-expected)<.00002,"Vulkan binocular mask differs from 1.3 CPU oracle");}}
        std::cout<<"PASS Vulkan composition shape="<<unsigned(shape)<<" srgb="<<srgb<<" asymmetric binocular mappings, two layers, alpha\n";
    }
    // Exact crop fast path and nearest resampling with MV component conversion.
    for(unsigned e=0;e<2;++e)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){auto k=(e*64+y*8+x)*4;dataA[k]=float(e*100+x);dataA[k+1]=float(y);dataA[k+2]=.5f;dataA[k+3]=1;}
    g.Write(uploadA,dataA.data());g.Begin();g.Upload(a,uploadA,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);g.Finish();
    for(bool direct:{true,false}){g.Begin();std::vector<std::unique_ptr<vulkan::Recorded>> jobs;for(unsigned e=0;e<2;++e){jobs.push_back(ops.Resample(g.command,inputs0[e],direct?Rect{0,0,8,8}:Rect{2,1,4,4},outputs[e],true,direct?Point{1,1}:Point{-2,3}));need(jobs.back()->DirectCopy()==direct,"Incorrect direct copy selection");}readOutput();retire(jobs);const auto pixels=g.Read(read);
        for(unsigned e=0;e<2;++e)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){const auto k=(e*64+y*8+x)*4;const auto expectedX=direct?float(e*100+x):-2.f*float(e*100+2+x/2);const auto expectedY=direct?float(y):3.f*float(1+y/2);need(pixels[k]==expectedX&&pixels[k+1]==expectedY&&pixels[k+2]==.5f&&pixels[k+3]==1,"Crop/component conversion mismatch");}std::cout<<"PASS Vulkan crop direct="<<direct<<" both layers\n";}
    for(auto format:{VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R32G32_SFLOAT,VK_FORMAT_R32_SFLOAT}){
        const auto converted=g.MakeTexture(format,targetUsage);g.Begin();std::vector<std::unique_ptr<vulkan::Recorded>> jobs;
        for(unsigned e=0;e<2;++e){auto target=eyeView(converted,e,targetUsage,VK_IMAGE_LAYOUT_UNDEFINED);jobs.push_back(ops.Resample(g.command,inputs0[e],{0,0,8,8},target,true));need(!jobs.back()->DirectCopy(),"Format conversion incorrectly used copy");}
        g.Barrier(converted,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);g.Download(converted,read);retire(jobs);const auto values=g.Read(read);
        const unsigned channels=format==VK_FORMAT_R32_SFLOAT?1:format==VK_FORMAT_R32G32_SFLOAT?2:4;
        std::array<uint16_t,512> half{};if(format==VK_FORMAT_R16G16B16A16_SFLOAT)std::memcpy(half.data(),values.data(),sizeof(half));
        for(unsigned n=0;n<128;++n)for(unsigned c=0;c<channels;++c){float actual=values[n*channels+c];if(format==VK_FORMAT_R16G16B16A16_SFLOAT){const auto h=half[n*4+c];const int exp=(h>>10)&31,mantissa=h&1023;actual=(h&0x8000?-1.f:1.f)*(exp?std::ldexp(1.f+float(mantissa)/1024,exp-15):std::ldexp(float(mantissa),-24));}need(actual==dataA[n*4+c],"HDR/depth/motion typed conversion mismatch");}
        std::cout<<"PASS Vulkan typed output format="<<unsigned(format)<<" both layers\n";
    }
    // Verify post-processing controls on real pixels, including repeated reuse
    // of retired descriptor/constants slots and separate eye array layers.
    for(unsigned e=0;e<2;++e)for(unsigned n=0;n<64;++n){const auto k=(e*64+n)*4;for(unsigned c=0;c<3;++c){dataA[k+c]=(e?.5f:.4f)+float(n%3)*.01f;dataB[k+c]=e?.25f:.2f;}dataA[k+3]=dataB[k+3]=1;}
    g.Write(uploadA,dataA.data());g.Write(uploadB,dataB.data());g.Begin();g.Upload(a,uploadA,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);g.Upload(b,uploadB,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);g.Finish();
    Config marker;marker.enabled=true;std::array<ViewGeometry,2> markerViews{};
    for(unsigned e=0;e<2;++e){auto& v=markerViews[e];v.viewId=e+1;v.input=v.output={8,8};v.binocular={{.125,0,0,0,.125,0,0,0,1},1,1};}
    const auto markerPlan=Plan(marker,markerViews,FixedPlacement(marker),1,1,1,{1,64});
    for(unsigned iteration=0;iteration<24;++iteration){const unsigned kind=iteration%4;g.Begin();std::vector<std::unique_ptr<vulkan::Recorded>> jobs;
        for(unsigned e=0;e<2;++e){
            if(kind<2)jobs.push_back(ops.NeuralResolve(g.command,inputs0[e],inputs1[e],inputs0[e],outputs[e],float(kind),1,0));
            else if(kind==2)jobs.push_back(ops.Sharpen(g.command,inputs0[e],outputs[e],.7f));
            else jobs.push_back(ops.Indicator(g.command,inputs0[e],markerPlan.views[e].central,outputs[e],true));
        }
        readOutput();retire(jobs);const auto pixels=g.Read(read);unsigned changed{};
        for(unsigned n=0;n<128;++n)for(unsigned c=0;c<4;++c){const unsigned k=n*4+c;need(std::isfinite(pixels[k]),"Non-finite postprocess output");if(kind<2){const auto expected=c==3||kind==0?dataA[k]:2*dataA[k]-dataB[k];need(std::abs(pixels[k]-expected)<.00002f,"Residual identity/edit transfer differs from expected pixels");}else changed+=std::abs(pixels[k]-dataA[k])>.0001f;}
        if(kind>=2)need(changed>0,"Sharpen/region control produced unchanged pixels");
    }
    std::cout<<"PASS Neural residual identity/edit, sharpen and indicator pixels, 24 retired slot reuse cycles\n";
    g.Begin();bool rejected=false;try{auto invalid=inputs0[0];invalid.range.layerCount=2;(void)ops.Resample(g.command,invalid,{0,0,8,8},outputs[0]);}catch(const std::invalid_argument&){rejected=true;}need(rejected,"Multi-eye view accepted");g.Finish();
    for(auto view:views)g.vkDestroyImageView(g.device,view,nullptr);
    std::cout<<"PASS Vulkan fixed-foveation primitives. No game, temporal reconstruction, Neural or FPS claim.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
