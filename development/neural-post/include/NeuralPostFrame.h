#pragma once
#include "NeuralPostAPI.h"
#include <cmath>
#include <cstring>

namespace es2np {
inline bool ValidFrame(const Frame& f){
 if(f.size!=sizeof(Frame)||f.abi!=Abi||!f.epoch||!f.source||!f.timeNs||!f.width||!f.height||f.width>16384||f.height>16384)return false;
 for(const auto& e:f.eyes){
  if(e.left>=e.right||e.top>=e.bottom||e.right>f.width||e.bottom>f.height)return false;
  if(e.right-e.left<64||e.bottom-e.top<64)return false; // all allowed scale/fovea combinations remain nonzero
  double n{};for(auto q:e.orientation){if(!std::isfinite(q))return false;n+=double(q)*q;}if(n<.9||n>1.1)return false;
  for(auto a:e.fov)if(!std::isfinite(a)||std::abs(a)>=1.5707f)return false;
  if(e.fov[0]>=e.fov[1]||e.fov[3]>=e.fov[2])return false;
 }
 const auto& a=f.eyes[0];const auto& b=f.eyes[1];
 return a.right<=b.left;
}
inline bool SameGeometry(const Frame& a,const Frame& b){
 if(a.width!=b.width||a.height!=b.height||a.epoch!=b.epoch||a.generation!=b.generation)return false;
 for(int i=0;i<2;++i){const auto& x=a.eyes[i];const auto& y=b.eyes[i];
  if(x.left!=y.left||x.top!=y.top||x.right!=y.right||x.bottom!=y.bottom)return false;
  for(int j=0;j<4;++j)if(x.fov[j]!=y.fov[j])return false;
 }return true;
}
inline bool SameFrame(const Frame& a,const Frame& b){
 if(a.source!=b.source||a.timeNs!=b.timeNs||!SameGeometry(a,b))return false;
 for(int i=0;i<2;++i)for(int j=0;j<4;++j)if(a.eyes[i].orientation[j]!=b.eyes[i].orientation[j])return false;
 return true;
}
inline bool ResetRequired(const Frame& a,const Frame& b){return !SameGeometry(a,b)||b.source<=a.source||b.timeNs<a.timeNs||b.timeNs-a.timeNs>250000000ull;}
}
