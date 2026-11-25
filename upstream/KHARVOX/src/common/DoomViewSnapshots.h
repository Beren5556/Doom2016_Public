#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace kharvox::doom_temporal {
struct ViewSnapshot {
    std::array<float,16> jittered{},unjittered{},view{};
    std::array<int32_t,4> rectangle{};
    uint64_t source{},present{},sequence{},timeNs{};
    uint8_t mode{},phase{};
};
class ViewSnapshots {
    std::array<ViewSnapshot,32> ring{};uint64_t sequence{};std::mutex mutex;
public:
    std::vector<ViewSnapshot> recent(){
        std::lock_guard<std::mutex> lock(mutex);std::vector<ViewSnapshot> result;
        for(uint64_t n=0;n<ring.size()&&n<sequence;++n)result.push_back(ring[(sequence-1-n)%ring.size()]);
        return result;
    }
    static uint64_t now(){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    template<class Read> void observe(Read&& read,uintptr_t view,uintptr_t rectangle,uint64_t present){
        ViewSnapshot s;s.source=view;s.present=present;s.timeNs=now();
        if(!view||!rectangle||!read(view+0x1130,s.jittered.data(),64)||!read(view+0x1170,s.unjittered.data(),64)||!read(view+0x11f0,s.view.data(),64)||
           !read(rectangle,s.rectangle.data(),16)||!read(view+0x1111,&s.mode,1)||!read(view+0x1112,&s.phase,1)||s.mode||s.phase>=8)return;
        std::lock_guard<std::mutex> lock(mutex);s.sequence=++sequence;ring[(sequence-1)%ring.size()]=s;
    }
    // Callers match against the currently bound TAA constants, never select a
    // view solely because it was the most recent CPU camera update.
    template<class Match> bool find(Match&& matches,ViewSnapshot& result){
        const auto time=now();std::lock_guard<std::mutex> lock(mutex);
        for(uint64_t n=0;n<ring.size()&&n<sequence;++n){const auto& candidate=ring[(sequence-1-n)%ring.size()];
            if(candidate.timeNs<=time&&time-candidate.timeNs<=250000000&&matches(candidate)){result=candidate;return true;}}
        return false;
    }
};
inline std::shared_ptr<ViewSnapshots> views;
inline std::shared_ptr<ViewSnapshots> current(){return std::atomic_load(&views);}
inline bool publish(const std::shared_ptr<ViewSnapshots>& value){std::shared_ptr<ViewSnapshots> empty;return std::atomic_compare_exchange_strong(&views,&empty,value);}
inline void unpublish(const std::shared_ptr<ViewSnapshots>& value){auto expected=value;std::atomic_compare_exchange_strong(&views,&expected,std::shared_ptr<ViewSnapshots>{});}
}
