#pragma once
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <string>
namespace external_overlay {
constexpr uint32_t Width=960,Height=900,PixelsOffset=4096,Bytes=PixelsOffset+Width*Height*4;
// Keep transport ABI/size stable; the unused lower rows hold a cursor sprite.
constexpr uint32_t PanelWidth=672,PanelHeight=720,CursorSize=24;
constexpr float Distance=1.5f,WidthMeters=.84f,HeightMeters=WidthMeters*PanelHeight/PanelWidth,CenterX=-.08f,CenterY=0.f;
inline void StampCursor(unsigned char* pixels){
 for(uint32_t y=0;y<CursorSize;++y)for(uint32_t x=0;x<CursorSize;++x){
  auto* p=pixels+((PanelHeight+y)*Width+x)*4;const float dx=float(x)-11.5f,dy=float(y)-11.5f,r2=dx*dx+dy*dy;
  const bool inside=r2<=100.f,border=r2>=64.f;p[0]=inside?(border?255:234):0;p[1]=inside?(border?255:142):0;p[2]=inside?(border?255:38):0;p[3]=inside?255:0;
 }
}
struct Event {uint32_t sequence,type;float x,y;int32_t value;uint32_t reserved;};
struct Header {uint32_t version,frame,visible,writeSequence,readSequence,status;uint64_t hostTick;uint32_t reserved[8];};
static_assert(sizeof(Header)==64);static_assert(sizeof(Event)==24);
// Aim ray in view space. OpenXR aim forward is -Z; texture origin is top-left.
inline bool Intersect(float px,float py,float pz,float qx,float qy,float qz,float qw,float& x,float& y){
 const float dx=-2*(qx*qz+qw*qy),dy=-2*(qy*qz-qw*qx),dz=-(1-2*(qx*qx+qy*qy));
 if(dz>=-.001f)return false;
 const float t=(-Distance-pz)/dz;if(t<=0)return false;
 x=(px+t*dx-CenterX)/WidthMeters+.5f;y=.5f-(py+t*dy-CenterY)/HeightMeters;
 return std::isfinite(x)&&std::isfinite(y)&&x>=0&&x<=1&&y>=0&&y<=1;
}
class Surface {
 HANDLE mapping{},mutex{};void* memory{};
public:
 ~Surface(){if(memory)UnmapViewOfFile(memory);if(mapping)CloseHandle(mapping);if(mutex)CloseHandle(mutex);}
 bool Open(){if(memory)return true;auto name=L"Local\\ExternalOverlay.Surface."+std::to_wstring(GetCurrentProcessId());mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());if(!mapping)return false;mutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(name+L".Mutex").c_str());if(!mutex){CloseHandle(mapping);mapping=nullptr;return false;}memory=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,Bytes);return memory!=nullptr;}
 bool Lock(){if(!Open())return false;const auto r=WaitForSingleObject(mutex,0);if(r!=WAIT_OBJECT_0&&r!=WAIT_ABANDONED)return false;if(H()->version!=1){ReleaseMutex(mutex);return false;}return true;}
 void Unlock(){ReleaseMutex(mutex);}
 Header* H(){return static_cast<Header*>(memory);}
 const void* Pixels(){return static_cast<char*>(memory)+PixelsOffset;}
 void Push(uint32_t type,float x,float y,int32_t value=0){
  auto* h=H();auto* events=reinterpret_cast<Event*>(static_cast<char*>(memory)+64);
  // Pose sampling can run much faster than browser rendering. Keep the latest
  // unconsumed move (or repeated leave), but never replace a click transition.
  if(h->writeSequence!=h->readSequence&&(type==1||type==4)){
   auto& last=events[(h->writeSequence-1)%128];
   if(last.type==type){last.x=x;last.y=y;last.value=value;++h->reserved[4];return;}
  }
  if(h->writeSequence-h->readSequence>=128){
   if(type==1){++h->reserved[4];return;}
   h->status=2;++h->reserved[5];return;
  }
  const auto seq=++h->writeSequence;events[(seq-1)%128]={seq,type,x,y,value,0};
 }
};
}
