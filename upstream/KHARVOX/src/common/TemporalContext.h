#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace kharvox::temporal_context {
// CPU observations, never GPU consumption evidence. No Vulkan calls, engine
// writes or file writes from record paths. Arming and flushing use the existing
// SFS retired boundary. Any loss is explicit; diagnostics must not break render.
inline std::string quote(const std::string& value){
    std::string out="\"";for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){static const char h[]="0123456789abcdef";out+="\\u00";out+=h[c>>4];out+=h[c&15];}else out+=char(c);}return out+'"';
}
template<class T> inline uint64_t handle(T value){return reinterpret_cast<uintptr_t>(value);}
template<class T> inline std::string numbers(const T* values,size_t count){std::ostringstream out;out<<'[';for(size_t i=0;i<count;++i)out<<(i?",":"")<<+values[i];out<<']';return out.str();}
struct Limits {size_t metadataBytes=32*1024*1024,eventBytes=96*1024*1024,objects=100000,events=250000;unsigned intervals=12;};
class Capture {
    struct Object {uint64_t revision;std::string data;};
    std::mutex mutex;
    std::map<std::pair<std::string,uint64_t>,Object> objects;
    std::vector<std::string> events;
    std::map<std::string,uint64_t> sampled,skipped;
    struct LossCount {uint64_t beforeArm{},afterArm{};};
    std::map<std::string,LossCount> lossReasons;
    std::ofstream file;Limits limits;
    size_t metadataBytes{},eventBytes{},peakMetadataBytes{},peakObjects{};
    uint64_t sequence{},frame{},intervals{},losses{};
    std::atomic<uint64_t> failures{0};
    std::atomic<bool> running{false},done{false};
    bool started{},sealed{};std::string reason;
    static uint64_t time(){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    std::string row(const std::string& kind,const std::string& data){
        return "{\"kind\":"+quote(kind)+",\"sequence\":"+std::to_string(++sequence)+",\"cpuInterval\":"+std::to_string(frame)+",\"timeNs\":"+std::to_string(time())+",\"thread\":"+std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()))+",\"data\":"+data+"}";
    }
    // Called under mutex. Keep reasons even before the event journal is armed.
    // Bound both the number and length of keys; overflow remains counted.
    void loss(const std::string& what){
        ++losses;
        const auto found=lossReasons.find(what);
        const auto key=(what.size()>128||(found==lossReasons.end()&&lossReasons.size()>=63))?"other-reasons":what;
        auto& count=lossReasons[key];if(started)++count.afterArm;else ++count.beforeArm;
    }
    void append(std::string value){
        if(sealed)return;
        if(events.size()>=limits.events||value.size()+eventBytes>limits.eventBytes){loss(events.size()>=limits.events?"event-count-capacity":"event-byte-capacity");sealed=true;reason="event-capacity";running=false;return;}
        eventBytes+=value.size();events.push_back(std::move(value));
    }
    void flush(const char* why){
        if(done.exchange(true))return;running=false;
        if(reason.empty())reason=why;
        for(const auto& event:events)file<<event<<'\n';
        file<<"{\"kind\":\"end\",\"reason\":"<<quote(reason)<<",\"started\":"<<(started?"true":"false")<<",\"intervals\":"<<intervals<<",\"losses\":"<<losses<<",\"exceptions\":"<<failures.load()<<",\"events\":"<<events.size()<<",\"sampledOut\":{";
        bool comma=false;for(const auto& item:skipped){file<<(comma?",":"")<<quote(item.first)<<':'<<item.second;comma=true;}
        file<<"},\"lossReasons\":{";comma=false;
        for(const auto& item:lossReasons){file<<(comma?",":"")<<quote(item.first)<<":{\"beforeArm\":"<<item.second.beforeArm<<",\"afterArm\":"<<item.second.afterArm<<'}';comma=true;}
        file<<"},\"peakMetadataBytes\":"<<peakMetadataBytes<<",\"peakObjects\":"<<peakObjects<<",\"gpuExecutionQualified\":false,\"dlssReady\":false}\n";file.flush();file.close();events.clear();objects.clear();
    }
public:
    const uint64_t device;
    Capture(uint64_t d,const std::filesystem::path& path,Limits bound={}):limits(bound),device(d){
        if(!path.is_absolute()||std::filesystem::exists(path))throw std::runtime_error("Context path must be absolute and new");
        if(!limits.intervals||!limits.events||!limits.objects)throw std::runtime_error("Invalid context bounds");
        file.open(path);if(!file)throw std::runtime_error("Cannot create context evidence");
        file<<"{\"kind\":\"header\",\"schema\":1,\"lossAccounting\":2,\"source\":\"cpu-observation\",\"device\":"<<device<<",\"maxIntervals\":"<<limits.intervals<<",\"maxEventBytes\":"<<limits.eventBytes<<",\"maxMetadataBytes\":"<<limits.metadataBytes<<",\"gpuExecutionQualified\":false,\"jitterQualified\":false,\"preExposureQualified\":false}\n";file.flush();
    }
    ~Capture(){finish("shutdown");}
    bool active()const noexcept{return running.load(std::memory_order_relaxed);}
    bool finished()const noexcept{return done.load(std::memory_order_relaxed);}
    template<class F> void protect(F&& action)noexcept {try{action();}catch(...){failures.fetch_add(1,std::memory_order_relaxed);}}
    void gap(const std::string& what)noexcept{protect([&]{std::lock_guard<std::mutex> lock(mutex);if(done)return;loss(what);if(running)append(row("gap",quote(what)));});}
    uint64_t define(const std::string& type,uint64_t id,const std::string& data)noexcept{
        uint64_t revision{};protect([&]{std::lock_guard<std::mutex> lock(mutex);if(done||sealed)return;
            const auto key=std::make_pair(type,id);auto old=objects.find(key);const auto previous=old==objects.end()?0:old->second.data.size();
            if(old==objects.end()&&objects.size()>=limits.objects){loss("metadata-object-capacity");return;}
            if(metadataBytes-previous+data.size()>limits.metadataBytes){loss("metadata-byte-capacity");return;}
            revision=++sequence;metadataBytes=metadataBytes-previous+data.size();objects[key]={revision,data};
            if(metadataBytes>peakMetadataBytes)peakMetadataBytes=metadataBytes;if(objects.size()>peakObjects)peakObjects=objects.size();
            if(running)append(row("define","{\"type\":"+quote(type)+",\"id\":"+std::to_string(id)+",\"revision\":"+std::to_string(revision)+",\"value\":"+data+"}"));
        });return revision;
    }
    void forget(const std::string& type,uint64_t id)noexcept{protect([&]{std::lock_guard<std::mutex> lock(mutex);if(done||sealed)return;auto it=objects.find({type,id});if(it!=objects.end()){metadataBytes-=it->second.data.size();objects.erase(it);}if(running)append(row("forget","{\"type\":"+quote(type)+",\"id\":"+std::to_string(id)+"}"));});}
    void event(const std::string& kind,const std::string& data)noexcept{if(!active())return;protect([&]{std::lock_guard<std::mutex> lock(mutex);if(running)append(row(kind,data));});}
    bool sample(const char* kind,unsigned maximum)noexcept{
        if(!active())return false;bool accept=false;protect([&]{std::lock_guard<std::mutex> lock(mutex);if(!running)return;if(sampled[kind]++<maximum)accept=true;else ++skipped[kind];});return accept;
    }
    void boundary(uint64_t nextFrame,bool auditCollecting)noexcept{protect([&]{std::lock_guard<std::mutex> lock(mutex);if(done)return;
        if(sealed){flush("event-capacity");return;}
        if(!started){if(!auditCollecting)return;frame=nextFrame;started=true;
            for(const auto& object:objects)append(row("baseline","{\"type\":"+quote(object.first.first)+",\"id\":"+std::to_string(object.first.second)+",\"revision\":"+std::to_string(object.second.revision)+",\"value\":"+object.second.data+"}"));
            if(sealed){flush("event-capacity");return;}running=true;append(row("armed","{}"));return;}
        if(nextFrame==frame&&auditCollecting)return;
        ++intervals;append(row("retired","{\"nextSerial\":"+std::to_string(nextFrame)+"}"));
        if(intervals>=limits.intervals||!auditCollecting){flush(intervals>=limits.intervals?"interval-limit":"audit-stopped");return;}
        frame=nextFrame;sampled.clear();
    });}
    void finish(const char* why)noexcept{protect([&]{std::lock_guard<std::mutex> lock(mutex);flush(why);});}
};
inline std::shared_ptr<Capture> global;
inline std::shared_ptr<Capture> current(){return std::atomic_load(&global);}
inline bool publish(const std::shared_ptr<Capture>& value){std::shared_ptr<Capture> empty;return std::atomic_compare_exchange_strong(&global,&empty,value);}
inline void unpublish(const std::shared_ptr<Capture>& value){auto expected=value;std::atomic_compare_exchange_strong(&global,&expected,std::shared_ptr<Capture>{});}
}
