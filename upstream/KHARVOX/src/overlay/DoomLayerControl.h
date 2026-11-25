#pragma once
#include "../common/RuntimePaths.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <optional>
#include <cmath>
#include <mutex>
#include <iomanip>
#include <array>
#include <memory>
#include <functional>
#include <tuple>
#include "DoomOutputResolution.h"

namespace kharvox::overlay {
// Own startup selection. This file belongs to DOOM Layer, never to the launcher.
struct Selection {
    unsigned version{1},mode{2},preset{11};
    float outputScale{1.3f};
    unsigned quality{},scaleBps{6667},neuralModel{},neuralPercent{85},neuralBefore{};
    unsigned style{2},intensity{120},structure{127},tone{134},skinAuto{1},skin{100},autoMask{},detail{100},color{100},sharpness{},sharpen{};
    unsigned foveationMode{},targetMicros{11100},overloadMs{1000},recoveryMs{1000};
    std::array<unsigned,4> inner{3000,3000,3000,3000},outer{1000,1000,1000,1000};
    uint64_t revision{1};
    std::string foveation{"3;0;1;2000;2000;2000;2000;1000;0;7500;11;11;10000;0;1;1;2000;2000;2000;2000;1000;0"};
};
inline auto rendererIdentity(const Selection& v){return std::tie(v.mode,v.preset,v.quality,v.scaleBps,v.neuralModel,v.neuralPercent,v.neuralBefore,v.style,v.intensity,v.structure,v.tone,v.skinAuto,v.skin,v.autoMask,v.detail,v.color,v.sharpness,v.sharpen,v.foveationMode,v.targetMicros,v.overloadMs,v.recoveryMs,v.inner,v.outer,v.foveation);}
inline std::optional<Selection> parseSelection(const std::string& text){
    if(text.size()>2048)return {};
    Selection v;std::istringstream input(text);std::string extra;
    if(!(input>>v.version>>v.mode>>v.preset>>v.outputScale))return {};
    if(v.version>=2){if(!(input>>v.quality>>v.scaleBps>>v.neuralModel>>v.neuralPercent>>v.neuralBefore>>v.foveation))return {};}
    if(v.version==3){
        if(!(input>>v.style>>v.intensity>>v.structure>>v.tone>>v.skinAuto>>v.skin>>v.autoMask>>v.detail>>v.color>>v.sharpness>>v.sharpen>>v.foveationMode>>v.targetMicros>>v.overloadMs>>v.recoveryMs))return {};
        for(auto& n:v.inner)if(!(input>>n))return {};for(auto& n:v.outer)if(!(input>>n))return {};if(!(input>>v.revision))return {};
    }
    if(input>>extra)return {};
    if((v.version<1||v.version>3)||(v.version<3&&v.mode<1)||v.mode>(v.version==1?2u:4u)||(v.preset!=0&&(v.preset<10||v.preset>13))||!std::isfinite(v.outputScale)
        ||v.outputScale<.5f||v.outputScale>3.f)return {};
    if(v.quality>2||v.scaleBps<3333||v.scaleBps>9900||v.neuralModel>2||v.neuralPercent<50||v.neuralPercent>100||v.neuralBefore>1||v.foveation.size()>1024)return {};
    if(v.style>2||v.intensity>200||v.structure>200||v.tone>200||v.skinAuto>1||v.skin>200||v.autoMask>1||v.detail>200||v.color>100||v.sharpness>100||v.sharpen>100||v.foveationMode>2||v.targetMicros<5000||v.targetMicros>50000||v.overloadMs<100||v.overloadMs>10000||v.recoveryMs<100||v.recoveryMs>10000||!v.revision)return {};
    // Validate the persisted common contract even when the selected mode does
    // not instantiate its GPU provider. UI and native startup must agree.
    std::array<unsigned,22> fields{};std::istringstream config(v.foveation);
    constexpr std::array<unsigned,22> limits{3,1,1,4500,4500,4500,4500,5000,1,10000,13,13,20000,1,1,1,4500,4500,4500,4500,5000,2};
    for(unsigned i=0;i<fields.size();++i){
        std::string word;if(!std::getline(config,word,';')||word.empty()||word.find_first_not_of("0123456789")!=std::string::npos)return {};
        std::istringstream number(word);if(!(number>>fields[i])||fields[i]>limits[i])return {};
    }
    if(std::getline(config,extra,';')||v.foveation.back()==';'||fields[0]!=3||fields[9]<2500||fields[12]<10000)return {};
    for(auto i:{10u,11u})if(fields[i]!=0&&(fields[i]<10||fields[i]>13))return {};
    if(v.version==2)v.foveationMode=fields[1];
    if(fields[1]!=unsigned(v.foveationMode!=0)||(v.foveationMode&&v.mode<3))return {};
    for(unsigned i=0;i<4;++i)if(v.inner[i]>4500||v.outer[i]>v.inner[i]||v.inner[i]%100||v.outer[i]%100)return {};
    if(v.foveationMode==2&&v.mode==4)return {};
    return v;
}
inline std::filesystem::path root(){return runtimePath(L"DoomLayer");}
inline bool enabled(){static const bool on=std::filesystem::is_regular_file(root()/L"enabled");return on;}
inline const Selection& startup(){
    static const Selection value=[](){
        if(!enabled())return Selection{};
        std::ifstream file(root()/L"startup.cfg",std::ios::binary);
        if(!file)throw std::runtime_error("DOOM Layer startup selection missing");
        char text[2049]{};file.read(text,sizeof(text));
        const auto parsed=parseSelection(std::string(text,static_cast<size_t>(file.gcount())));
        if(!parsed)throw std::runtime_error("DOOM Layer startup selection invalid");
        return *parsed;
    }();return value;
}
// Immutable snapshots prevent UI writes racing Vulkan recorders. Only the
// retired frame boundary may commit a new selection.
inline std::shared_ptr<const Selection> liveSelection;
inline Selection current(){auto value=std::atomic_load(&liveSelection);return value?*value:startup();}
inline void commit(const Selection& value){std::atomic_store(&liveSelection,std::make_shared<const Selection>(value));}
inline std::function<bool(const Selection&)> applyEngineSelection;
inline unsigned mode(){auto value=std::atomic_load(&liveSelection);return value?value->mode:startup().mode;}
inline bool dlaa(){return enabled()&&mode()==2;}
inline bool reconstruction(){return enabled()&&mode()>=3;}
inline const char* renderFraction(){thread_local std::string value;const auto selected=current();std::ostringstream out;out<<(selected.mode>=3?selected.scaleBps/10000.:1.);value=out.str();return value.c_str();}
inline std::optional<Selection> requestedSelection(){
    if(!enabled())return {};
    std::ifstream file(root()/(L"request-"+std::to_wstring(GetCurrentProcessId())+L".cfg"),std::ios::binary);
    if(!file)return {};char text[2049]{};file.read(text,sizeof(text));
    return parseSelection(std::string(text,static_cast<size_t>(file.gcount())));
}
inline void prepareEnvironment(){
    if(!enabled())return;
    (void)startup();
    // PID reuse must not replay a previous process's control transaction.
    for(const auto* prefix:{L"request-",L"ack-"}){
        const auto path=root()/(std::wstring(prefix)+std::to_wstring(GetCurrentProcessId())+(prefix[0]==L'r'?L".cfg":L".json"));
        std::error_code ignored;std::filesystem::remove(path,ignored);
    }
    SetEnvironmentVariableA("KHARVOX_DISABLE_AA","0");
    SetEnvironmentVariableA("KHARVOX_TEMPORAL_BRIDGE","1");
    SetEnvironmentVariableA("KHARVOX_TEMPORAL_PARAMETERS","1");
    SetEnvironmentVariableA("KHARVOX_DLSS_DLAA",dlaa()?"1":"0");
    SetEnvironmentVariableA("KHARVOX_DLSS_RAW_HDR",dlaa()?"1":"0");
    SetEnvironmentVariableA("KHARVOX_DLSS_PREPARE","1");
    SetEnvironmentVariableW(L"KHARVOX_DLSS_RUNTIME",(root()/L"ngx").c_str());
    const auto cache=root()/L"cache";std::filesystem::create_directories(cache);
    SetEnvironmentVariableW(L"KHARVOX_DLSS_CACHE",cache.c_str());
}
inline std::string jsonText(const std::string& text){
    std::ostringstream out;out<<'"';
    for(unsigned char c:text){if(c=='"'||c=='\\')out<<'\\'<<char(c);else if(c<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;else out<<char(c);}out<<'"';return out.str();
}
inline std::mutex acknowledgementMutex;
inline std::string pendingAcknowledgement;
inline void flushAcknowledgement(){
    std::lock_guard<std::mutex> guard(acknowledgementMutex);
    if(pendingAcknowledgement.empty())return;
    const auto path=root()/(L"ack-"+std::to_wstring(GetCurrentProcessId())+L".json");
    const auto temporary=path.wstring()+L".tmp";
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
    out<<pendingAcknowledgement;
    out.close();
    if(out&&MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING))pendingAcknowledgement.clear();
}
inline void acknowledge(uint64_t revision,bool accepted,const std::string& error={}){
    std::ostringstream out;
    out<<"{\"revision\":"<<revision<<",\"accepted\":"<<(accepted?"true":"false")<<",\"error\":"<<jsonText(error)<<"}";
    {std::lock_guard<std::mutex> guard(acknowledgementMutex);pendingAcknowledgement=out.str();}
    flushAcknowledgement();
}
struct Metrics {uint64_t neuralEvaluations{};unsigned inputWidth{},inputHeight{};bool foveated{};double fps{},gpuMs{},processingMs{},framePeriodMs{},changeCpuMs{},targetMs{};std::string error,dynamicStatus;std::array<unsigned,4> cuts{};std::array<std::array<double,9>,2> maps{};};
inline void publish(unsigned mode,uint64_t evaluations,uint64_t resets,unsigned width,unsigned height,bool completed,const Metrics& metrics={}){
    if(!enabled())return;
    // Caller serializes this at the existing frame/retirement boundary.
    static std::mutex mutex;std::lock_guard<std::mutex> guard(mutex);
    static ULONGLONG previous{};const auto now=GetTickCount64();
    if(now-previous<250)return;previous=now;
    // A host read may briefly deny Windows replacement. Republish the receipt,
    // never repeat its engine mutation or stall rendering waiting for the reader.
    flushAcknowledgement();
    const auto path=root()/(L"status-"+std::to_wstring(GetCurrentProcessId())+L".json");
    const auto temporary=path.wstring()+L".tmp";
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
    out<<"{\"schema\":2,\"pid\":"<<GetCurrentProcessId()<<",\"tick\":"<<now<<",\"requestedMode\":"<<current().mode<<",\"mode\":"<<mode
       <<",\"preset\":"<<current().preset<<",\"startupRevision\":"<<current().revision<<",\"evaluations\":"<<evaluations<<",\"resets\":"<<resets
       <<",\"width\":"<<width<<",\"height\":"<<height<<",\"completed\":"<<(completed?"true":"false")
       <<",\"neuralEvaluations\":"<<metrics.neuralEvaluations<<",\"inputWidth\":"<<metrics.inputWidth<<",\"inputHeight\":"<<metrics.inputHeight
       <<",\"foveated\":"<<(metrics.foveated?"true":"false")<<",\"fps\":"<<(std::isfinite(metrics.fps)?metrics.fps:0)
       <<",\"gpuMs\":"<<(std::isfinite(metrics.gpuMs)?metrics.gpuMs:0)<<",\"processingMs\":"<<metrics.processingMs<<",\"framePeriodMs\":"<<metrics.framePeriodMs<<",\"changeCpuMs\":"<<metrics.changeCpuMs<<",\"targetMs\":"<<metrics.targetMs<<",\"dynamicStatus\":"<<jsonText(metrics.dynamicStatus)<<",\"error\":"<<jsonText(metrics.error)<<",\"cuts\":[";
    for(unsigned i=0;i<4;++i)out<<(i?",":"")<<metrics.cuts[i];out<<"],\"maps\":[";
    for(unsigned e=0;e<2;++e){out<<(e?",[":"[");for(unsigned i=0;i<9;++i)out<<(i?",":"")<<metrics.maps[e][i];out<<']';}
    const auto output=outputResize.describe();
    out<<"],\"output\":{\"writable\":"<<(output.writable?"true":"false")
       <<",\"width\":"<<output.selected[0].width<<",\"height\":"<<output.selected[0].height
       <<",\"recommendedWidth\":"<<output.recommended[0].width<<",\"recommendedHeight\":"<<output.recommended[0].height
       <<",\"minWidth\":"<<uint32_t(std::ceil(output.recommended[0].width*.5))<<",\"maxWidth\":"<<output.maximumWidth()<<"}}";
    out.close();if(out)MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);
}
inline bool prepareXr(){
    if(!enabled())return false;
    (void)startup();
    const auto native=root()/L"native";
    if(!std::filesystem::is_regular_file(native/L"ExternalOverlayXR.json")
        ||!std::filesystem::is_regular_file(native/L"ExternalOverlayXR.dll"))
        throw std::runtime_error("DOOM Layer OpenXR files missing");
    // Process-local explicit layer discovery; do not register a global layer.
    wchar_t old[32768]{};GetEnvironmentVariableW(L"XR_API_LAYER_PATH",old,32768);
    auto path=native.wstring();if(*old)path+=L";"+std::wstring(old);
    SetEnvironmentVariableW(L"XR_API_LAYER_PATH",path.c_str());
    std::wifstream config(root()/L"python.cfg");std::wstring python;std::getline(config,python);
    if(python.empty()||python.find(L'"')!=std::wstring::npos||!std::filesystem::is_regular_file(python))
        throw std::runtime_error("DOOM Layer Python runtime missing");
    auto command=L"\""+python+L"\" -B \""+(root()/L"host/run.py").wstring()+L"\" --pid "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION child{};
    if(!CreateProcessW(python.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root().c_str(),&si,&child))
        throw std::runtime_error("DOOM Layer host could not start");
    CloseHandle(child.hThread);CloseHandle(child.hProcess);return true;
}
}
