#include <dlss_foveado/Reconstruction.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace dlss_foveado;
using namespace dlss_foveado::d3d11;
template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int wmain(int argc,wchar_t** argv) try {
    check(argc==3,"usage: NgxHardware <runtime-directory> <private-cache-directory>");
    std::filesystem::create_directories(argv[2]);
    Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)),"hardware D3D11 device");
    Operations operations(device.Get());
    auto backend=CreateNgxBackend(device.Get(),{std::filesystem::absolute(argv[1]),std::filesystem::absolute(argv[2]),"21fdbd19-75b2-4993-a664-a20151745504"});
    Image color,depth,motion,out;
    operations.Ensure(color,{256,256});operations.Ensure(depth,{256,256},DXGI_FORMAT_R32_FLOAT);
    operations.Ensure(motion,{256,256},DXGI_FORMAT_R32G32_FLOAT);operations.Ensure(out,{512,512});
    const float gray[]{0.3f,0.4f,0.5f,1}, z[]{0.5f,0,0,0}, zero[]{0,0,0,0};
    context->ClearRenderTargetView(color.target.Get(),gray);context->ClearRenderTargetView(depth.target.Get(),z);context->ClearRenderTargetView(motion.target.Get(),zero);
    for(uint64_t frame=1;frame<=3;++frame)for(uint64_t eye=0;eye<2;++eye){
        ReconstructionInput input;input.feature={eye,Branch::Central,{256,256},{512,512},11,Quality::Performance,true,true,false};
        input.epoch=1;input.frame=frame;input.frameTimeMs=11.1f;input.color=color.texture.Get();input.depth=depth.texture.Get();
        input.motion=motion.texture.Get();input.destination=out.texture.Get();input.reset=frame==1;
        const auto receipt=backend->Evaluate(context.Get(),input);
        check(receipt.ticket&&receipt.frame==frame&&receipt.viewId==eye&&receipt.output==Extent{512,512},"SR receipt mismatch");
    }
    operations.Ensure(out,{256,256});
    for(uint64_t eye=0;eye<2;++eye){
        ReconstructionInput input;input.feature={eye,Branch::Peripheral,{256,256},{256,256},11,Quality::Native,true,true,false};
        input.epoch=1;input.frame=3;input.frameTimeMs=11.1f;input.color=color.texture.Get();input.depth=depth.texture.Get();
        input.motion=motion.texture.Get();input.destination=out.texture.Get();input.reset=true;
        const auto receipt=backend->Evaluate(context.Get(),input);
        check(receipt.branch==Branch::Peripheral&&receipt.input==receipt.output,"LDR DLAA receipt mismatch");
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!backend->Stop(context.Get())){
        check(std::chrono::steady_clock::now()<deadline,"NGX retirement timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    backend.reset();
    std::cout<<"PASS: real NVIDIA SR and LDR DLAA, four eye/branch contexts, receipts and GPU retirement\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
