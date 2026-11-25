#include <filesystem>
#define main resolveFixtureMain
#include "doom_temporal_resolve_gpu_tests.cpp"
#undef main
#include <renderdoc_app.h>
#include "../src/debug/RenderDocFrame.h"
int main(int argc,char** argv)try{
    need(argc==6,"Expected vertex/profile/readback/RenderDoc-directory/capture-template");
    const auto directory=std::filesystem::absolute(argv[4]);
    wchar_t extra[32768]{};const auto extraLength=GetEnvironmentVariableW(L"VK_ADD_IMPLICIT_LAYER_PATH",extra,32768);
    const auto paths=(extraLength&&extraLength<32768?std::wstring(extra)+L";":L"")+directory.wstring();
    SetEnvironmentVariableW(L"VK_ADD_IMPLICIT_LAYER_PATH",paths.c_str());
    SetEnvironmentVariableA("ENABLE_VULKAN_RENDERDOC_CAPTURE","1");
    const auto library=LoadLibraryExW((directory/L"renderdoc.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    need(library!=nullptr,"RenderDoc DLL unavailable");
    const auto get=reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(library,"RENDERDOC_GetAPI"));
    RENDERDOC_API_1_6_0* api{};need(get&&get(eRENDERDOC_API_Version_1_6_0,reinterpret_cast<void**>(&api)),"RenderDoc API unavailable");
    api->SetCaptureFilePathTemplate(argv[5]);api->MaskOverlayBits(0,0);
    {
        ResolveTest test(argv[1],argv[2],argv[3]);
        const auto device=RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(test.g.instance);
        const auto prefix=std::filesystem::absolute(argv[5]);
        // CTest may repeat the fixture; only its fixed marker files are reset.
        for(const wchar_t* suffix:{L".arm",L".done",L".failed"}){auto marker=prefix;marker+=suffix;std::filesystem::remove(marker);}
        kharvox::RenderDocFrame capture(prefix);
        capture.Boundary(device);need(!api->IsFrameCapturing(),"Capture started before arming");
        auto arm=prefix;arm+=L".arm";std::ofstream(arm)<<"ready\n";
        capture.Boundary(device);need(api->IsFrameCapturing()!=0,"Vulkan capture layer not active");
        test.Run(3);capture.Boundary(device);need(!api->IsFrameCapturing(),"RenderDoc capture did not end");
        capture.Boundary(device);need(!api->IsFrameCapturing(),"Completed capture rearmed");
        auto done=prefix;done+=L".done";need(std::filesystem::is_regular_file(done),"Capture completion marker missing");
        need(api->GetNumCaptures()==1,"Expected one bounded capture");
        uint32_t length{};uint64_t timestamp{};need(api->GetCapture(0,nullptr,&length,&timestamp)!=0,"Capture metadata missing");
        std::vector<char> path(length+1);need(api->GetCapture(0,path.data(),&length,&timestamp)!=0,"Capture filename missing");
        need(std::filesystem::is_regular_file(path.data()),"RDC file missing");std::cout<<"PASS headless multiview RenderDoc capture="<<path.data()<<'\n';
    }
    FreeLibrary(library);return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
