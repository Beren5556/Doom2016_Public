#include "../src/native/NativeStartupControls.h"
#include "../src/overlay/DoomLayerControl.h"
#include <map>
#include <iostream>
using namespace kharvox::native;
int main(){
 int failures=0,writes=0,reports=0;auto check=[&](bool b){if(!b)++failures;};
 // Regression: changing resolution must not override the motor's virtual
 // texture sampling or residency. Resolution controls still apply in SFS.
 for(bool native:{false,true})for(bool sfs:{false,true})for(bool own:{false,true}){
  std::map<uintptr_t,std::string> values{{0x66e0760,"-0.25"},{0x66e04e0,"0.5"}};
  for(const auto* scale:{"1","0.6667","0.5","0.3333","0.6667","1"}){
   const auto c=rendererStartupControls(native,false,false,false,false,false,sfs,own,scale);
   check(!nativePresetControl(c,0x66e0760)&&!nativePresetControl(c,0x66e04e0));
   check(initializeStartupControls(true,c,[&](const auto& control,std::string& value){value=values[control.rva];return true;},
    [&](const auto& control){values[control.rva]=control.value;return true;},[](const auto&,const auto&,const auto&){}));
   check(values[0x66e0760]=="-0.25"&&values[0x66e04e0]=="0.5");
   if(!native&&sfs&&own)check(values[0x672c560]==scale&&values[0x672c600]==scale&&values[0x672c4c0]==scale);
  }
 }
 for(const auto* text:{"1 2 11 1.3", "1 1 11 0.5", "1 2 11 3.0\n", "1 2 0 1.3"})check(bool(kharvox::overlay::parseSelection(text)));
 for(const auto* text:{"", "1 2 11", "2 2 11 1.3", "1 3 11 1.3", "1 4 11 1.3", "1 2 9 1.3", "1 2 11 nan", "1 2 11 inf", "1 2 11 .49", "1 2 11 3.01", "1 2 11 1.3 extra"})check(!kharvox::overlay::parseSelection(text));
 const std::string fov="3;1;1;2000;2000;2000;2000;1000;0;7500;11;11;10000;0;1;1;2000;2000;2000;2000;1000;0";
 const std::string prefix="3 3 12 1.3 0 6667 0 85 0 "+fov;
 const std::string appearanceControls=" 1 110 80 90 0 70 1 75 50 30 25 ";
 const std::string bounds=" 11100 1000 1000 3000 3000 3000 3000 1000 1000 1000 1000 42";
 const auto dynamic=kharvox::overlay::parseSelection(prefix+appearanceControls+"2"+bounds);
 check(dynamic&&dynamic->preset==12&&dynamic->foveationMode==2&&dynamic->sharpen==25&&dynamic->revision==42);
 check(!kharvox::overlay::parseSelection(prefix+appearanceControls+"0"+bounds)); // inconsistent enable bit
 check(!kharvox::overlay::parseSelection("3 4 11 1.3 0 6667 0 85 0 "+fov+appearanceControls+"2"+bounds));
 check(!kharvox::overlay::parseSelection("3 2 11 1.3 0 6667 0 85 0 "+fov+appearanceControls+"1"+bounds));
 check(!kharvox::overlay::parseSelection(prefix+";"+appearanceControls+"1"+bounds));
 const auto legacy=kharvox::overlay::parseSelection("2 3 11 1.3 0 6667 0 85 0 "+fov);
 check(legacy&&legacy->foveationMode==1);
 for(bool disableAa:{false,true})for(bool force:{false,true}){
  const auto profile=rendererStartupControls(false,false,false,false,false,disableAa,true,true);
  const auto* aa=nativePresetControl(profile,0x66dd200);
  const std::string expected=disableAa?"0":"6";
  check(aa&&std::string(aa->name)=="r_antialiasing"&&std::string(aa->value)==expected);
  check(setProtectedRenderControl(aa,"0",force,[&](const char* value,bool forwarded){
   return std::string(value)==expected&&forwarded==force;
  }));
 }
 for(bool sfs:{false,true})for(bool native:{false,true})for(bool disableAa:{false,true}){
  const auto profile=rendererStartupControls(native,false,false,false,false,disableAa,sfs);
  const auto* aa=nativePresetControl(profile,0x66dd200);
  check(bool(aa)==(native||!sfs||disableAa));
  if(!aa)for(const char* mode:{"0","1","2","3","4"})
   check(setProtectedRenderControl(aa,mode,false,[&](const char* value,bool){return std::string(value)==mode;}));
  const auto* temporal=nativePresetControl(profile,0x672bc30);
  check(bool(temporal)==(sfs&&!native));
  for(auto rva:{uintptr_t(0x66d9380),uintptr_t(0x6727e70)}){
   const auto* flare=nativePresetControl(profile,rva);
   check(bool(flare)==(sfs&&!native));
   if(flare){
    const std::string expected=rva==0x66d9380?"1":"0";
    check(std::string(flare->name)==(rva==0x66d9380?"r_skipFlares":"r_lensFlaresRatio"));
    for(bool force:{false,true})check(setProtectedRenderControl(flare,"99",force,[&](const char* value,bool forwarded){
     return std::string(value)==expected&&force==forwarded;
    }));
   }
  }
  if(temporal){
   check(std::string(temporal->name)=="r_SSDOTemporalAA"&&std::string(temporal->value)=="0");
   for(bool force:{false,true})check(setProtectedRenderControl(temporal,"1",force,[&](const char* value,bool forwarded){
    return std::string(value)=="0"&&force==forwarded;
   }));
  }
 }
 const uintptr_t debugRvas[]={0x6727ac0,0x66dd2a0,0x66e0760,0x66e04e0,0x5b5ec50};
 for(bool native:{false,true}) {
  const auto profile=rendererStartupControls(native,false,false,false);
  for(auto rva:debugRvas) check(!nativePresetControl(profile,rva));
 }
 const auto controls=nativeStartupControls(true,true,false,true);
 check(std::string(nativePresetControl(controls,0x66dd200)->value)=="2");
 check(controls.size()==12);check(nativeStartupControls(false,false,false).size()==7);
 const auto aer=rendererStartupControls(false,true,true,true,true);
 check(aer.size()==7&&std::string(aer[0].name)=="r_antialiasing");
 check(!nativePresetControl(aer,0x5d48350)); // AER must not force/protect r_useSMP.
 check(nativePresetControl(aer,0x6728df0)&&!nativePresetControl(aer,0x6fd58a0)&&!nativePresetControl(aer,0x6c0b620));
 for(bool isNative:{false,true})for(bool force:{false,true})for(bool disableAa:{false,true}){
  const auto profile=rendererStartupControls(isNative,false,false,false,false,disableAa);
  const auto* aa=nativePresetControl(profile,0x66dd200);
  const std::string expected=disableAa?"0":"2";
  std::string actual="6";
  // Presets must not select temporal AA or defeat either renderer's debug override.
  check(setProtectedRenderControl(aa,isNative?"6":"0",force,[&](const char* value,bool forwarded){
   check(forwarded==force);actual=value;return true;
  })&&actual==expected);
  check(!setProtectedRenderControl(aa,isNative?"6":"0",force,[&](const char* value,bool forwarded){
   check(forwarded==force&&std::string(value)==expected);return false;
  })); // Never hide the engine's permission refusal.
  for(auto rva:{uintptr_t(0x67288e0),uintptr_t(0x6728ca0),uintptr_t(0x6727970),uintptr_t(0x6728df0),uintptr_t(0x672b5c0),uintptr_t(0x672bae0)}){
   const auto* visual=nativePresetControl(profile,rva);check(visual!=nullptr);
   check(setProtectedRenderControl(visual,"99",force,[&](const char* value,bool forwarded){
    check(forwarded==force&&std::string(value)==(rva==0x6728ca0?"2":"0"));return true;
   }));
  }
  check(setProtectedRenderControl(nullptr,"2",force,[&](const char* value,bool forwarded){
   check(forwarded==force&&std::string(value)=="2");return true;
  }));
 }
 const auto* asyncControl=nativePresetControl(controls,0x6fd58a0);
 check(asyncControl&&std::string(asyncControl->name)=="r_enableAsyncCompute"&&std::string(asyncControl->value)=="0");
 const auto baseline=nativeStartupControls(true,true,false);
 check(baseline.size()==11&&!nativePresetControl(baseline,0x6fd58a0));
 check(!nativePresetControl(controls,0x1234)); // Unrelated effect settings pass through.
 const auto cached=nativeStartupControls(false,false,true);check(std::string(cached.back().value)=="-1,16,8,4,2");
 std::map<uintptr_t,std::string> values;for(auto& c:controls)values[c.rva]="old";
 bool readable=true,allowed=true,apply=true;
 auto read=[&](const StartupControl& c,std::string& v){v=values[c.rva];return readable;};
 auto write=[&](const StartupControl& c){++writes;if(allowed&&apply)values[c.rva]=c.value;return allowed;};
 auto report=[&](const auto&,const auto&,const auto&){++reports;};
 check(!initializeStartupControls(false,controls,read,write,report)&&writes==0);
 readable=false;check(!initializeStartupControls(true,controls,read,write,report)&&writes==0);
 values[asyncControl->rva]="1";
 readable=true;check(initializeStartupControls(true,controls,read,write,report));check(writes==12&&reports==12);
 check(values[asyncControl->rva]=="0");
 check(initializeStartupControls(true,controls,read,write,report)&&writes==12);
 values[controls[0].rva]="6";allowed=false;
 check(!initializeStartupControls(true,controls,read,write,report));
 allowed=true;apply=false;check(!initializeStartupControls(true,controls,read,write,report));
 apply=true;check(initializeStartupControls(true,controls,read,write,report));
 const auto before=writes;
 auto wrongIdentity=[&](const StartupControl& c,std::string& v){return c.rva!=controls.back().rva&&read(c,v);};
 check(!initializeStartupControls(true,controls,wrongIdentity,write,report)&&writes==before);
 std::cout<<"Native startup controls failures="<<failures<<'\n';return failures?1:0;
}
