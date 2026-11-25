#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace kharvox::native {
struct StartupControl { uintptr_t rva; const char* name; const char* value; };
// Verified DOOM CVar registrations: constructor RVA 0x293870.
inline std::vector<StartupControl> visualStartupControls(bool disableAa=false){
 return {{0x66dd200,"r_antialiasing",disableAa?"0":"2"},
  {0x67288e0,"r_filmGrainRatio","0"},
  {0x6728ca0,"r_sharpening","2"},
  {0x6727970,"r_motionblur","0"}, // Pass gate at RVA 0x18735D2; quality 0 still selects a kernel.
  {0x6728df0,"r_motionBlurQuality","0"},
  {0x672b5c0,"r_SSR","0"}, // Verified pass enable switch; quality 0 alone keeps SSR active.
  {0x672bae0,"r_SSRQuality","0"}}; // Lowest SSR quality, not proven disabled.
}
inline std::vector<StartupControl> nativeStartupControls(bool fresh,bool uncached,bool cached,bool disableAsync=false,bool disableAa=false){
 auto controls=visualStartupControls(disableAa);
 // The engine refused this experimental override in r166 before rendering.
 // Keep it explicit; never force writes past engine permissions.
 if(disableAsync)controls.push_back({0x6fd58a0,"r_enableAsyncCompute","0"});
 if(uncached||cached){controls.push_back({0x6c0b620,"r_shadowStaticMode","1"});
  controls.push_back({0x6eacb90,"r_shadowStaticMipTriThresholds",cached?"-1,16,8,4,2":"-1,-1,-1,-1,-1"});}
 if(fresh){controls.push_back({0x6c0b830,"r_shadowMaxStaleFrames","0,0,0,0,0"});
  controls.push_back({0x6eac480,"r_shadowParallelMaxStaleFrames","0,0,0,0"});}
 return controls;
}
// Shared visual settings, never Native shadow/queue controls in AER.
inline std::vector<StartupControl> rendererStartupControls(bool native,bool fresh,bool uncached,bool cached,bool disableAsync=false,bool disableAa=false,bool sfs=false,bool ownLayer=false,const char* renderFraction=nullptr){
 if(native)return nativeStartupControls(fresh,uncached,cached,disableAsync,disableAa);
 auto controls=visualStartupControls(disableAa);
 // Independent of r_antialiasing: registration 0x2327DD, default 1.
 // The history pass at 0x1874673 is skipped when this is zero. Late-bound
 // controller models cannot use camera-only occlusion history reliably.
 // Keep current-frame SSDO, direct shadows and ordinary AER unchanged.
 if(sfs){
  // The own Layer needs the engine temporal source. Otherwise retain the
  // existing SFS/game selection and explicit debug override behavior.
  if(ownLayer)controls.front().value=disableAa?"0":"6"; // DOOM registration: TSSAA 8TX, single sample; our temporal source.
  else if(!disableAa)controls.erase(controls.begin());
  controls.push_back({0x672bc30,"r_SSDOTemporalAA","0"});
  // Verified registrations at 0x226436 / 0x2301C6. Disable both flare
  // model submission and lens-flare composition only in the SFS renderer.
  controls.push_back({0x66d9380,"r_skipFlares","1"});
  controls.push_back({0x6727e70,"r_lensFlaresRatio","0"});
  if(ownLayer&&renderFraction){
   // Read-only registration audit: RVAs 0x2331B6..0x2332B4, CVar constructor
   // 0x293870 in the pinned DOOM executable. Fixed engine resolution; no
   // reduction of the native final framebuffer or the OpenXR swapchains.
   controls.push_back({0x672c420,"rs_enable",std::string(renderFraction)=="1"?"0":"3"});
   controls.push_back({0x672c560,"rs_forceFractionX",renderFraction});
   controls.push_back({0x672c600,"rs_forceFractionY",renderFraction});
   controls.push_back({0x672c4c0,"rs_minimumResolutionScale",renderFraction});
  }
 }
 return controls;
}
template<class Set>
bool setProtectedRenderControl(const StartupControl* control,const char* requested,bool force,const Set& setter){
 return setter(control?control->value:requested,force);
}
inline const StartupControl* nativePresetControl(const std::vector<StartupControl>& controls,uintptr_t rva){
 for(const auto& control:controls)if(control.rva==rva)return &control;
 return nullptr;
}
template<class Read,class Write,class Report>
bool initializeStartupControls(bool beforeRendering,const std::vector<StartupControl>& controls,
 const Read& read,const Write& write,const Report& report){
 if(!beforeRendering)return false;
 std::vector<std::string> before(controls.size());
 // Validate every object before changing any setting.
 for(size_t i=0;i<controls.size();++i)if(!read(controls[i],before[i]))return false;
 for(size_t i=0;i<controls.size();++i){
  const auto& c=controls[i];std::string after;
  if(before[i]!=c.value&&!write(c))return false;
  if(!read(c,after)||after!=c.value)return false;
  report(c,before[i],after);
 }
 return true;
}
}
