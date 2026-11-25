// Run only from the isolated fixture created by overlay/tests/test_bootstrap.py.
#include "../src/overlay/DoomLayerControl.h"
#include <iostream>

int main(int argc,char** argv){
 try{
  if(argc!=2||std::string(argv[1])!="--isolated-fixture")return 2;
  using namespace kharvox::overlay;
  if(!enabled())throw std::runtime_error("Fixture marker missing");
  prepareEnvironment();
  acknowledge(91,true);
  const auto receipt=root()/(L"ack-"+std::to_wstring(GetCurrentProcessId())+L".json");
  const auto reader=CreateFileW(receipt.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  if(reader==INVALID_HANDLE_VALUE)throw std::runtime_error("Receipt reader fixture failed");
  acknowledge(92,true);
  std::ifstream first(receipt);std::string contents;std::getline(first,contents);first.close();
  if(contents.find("91")==std::string::npos)throw std::runtime_error("Locked receipt unexpectedly replaced");
  CloseHandle(reader);flushAcknowledgement();
  std::ifstream second(receipt);std::getline(second,contents);second.close();
  if(contents.find("92")==std::string::npos)throw std::runtime_error("Receipt lost after temporary sharing violation");
  const bool active=startup().mode==2;
  for(const char* name:{"KHARVOX_TEMPORAL_BRIDGE","KHARVOX_TEMPORAL_PARAMETERS","KHARVOX_DLSS_DLAA","KHARVOX_DLSS_RAW_HDR","KHARVOX_DLSS_PREPARE"}){
   char value[8]{};GetEnvironmentVariableA(name,value,sizeof(value));
   const bool required=active||std::string(name)=="KHARVOX_TEMPORAL_BRIDGE"||std::string(name)=="KHARVOX_TEMPORAL_PARAMETERS"||std::string(name)=="KHARVOX_DLSS_PREPARE";
   if(std::string(value)!=(required?"1":"0"))throw std::runtime_error(name);
  }
  char aa[8]{};GetEnvironmentVariableA("KHARVOX_DISABLE_AA",aa,sizeof(aa));
  if(std::string(aa)!="0")throw std::runtime_error("Mod AA override leaked");
  publish(startup().mode,active?120:0,active?2:0,3500,3750,active);
  if(!prepareXr())throw std::runtime_error("Own layer bootstrap disabled");
  const auto name=L"Local\\ExternalOverlay.Surface."+std::to_wstring(GetCurrentProcessId());
  bool frame=false;const auto until=GetTickCount64()+15000;
  while(!frame&&GetTickCount64()<until){
   HANDLE mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());
   HANDLE mutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(name+L".Mutex").c_str());
   if(mapping&&mutex){
    const auto* bytes=static_cast<const unsigned*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,4096));
    const auto wait=WaitForSingleObject(mutex,1000);
    if(wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED){
     frame=bytes&&bytes[0]==1&&bytes[1]>0;
     ReleaseMutex(mutex);
    }
    if(bytes)UnmapViewOfFile(bytes);
   }
   if(mapping)CloseHandle(mapping);if(mutex)CloseHandle(mutex);
   if(!frame)Sleep(50);
  }
  if(!frame)throw std::runtime_error("No panel frame received from automatic host");
  std::cout<<"PASS automatic host surface frame; mode="<<startup().mode<<" pid="<<GetCurrentProcessId()<<'\n';
  return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
