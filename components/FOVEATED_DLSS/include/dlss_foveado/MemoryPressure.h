#pragma once
#include <cstdint>

namespace dlss_foveado::d3d11 {
// Allocation headroom is not an out-of-memory error. Reusing prepared features
// below the actual DXGI budget must not disable the route at 90% residency.
class MemoryPressure {
    uint64_t localSince{},sharedSince{};bool localPending{},sharedPending{};
    static bool sustained(bool high,uint64_t now,bool allocating,uint64_t& since,bool& pending){
        if(!high){pending=false;return false;}
        if(!pending||now<since){since=now;pending=true;}
        return allocating||now-since>=1000;
    }
public:
    enum class Result { None, LocalBudget, Paging };
    Result Check(uint64_t now,uint64_t budget,uint64_t used,bool sharedKnown,
                 uint64_t shared,uint64_t baseline,bool allocating){
        const bool local=sustained(budget&&used>=budget,now,allocating,localSince,localPending);
        const bool paging=sustained(sharedKnown&&shared>baseline&&shared-baseline>256ull*1024*1024,
            now,allocating,sharedSince,sharedPending);
        return local?Result::LocalBudget:paging?Result::Paging:Result::None;
    }
};
}
