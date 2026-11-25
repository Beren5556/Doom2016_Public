// Image-quality diagnostic, independent of the game's history/transport tests.
// Raster displacement is known analytically: a positive displacement shifts
// geometry right/down, so the image samples F(pixel - displacement).
#define KHARVOX_DLSS_RECORD_TEST
#include "vulkan_dlss_tests.cpp"
#include <fstream>
#include <algorithm>

struct DetailLab : Lab {
    VkBuffer buffer{};VkDeviceMemory allocation{};
    void Allocate(size_t bytes){
        VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=bytes;
        b.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vkCheck(createBuffer(o.device,&b,nullptr,&buffer),"Detail staging");
        VkMemoryRequirements req{};bufferRequirements(o.device,buffer,&req);
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=req.size;
        a.memoryTypeIndex=MemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkCheck(f.vkAllocateMemory(o.device,&a,nullptr,&allocation),"Detail staging memory");
        vkCheck(bindBuffer(o.device,buffer,allocation,0),"Detail staging bind");
    }
    ~DetailLab(){if(buffer)destroyBuffer(o.device,buffer,nullptr);if(allocation)f.vkFreeMemory(o.device,allocation,nullptr);}
    static uint16_t ToHalf(float v){
        if(v<=0)return 0;
        int exp{};const double m=std::frexp(v,&exp);
        if(exp < -13)return uint16_t(std::round(std::ldexp(v,24)));
        return uint16_t(((exp+14)<<10)+int(std::round((m*2-1)*1024)));
    }
    static std::array<float,3> Pattern(double x,double y,unsigned eye){
        const double pi=3.141592653589793;
        return {float(.5+.3*std::sin(2*pi*(x*.21+eye*.23))),
                float(.5+.3*std::sin(2*pi*(y*.17+eye*.31))),
                float(.5+.25*std::sin(2*pi*(x*.13+y*.11+eye*.17)))};
    }
    void Upload(const PairInput& pair,double jx,double jy){
        const auto width=pair.eyes[0].color.extent.width,height=pair.eyes[0].color.extent.height;
        const auto bytes=size_t(width)*height*8;
        void* mapped{};vkCheck(map(o.device,allocation,0,bytes*2,0,&mapped),"Pattern map");
        auto* data=static_cast<uint16_t*>(mapped);
        for(unsigned eye=0;eye<2;++eye)for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
            const auto c=Pattern(x+.5-jx,y+.5-jy,eye);const auto n=((eye*height+y)*width+x)*4;
            for(unsigned k=0;k<3;++k)data[n+k]=ToHalf(c[k]);data[n+3]=ToHalf(1);
        }
        unmap(o.device,allocation);
        const auto copy=reinterpret_cast<PFN_vkCmdCopyBufferToImage>(o.gdpa(o.device,"vkCmdCopyBufferToImage"));
        Begin();
        for(unsigned eye=0;eye<2;++eye){const auto& image=pair.eyes[eye].color;
            Barrier(image,image.layout,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy region{};region.bufferOffset=eye*bytes;region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,image.range.baseArrayLayer,1};region.imageExtent={width,height,1};
            copy(command,buffer,image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
            Barrier(image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,image.layout);
        }Finish();
    }
    double Measure(const Output& output,const std::filesystem::path& path,double scale){
        double squared{};size_t samples{};
        for(unsigned eye=0;eye<2;++eye){const auto& image=output.eyes[eye];const auto width=image.extent.width,height=image.extent.height;
            Begin();Barrier(image,image.layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={width,height,1};
            readback(command,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&region);
            Barrier(image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,image.layout);Finish();
            void* mapped{};vkCheck(map(o.device,allocation,0,size_t(width)*height*8,0,&mapped),"Detail readback");
            const auto* data=static_cast<uint16_t*>(mapped);
            std::ofstream ppm(path/(std::to_string(eye)+".ppm"),std::ios::binary);ppm<<"P6\n"<<width<<' '<<height<<"\n255\n";
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
                const auto ref=Pattern((x+.5)*scale,(y+.5)*scale,eye);
                for(unsigned k=0;k<3;++k){const double value=Half(data[(y*width+x)*4+k]);need(std::isfinite(value),"Nonfinite detail output");
                    ppm.put(char(std::clamp(int(std::round(value*255)),0,255)));
                    if(x>16&&y>16&&x+16<width&&y+16<height){squared+=(value-ref[k])*(value-ref[k]);++samples;}
                }
            }unmap(o.device,allocation);
        }return squared/samples;
    }
};
static double Halton(unsigned n,unsigned base){double f=1.,r=0;while(n){f/=base;r+=f*(n%base);n/=base;}return r-.5;}
int wmain(int argc,wchar_t** argv)try{
    need(argc==4,"Expected runtime/cache/output directory");
    DetailLab lab;lab.Initialize(argv[1],argv[2]);lab.Allocate(512*512*8*2);
    const auto root=std::filesystem::path(argv[3]);std::filesystem::create_directories(root);
    double errors[2][2]{};
    for(const float sign:{1.f,-1.f})for(const unsigned phases:{8u,18u}){
        VulkanDlss provider(lab.o);auto pair=lab.Inputs(256,256,1,Mode::Quality,false);
        pair.hdr=false;for(auto& size:pair.output)size={384,384};
        Output output;
        for(unsigned frame=1;frame<=72;++frame){
            const unsigned phase=(frame-1)%phases+1;
            const float x=float(-Halton(phase,2)*1.2425),y=float(Halton(phase,3)*1.1932);
            pair.reset=frame==1;
            for(auto& eye:pair.eyes){eye.jitterX=sign*x;eye.jitterY=sign*y;
                for(auto* i:{&eye.color,&eye.depth,&eye.motion})i->identity.frame=frame;}
            lab.Upload(pair,x,y);lab.Begin();
            if(provider.Record(pair,lab.command,output)!=Status::Recorded)throw std::runtime_error(provider.Error());
            lab.Finish();need(provider.RetireRecordedAfterCompletion(output.ticket),"Retire detail");
        }
        const auto name=std::string(sign>0?"raster":"negated")+"-"+std::to_string(phases);
        std::filesystem::create_directories(root/name);
        const auto mse=lab.Measure(output,root/name,256./384);
        errors[sign>0?0:1][phases==8?0:1]=mse;
        std::cout<<name<<" mse="<<mse<<" resets="<<provider.ResetEvaluations()<<'\n';
    }
    for(unsigned p=0;p<2;++p){
        need(errors[0][p]<.002,"Correct raster jitter lost static detail");
        need(errors[0][p]*4<errors[1][p],"Image diagnostic did not distinguish wrong jitter sign");
    }
    std::cout<<"PASS both-eye static detail and opposing jitter control\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
