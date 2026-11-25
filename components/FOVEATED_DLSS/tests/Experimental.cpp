#include <dlss_foveado/Experimental.h>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace dlss_foveado::experimental;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
struct PacedRun {
    Settings settings;Controller controller;double clock{1000};uint64_t sequence{},generation{1};
    explicit PacedRun(double hz){settings.mode=Mode::Dynamic;controller.Configure(settings,1000);controller.FramePeriod(1000./hz);}
    void feed(double ms){
        clock+=ms;const auto now=uint64_t(clock);
        controller.Update({++sequence,generation,now,ms},generation,now);
        // Automatic mask movement retains the B3 route generation (exp6).
    }
    void constant(double seconds,double ms){const auto end=clock+seconds*1000;while(clock<end)feed(ms);}
    void minimum(){constant(5,controller.FramePeriodMs()*1.7);check(controller.Current()==settings.inner,"recovery fixture must first contract to minimum");}
};
void CheckPacedRecovery(){
    for(const double hz:{72.,90.,120.}){
        PacedRun r(hz);r.minimum();const auto period=r.controller.FramePeriodMs();
        const auto end=r.clock+10000;unsigned n=0;
        while(r.clock<end)r.feed(period+(++n%2?.9:-.9));
        check(r.controller.Current()==r.settings.outer,"balanced frame pacing jitter must recover all the way from minimum to maximum");
        r.minimum();
    }
    {
        PacedRun r(72);r.minimum();const auto period=r.controller.FramePeriodMs();
        // One missed deadline per second still leaves the window near 72 Hz.
        const auto end=r.clock+12000;unsigned n=0;
        while(r.clock<end)r.feed(++n%72?period:period*2);
        check(r.controller.Current()==r.settings.outer,"isolated missed deadlines must not permanently prevent recovery");
    }
    {
        PacedRun r(90);r.minimum();const auto period=r.controller.FramePeriodMs();
        const auto tolerance=std::max(.35,period*.04);
        r.constant(8,period+tolerance*.75);
        check(r.controller.Current()==r.settings.inner,"persistent recovery deadband must not grow the region");
        r.constant(8,period*1.35);
        check(r.controller.Current()==r.settings.inner,"sustained overload must never initiate a quality probe");
        r.constant(8,period);
        check(r.controller.Current()==r.settings.outer,"healthy pacing must recover after sustained overload stops");
    }
    {
        PacedRun r(72);r.minimum();const auto period=r.controller.FramePeriodMs();
        r.constant(r.settings.recoveryDelayMs/1000.*.7,period);check(r.controller.Current()==r.settings.inner,"recovery needs sustained healthy evidence");
        r.clock+=500;r.constant(r.settings.recoveryDelayMs/1000.*.7,period);
        check(r.controller.Current()==r.settings.inner,"a timing gap must restart recovery observation");
        ++r.generation;r.constant(r.settings.recoveryDelayMs/1000.*.7,period);
        check(r.controller.Current()==r.settings.inner,"a new route generation must restart recovery observation");
        r.controller.Stabilize(uint64_t(r.clock));r.constant(2+r.settings.recoveryDelayMs/1000.*.7,period);
        check(r.controller.Current()==r.settings.inner,"stabilization must not count towards healthy recovery time");
        r.constant(8,period);check(r.controller.Current()==r.settings.outer,"fresh recovery works after gap, generation change and stabilization");
    }
    {
        PacedRun r(72);r.minimum();const auto period=r.controller.FramePeriodMs();
        r.constant(r.settings.recoveryDelayMs/1000.*.7,period);
        r.clock+=period;const auto now=uint64_t(r.clock);
        r.controller.Update({++r.sequence,r.generation,now,std::numeric_limits<double>::quiet_NaN()},r.generation,now);
        r.constant(r.settings.recoveryDelayMs/1000.*.7,period);
        check(r.controller.Current()==r.settings.inner,"invalid timing must not bridge a recovery window");
        r.controller.ReportChangeCpuMs(85);r.constant(10,period);
        check(r.controller.Current()==r.settings.inner&&r.controller.State()==Status::ReconfigurationPaused,"CPU stall guard must still freeze recovery");
    }
    std::cout<<"PASS: paced contraction/recovery cycles at 72/90/120 Hz, jitter, isolated missed deadlines, sustained overload, gaps, generation changes and stabilization\n";
}
int main()try{
    Settings s;s.mode=Mode::Dynamic;check(s.Valid(),"defaults invalid");Controller c;c.Configure(s,1000);
    check(c.Current()==Cuts{2000,2000,2000,2000},"initial midpoint");
    uint64_t now=1000,sequence=0;
    auto feed=[&](double ms,uint64_t generation=1){now+=100;return c.Update({++sequence,generation,now,ms},generation,now);};
    for(unsigned i=0;i<160;++i)feed(20);
    check(c.Current()==s.inner&&c.State()==Status::TargetUnmet,"sustained load must reach minimum and report unmet target");
    const auto before=c.Current();check(!c.Update({sequence+1,1,now,2},1,now,true)&&c.Current()==before&&c.State()==Status::NeuralBlocked,"Neural must freeze Dynamic");
    for(unsigned i=0;i<400;++i)feed(2);
    check(c.Current()==s.outer&&c.State()==Status::Maximum,"slow recovery reaches maximum");
    c.Configure(s,now);for(unsigned i=0;i<100;++i)feed(i%2?11.2:11.0);
    check(c.Current()==Cuts{2000,2000,2000,2000},"tolerance should prevent oscillation");
    check(!c.Update({sequence,1,now,20},1,now),"duplicate sample accepted");
    check(!c.Update({++sequence,1,now,20},2,now),"wrong generation accepted");
    check(!c.Update({++sequence,1,now-500,20},1,now+500)&&c.State()==Status::Stale,"stale timing accepted");
    check(!c.Update({++sequence,1,now,std::numeric_limits<double>::quiet_NaN()},1,now),"NaN accepted");
    c.ReportChangeCpuMs(85);const auto paused=c.Current();
    for(unsigned i=0;i<100;++i)feed(20);
    check(c.State()==Status::ReconfigurationPaused&&c.Current()==paused&&c.ChangeCpuMs()==85,"costly recreation must not repeat automatically");
    c.Configure(s,now);check(c.State()==Status::Waiting&&c.ChangeCpuMs()==0,"explicit Apply permits retry");
    for(unsigned i=0;i<14;++i)feed(2);
    const auto preSpike=c.Current();feed(20);
    check(c.Current()==preSpike,"overloaded raw sample must not grow the region while the filter catches up");
    c.Configure(s,now);
    // Duration means a consecutive valid run, not a cooldown since the last
    // adjustment and not a sum of separated spikes.
    for(unsigned i=0;i<10;++i)feed(11.1);
    const auto initial=c.Current();
    for(unsigned burst=0;burst<20;++burst){for(unsigned i=0;i<4;++i)feed(20);feed(11.1);}
    check(c.Current()==initial,"separate brief spikes must not accumulate into sustained overload");
    for(unsigned i=0;i<10;++i)feed(20);
    check(c.Current()==initial&&c.State()==Status::ConfirmingOverload,"900 ms below 1000 ms hold must not contract");
    feed(20);check(c.Current()!=initial,"full sustained duration triggers contraction");
    s.sustainedOverloadMs=1500;c.Configure(s,now);
    for(unsigned i=0;i<15;++i)feed(20);
    check(c.Current()==initial,"custom 1500 ms duration respected");
    feed(20);check(c.Current()!=initial,"custom duration releases contraction");
    c.Configure(s,now);for(unsigned i=0;i<10;++i)feed(20);
    now+=400;feed(20);for(unsigned i=0;i<5;++i)feed(20);
    check(c.Current()==initial,"missing fresh samples cannot complete the overload hold");
    for(unsigned i=0;i<10;++i)feed(20,2);
    check(c.Current()==initial,"a new generation restarts the sustained interval");
    s.sustainedOverloadMs=99;check(!s.Valid(),"too short sustained duration accepted");
    s.sustainedOverloadMs=10001;check(!s.Valid(),"unbounded sustained duration accepted");s.sustainedOverloadMs=1000;
    // 72 Hz pacing is not overload relative to a stored 90 Hz target.
    s.targetMicros=11100;c.Configure(s,now);c.FramePeriod(1000./72.);
    for(unsigned i=0;i<120;++i)feed(13.89);
    check(c.Current()==s.outer,"runtime-paced healthy frames should recover, not shrink");
    for(unsigned i=0;i<120;++i)feed(22.);
    check(c.Current()==s.inner,"sustained missed runtime budget contracts");
    for(unsigned i=0;i<120;++i)feed(13.95);
    check(c.Current()==s.outer,"paced recovery must leave the minimum");
    c.Configure(s,now);c.FramePeriod(1000./72.);
    for(unsigned n=0;n<8;++n){for(unsigned i=0;i<3;++i)feed(22.);for(unsigned i=0;i<30;++i)feed(13.89);}
    check(c.Current()==s.outer,"isolated paced dips must permit recovery rather than shrink");
    // Formerly each fast interval restarted the hold, starving contraction
    // indefinitely even though the mean is 20.5 ms against a 13.89 ms target.
    PacedRun irregular(72);const auto end=irregular.clock+7000;unsigned pair=0;
    while(irregular.clock<end)irregular.feed(++pair%2?1.:40.);
    check(irregular.controller.Current()==irregular.settings.inner,"sustained irregular overload must contract despite fast individual intervals");
    irregular.constant(8,1000./72.);
    check(irregular.controller.Current()==irregular.settings.outer,"irregular contraction must recover when runtime target is restored");
    c.Configure(s,now);c.FramePeriod(1000./120.);c.Stabilize(now);
    check(c.EffectiveTargetMs()<8.34,"runtime 120 Hz overrides legacy stored target");
    for(unsigned i=0;i<19;++i)feed(25.);
    check(c.Current()==Cuts{2000,2000,2000,2000}&&c.State()==Status::Stabilizing,"startup settling ignores slow samples");
    for(unsigned i=0;i<10;++i)feed(25.);
    check(c.Current()==Cuts{2000,2000,2000,2000},"startup load cannot count towards sustained overload");
    for(unsigned i=0;i<4;++i)feed(25.);check(c.Current()!=Cuts{2000,2000,2000,2000},"valid aggregated overload after stabilization contracts");
    s.inner[0]=500;check(!s.Valid(),"crossed bounds accepted");s.inner[0]=3001;check(!s.Valid(),"fractional control accepted");
    // The external delay controls readiness, independently of the unchanged ramp.
    for(const unsigned delay:{500u,1000u,2500u}){
        Settings timed;timed.mode=Mode::Dynamic;timed.recoveryDelayMs=delay;
        Controller t;t.Configure(timed,1000);t.FramePeriod(10);
        unsigned first=0;const auto center=t.Current();
        for(unsigned ms=10;ms<=delay+1000;ms+=10){
            t.Update({ms/10,1,1000+ms,10.},1,1000+ms);
            if(t.Current()!=center){first=ms;break;}}
        check(first>=delay&&first<=delay+100,"configured recovery delay must control first expansion, including below old cooldown");
    }
    Settings delayLimits;check(delayLimits.sustainedOverloadMs==1000&&delayLimits.recoveryDelayMs==1000,"both default waits are one second");
    delayLimits.recoveryDelayMs=99;check(!delayLimits.Valid(),"short recovery delay rejected");
    delayLimits.recoveryDelayMs=10001;check(!delayLimits.Valid(),"large recovery delay rejected");
    CheckPacedRecovery();
    std::cout<<"PASS: bounded adaptive control, sustained overload, isolated spikes, hysteresis, recovery, duplicate/stale/generation rejection and Neural block\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
