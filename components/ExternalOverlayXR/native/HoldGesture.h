#pragma once
#include <cstdint>
namespace external_overlay {
// A sustained gesture emits one OPEN, never a toggle. Re-arm only after all
// participating controls have been released. Inactive/missing tracking resets.
class HoldGesture {
 bool armed=true,holding=false;
 uint64_t since{};
public:
 bool Update(uint64_t now,bool allActive,bool allDown,bool allReleased){
  if(!allActive){holding=false;return false;}
  if(allReleased){armed=true;holding=false;return false;}
  if(!armed||!allDown){holding=false;return false;}
  if(!holding){holding=true;since=now;return false;}
  if(now<since){holding=false;return false;}
  if(now-since<2000)return false;
  armed=false;holding=false;return true;
 }
};
}
