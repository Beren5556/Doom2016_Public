#pragma once
#include "../src/dlss/VulkanDlssRequirements.h"
#include "../src/sfs/DoomTemporalParameters.h"
#include "DoomEngineJitterFixture.h"
struct DoomRuntimeDlssFixture {
    inline static std::filesystem::path runtime,cache;
    inline static kharvox::dlss::ExtensionSelection extensions;
    static constexpr auto project="953a53d7-aeb4-49bb-957d-4a3e5c31c6f6";
    static std::vector<const char*> query(VkInstance instance,VkPhysicalDevice physical){
        using namespace kharvox::dlss;Options o;o.instance=instance;o.physical=physical;o.runtimeDirectory=runtime;o.cacheDirectory=cache;o.projectId=project;
        const auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(GetModuleHandleW(L"vulkan-1.dll"),"vkGetInstanceProcAddr"));
        const auto required=instance?DeviceRequirements(o):InstanceRequirements(o);
        const auto available=instance?DeviceExtensions(physical,reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(gipa(instance,"vkEnumerateDeviceExtensionProperties"))):InstanceExtensions(reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(gipa(nullptr,"vkEnumerateInstanceExtensionProperties")));
        extensions=SelectExtensions(0,nullptr,required,available);if(!extensions.Ready())throw std::runtime_error("NGX test extensions unavailable");return extensions.Pointers();
    }
    kharvox::doom_temporal::ViewSnapshot view;
    kharvox::sfs::DoomTemporalUniformSnapshot uniforms;
    struct Sample {kharvox::doom_temporal::ViewSnapshot view;kharvox::sfs::DoomTemporalUniformSnapshot uniforms;};
    std::vector<Sample> samples;
    explicit DoomRuntimeDlssFixture(const char* path){std::ifstream in(path,std::ios::binary);
        auto read=[&](auto& value){if(!in.read(reinterpret_cast<char*>(&value),sizeof(value)))throw std::runtime_error("Missing temporal replay");};
        uint32_t magic{},count{},phase{};read(magic);read(count);if(magic!=0x44545031||!count)throw std::runtime_error("Invalid temporal replay");
        for(uint32_t n=0;n<count;++n){Sample sample;
            read(sample.view.source);read(sample.view.present);read(phase);sample.view.phase=uint8_t(phase);read(sample.view.rectangle);read(sample.view.jittered);read(sample.view.unjittered);read(sample.view.view);read(sample.uniforms.high);read(sample.uniforms.low);
            std::array<float,96> capturedEyes;read(capturedEyes);samples.push_back(sample);}
        view=samples.front().view;uniforms=samples.front().uniforms;
    }
    void select(size_t index){const auto present=view.present;const auto& sample=samples.at(index%samples.size());view=sample.view;view.present=present;uniforms=sample.uniforms;}
    void observe(){auto snapshots=kharvox::doom_temporal::current();if(!snapshots)throw std::runtime_error("Runtime view observer missing");
        auto reader=[&](uintptr_t address,void* destination,size_t size){const void* source=nullptr;size_t bytes{};
            if(address==view.source+0x1130){source=view.jittered.data();bytes=64;}else if(address==view.source+0x1170){source=view.unjittered.data();bytes=64;}
            else if(address==view.source+0x11f0){source=view.view.data();bytes=64;}else if(address==view.source+0x1111){source=&view.mode;bytes=1;}
            else if(address==view.source+0x1112){source=&view.phase;bytes=1;}else if(address==1){source=view.rectangle.data();bytes=16;}
            if(!source||size!=bytes)return false;std::memcpy(destination,source,size);return true;};
        snapshots->observe(reader,view.source,1,++view.present);
    }
};
