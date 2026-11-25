#pragma once
#include <windows.h>
#include <renderdoc_app.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace kharvox {
// SourceRing has no downstream vkQueuePresentKHR. Delimit one RenderDoc
// capture using the existing, completed engine frame boundary instead.
// No DLL injection, image readback, shader changes or automatic arming here.
class RenderDocFrame {
    std::filesystem::path prefix_;
    RENDERDOC_API_1_6_0* api_{};
    void* instance_{};
    uint32_t previous_{};
    bool active_{}, finished_{};
    std::mutex mutex_;
    std::filesystem::path Marker(const wchar_t* suffix) const {auto p=prefix_;p+=suffix;return p;}
    void Write(const wchar_t* suffix,const std::string& text){
        std::ofstream file(Marker(suffix),std::ios::binary);file<<text<<'\n';
        if(!file)throw std::runtime_error("Cannot save RenderDoc status");
    }
public:
    explicit RenderDocFrame(std::filesystem::path prefix):prefix_(std::move(prefix)){}
    bool Enabled() const {return !prefix_.empty();}
    static std::filesystem::path Environment(){
        wchar_t value[32768]{};auto n=GetEnvironmentVariableW(L"KHARVOX_RENDERDOC_CAPTURE",value,32768);
        return n&&n<32768?std::filesystem::path(value):std::filesystem::path{};
    }
    // Argument is RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE, not VkInstance.
    void Boundary(void* instance) noexcept {
        if(prefix_.empty())return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(finished_)return;
        try{
            if(active_){
                if(instance!=instance_)return;
                active_=false;finished_=true;
                if(!api_->EndFrameCapture(instance_,nullptr)||api_->GetNumCaptures()!=previous_+1)
                    throw std::runtime_error("RenderDoc did not save exactly one capture");
                uint32_t length{};uint64_t timestamp{};
                if(!api_->GetCapture(previous_,nullptr,&length,&timestamp)||length>32768)
                    throw std::runtime_error("Missing RenderDoc capture metadata");
                std::vector<char> name(length+1);
                if(!api_->GetCapture(previous_,name.data(),&length,&timestamp)||!std::filesystem::is_regular_file(std::filesystem::u8path(name.data())))
                    throw std::runtime_error("RenderDoc capture file missing");
                Write(L".done",name.data());return;
            }
            if(!prefix_.is_absolute())throw std::runtime_error("RenderDoc prefix must be absolute");
            if(!std::filesystem::is_regular_file(Marker(L".arm")))return;
            if(!instance)throw std::runtime_error("Game Vulkan instance unavailable");
            if(std::filesystem::exists(Marker(L".done"))||std::filesystem::exists(Marker(L".failed")))
                throw std::runtime_error("RenderDoc session path was already used");
            auto library=GetModuleHandleW(L"renderdoc.dll");
            auto get=library?reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(library,"RENDERDOC_GetAPI")):nullptr;
            if(!get||!get(eRENDERDOC_API_Version_1_6_0,reinterpret_cast<void**>(&api_)))
                throw std::runtime_error("RenderDoc Vulkan layer is not loaded");
            if(api_->IsFrameCapturing())throw std::runtime_error("Another RenderDoc capture is active");
            api_->SetCaptureFilePathTemplate(prefix_.u8string().c_str());
            api_->MaskOverlayBits(0,0);previous_=api_->GetNumCaptures();instance_=instance;
            api_->StartFrameCapture(instance_,nullptr);
            if(!api_->IsFrameCapturing())throw std::runtime_error("RenderDoc cannot capture the game Vulkan instance");
            active_=true;
        }catch(const std::exception& e){
            finished_=true;
            try{Write(L".failed",e.what());}catch(...){}
        }
    }
};
}
