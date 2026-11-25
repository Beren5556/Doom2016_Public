#include <dlss_foveado/ConsumerPolicy.h>
#include <iostream>
#include <stdexcept>
using namespace dlss_foveado;
namespace p = consumer_policy;
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
int main() try {
    using experimental::Mode;
    // Independent accepted matrix: unavailable, SR, Neural, native AA.
    const bool matrix[4][3]{{true,false,false},{true,true,true},{true,true,false},{true,false,false}};
    std::array<p::Selection,3> presets;
    for(unsigned m=0;m<3;++m) {
        presets[m].config.central.topBps=1000+m*500;
        presets[m].config.enabled=m!=0;
        presets[m].settings.mode=Mode(m);presets[m].settings.centralDlaa=true;
    }
    for(unsigned r=0;r<4;++r) for(unsigned m=0;m<3;++m) {
        const auto saved=presets[m];const auto renderer=p::Renderer(r);
        check(p::Allows(Mode(m),renderer)==matrix[r][m],"mode matrix");
        for(bool ready:{false,true})for(bool projection:{false,true})
            check(p::CanApply(Mode(m),renderer,ready,projection)==
                (matrix[r][m]&&(m==0||(ready&&projection))),"recovery/readiness gate");
        const auto effective=p::Effective(saved,renderer);
        check(effective.settings.mode==(matrix[r][m]?Mode(m):Mode::Disabled),"startup fallback");
        check(effective.config.enabled==(matrix[r][m]&&m!=0),"render route enabled");
        check(!effective.settings.centralDlaa,"legacy flag cannot enable hybrid");
        check(presets[m].settings==saved.settings&&presets[m].config==saved.config,"saved preset destroyed");
        check(effective.config.central.topBps==saved.config.central.topBps,"mode draft lost");
    }
    check(!p::Allows(Mode(99),p::Renderer::SuperResolution),"invalid mode");
    check(!p::Allows(Mode::Fixed,p::Renderer(99)),"unknown renderer");
    // A safe-point mode race from SR to native AA must disable the live copy.
    check(p::CanApply(Mode::Dynamic,p::Renderer::SuperResolution,true,true),"initial request");
    check(!p::Effective(presets[2],p::Renderer::NativeAA).config.enabled,"render race");
    std::cout << "PASS: opt-in consumer matrix, legacy flag, readiness recovery, presets and safe-point fallback\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
