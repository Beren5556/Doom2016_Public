#pragma once
#include <dlss_foveado/Foveation.h>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <windows.h>

namespace b3::control {
class FoveationController {
    mutable std::mutex mutex;
    std::filesystem::path file;
    dlss_foveado::Config requested;
    uint64_t revision{1},generalRevision{},applied{};
    bool active{},initialized{};
    std::string runtime{"Esperando una pareja completa de ojos"},saved,error,details;
    struct Command {dlss_foveado::Config config;uint64_t base{},general{};bool save{};};
    std::optional<Command> queued;
    static bool Number(std::string_view s,uint64_t& value){
        if(s.empty())return false;const auto r=std::from_chars(s.data(),s.data()+s.size(),value);
        return r.ec==std::errc{}&&r.ptr==s.data()+s.size()&&value!=0;
    }
public:
    struct Snapshot {dlss_foveado::Config config;uint64_t revision{};};
    void Initialize(const std::filesystem::path& directory){
        std::scoped_lock lock(mutex);file=directory/L"foveation-v1.ini";
        if(std::filesystem::exists(file)){
            if(!std::filesystem::is_regular_file(file)||std::filesystem::file_size(file)>1026)
                error="Configuracion foveada invalida; modulo desactivado";
            else {std::ifstream in(file);std::string text;std::getline(in,text);const auto parsed=dlss_foveado::Config::Parse(text);
                if(parsed)requested=*parsed;else error="Esquema foveado no reconocido; modulo desactivado";}
        }
        initialized=true;
    }
    bool Submit(std::string_view event,std::string_view packet){
        if(event!="DLSS_FOVEADO.Apply"&&event!="DLSS_FOVEADO.ApplySave")return false;
        if(packet.size()>1100)return false;
        const auto first=packet.find('|'),last=packet.find('|',first==packet.npos?0:first+1);
        if(first==packet.npos||last==packet.npos||packet.find('|',last+1)!=packet.npos)return false;
        Command command;const auto config=dlss_foveado::Config::Parse(packet.substr(last+1));
        if(!config||!Number(packet.substr(0,first),command.base)||!Number(packet.substr(first+1,last-first-1),command.general))return false;
        command.config=*config;command.save=event=="DLSS_FOVEADO.ApplySave";
        std::scoped_lock lock(mutex);
        if(!initialized||queued||command.base!=revision||command.general!=generalRevision){error="Ajustes cambiados; descarta o revisa el borrador";return false;}
        queued=command;return true;
    }
    void Tick(uint64_t general){
        std::scoped_lock lock(mutex);generalRevision=general;
        if(!queued)return;const auto command=*queued;queued.reset();
        if(command.base!=revision||command.general!=general){error="Ajustes DLSS cambiados; vuelve a revisar y aplicar";return;}
        requested=command.config;++revision;applied=0;active=false;error.clear();
        if(command.save){
            try{
                const auto temporary=std::filesystem::path(file.wstring()+L".tmp");
                {std::ofstream output(temporary,std::ios::binary|std::ios::trunc);output<<requested.Encode()<<'\n';output.flush();if(!output)throw std::runtime_error("escritura fallida");}
                if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("sustitucion fallida");
                saved="Configuracion de inicio guardada";
            }catch(const std::exception& e){saved=std::string("No se pudo guardar: ")+e.what();}
        }
    }
    Snapshot Requested()const{std::scoped_lock lock(mutex);return {requested,revision};}
    void Runtime(uint64_t confirmed,bool processing,std::string message,std::string information={}){
        std::scoped_lock lock(mutex);applied=confirmed;active=processing;runtime=std::move(message);
        details=std::move(information);
    }
    void Publish(const std::function<void(const char*,const char*)>& emit)const{
        std::string controls,status,persistence,information;
        {std::scoped_lock lock(mutex);controls=std::to_string(revision)+"|"+std::to_string(generalRevision)+"|"+requested.Encode();
            status=active&&applied==revision?"Foveacion fija activa en ambos ojos":requested.enabled?
                "Foveacion solicitada; requiere DLSS o DLSS5 y una pareja compatible":"Foveacion desactivada";
            status+=" | "+runtime;if(!error.empty())status+=" | "+error;persistence=saved;information=details;}
        emit("DLSS_FOVEADO.Controls",controls.c_str());emit("DLSS_FOVEADO.Status",status.c_str());emit("DLSS_FOVEADO.Saved",persistence.c_str());
        emit("DLSS_FOVEADO.Details",information.c_str());
    }
};
} // namespace b3::control
