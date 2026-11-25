#include "WorkerProtocol.h"
#include "DirectNR.hpp"
#include <cstdarg>
#include <mutex>
#include <memory>
#include <d3d12sdklayers.h>
namespace {FILE* logFile{};size_t logBytes{};std::mutex logMutex;}
void BridgeLog(const char* fmt,...){std::lock_guard lock(logMutex);if(!logFile||logBytes>2*1024*1024)return;char b[4096];va_list a;va_start(a,fmt);vsnprintf_s(b,_TRUNCATE,fmt,a);va_end(a);logBytes+=fprintf(logFile,"%llu %s\n",GetTickCount64(),b);fflush(logFile);}
namespace es2np {
DirectConfig Translate(const Config& c,uint32_t diagnostic){const auto* v=c.values;DirectConfig d;
 d.scale=float(v[ScalePercent]/100);d.intensity=float(v[IntensityPercent]/100);d.passes=int(v[Passes]);d.residual=v[Residual]!=0;d.style=unsigned(v[Style]);d.preset=unsigned(v[Preset]);
 d.local_structure=float(v[LocalStructure]);d.local_tone=float(v[LocalTone]);d.skin_structure=v[SkinAuto]?-1:float(v[SkinStrength]);d.auto_mask=v[AutoMask]!=0;
 d.fovea=float(v[FoveaPercent]/100);d.fovea_shape=int(v[FoveaShape]);d.feather=float(v[FeatherPixels]);d.outer_scale=v[OuterEnabled]?float(v[OuterScalePercent]/100):0;d.outer_pack=v[OuterPack]!=0;
 d.mv=v[MotionVectors]!=0;d.mv_sign=float(v[MotionSign]);d.mv_scale=float(v[MotionScale]);d.depth_value=float(v[ConstantDepth]);d.viewport_ref=float(v[ViewportReferencePercent]/100);
 d.ngx=diagnostic?int(diagnostic):int(v[Backend]);d.app_id=static_cast<unsigned long long>(v[AppId]);d.log_frames=int(v[LogFrames]);return d;
}
template<class T>void OpenGPU(ID3D12Device* dev,const wchar_t* name,ComPtr<T>& output){HANDLE h{};Check(dev->OpenSharedHandleByName(name,GENERIC_ALL,&h),"Open GPU name");Handle close(h);Check(dev->OpenSharedHandle(h,IID_PPV_ARGS(&output)),"Open GPU handle");}
int Run(const wchar_t* base){
 Handle map(OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(std::wstring(base)+L"-map").c_str()));Require(map.h,"IPC map unavailable");
 auto* p=static_cast<Packet*>(MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(Packet)));Require(p,"IPC view unavailable");
 struct Unmap {void* p;~Unmap(){UnmapViewOfFile(p);}} unmap{p};
 Require(p->magic==0x45534e50&&p->abi==Abi,"IPC version mismatch");
 Handle request(OpenEventW(SYNCHRONIZE,FALSE,(std::wstring(base)+L"-request").c_str()));
 Handle response(OpenEventW(EVENT_MODIFY_STATE,FALSE,(std::wstring(base)+L"-response").c_str()));
 Handle stop(OpenEventW(SYNCHRONIZE,FALSE,(std::wstring(base)+L"-stop").c_str()));
 Handle parent(OpenProcess(SYNCHRONIZE,FALSE,p->parentPid));Require(request.h&&response.h&&stop.h&&parent.h,"IPC peer unavailable");
 std::string error;Require(Validate(p->config,error),error.c_str());Require(p->diagnostic==0||p->diagnostic==3||p->diagnostic==4,"Invalid diagnostic mode");
 std::filesystem::create_directories(p->logs);const auto log=std::filesystem::path(p->logs)/(L"NeuralPost-worker-"+std::to_wstring(GetCurrentProcessId())+L".log");
 _wfopen_s(&logFile,log.c_str(),L"wb");BridgeLog("DOOM Neural Post 1.1-dev1; parent=%lu; API/native=D3D12; config=%s",p->parentPid,Encode(p->config).c_str());
 ComPtr<ID3D12Debug> debug;
 if(GetEnvironmentVariableW(L"DOOM_NEURAL_GPU_VALIDATION",nullptr,0) && SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))){debug->EnableDebugLayer();BridgeLog("GPU_VALIDATION_ENABLED=1");}
 ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> adapter;ComPtr<ID3D12Device> dev;ComPtr<ID3D12CommandQueue> queue;
 ComPtr<ID3D12Resource> texture;ComPtr<ID3D12Fence> input,output;uint64_t generation{};Frame last{};bool haveLast=false;
 auto engine=std::make_unique<DirectNR>();
 HANDLE waits[]{request.h,stop.h,parent.h};bool initialized=false,failed=false;
 for(;;){const auto w=WaitForMultipleObjects(3,waits,FALSE,INFINITE);if(w!=WAIT_OBJECT_0)break;
  p->result=0;
  try{
   Require(!failed,"Neural desactivado tras un error; reinicia el juego");
   if(!initialized){
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");Check(factory->EnumAdapterByLuid(p->adapter,IID_PPV_ARGS(&adapter)),"Adapter LUID");Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev)),"Worker device");
    D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;Check(dev->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"Worker queue");
    OpenGPU(dev.Get(),p->inputFence,input);OpenGPU(dev.Get(),p->outputFence,output);
    Require(engine->Init12(dev.Get(),queue.Get(),Translate(p->config,p->diagnostic),p->logs,p->runtime),"Inicializacion Neural fallida; consulta el registro");initialized=true;
   }
   Require(ValidFrame(p->frame),"Identidad o geometria de frame invalida");
   if(generation!=p->generation){Require(engine->Drain(),"GPU anterior no retirada");texture.Reset();OpenGPU(dev.Get(),p->texture,texture);generation=p->generation;haveLast=false;}
   const auto d=texture->GetDesc();Require(d.Width==p->frame.width&&d.Height==p->frame.height&&d.DepthOrArraySize==1&&d.MipLevels==1&&d.SampleDesc.Count==1&&Family(d.Format)!=DXGI_FORMAT_UNKNOWN,"Shared scene mismatch");
   Check(queue->Wait(input.Get(),p->request),"Worker input fence");
   if(!haveLast||ResetRequired(last,p->frame)){engine->RequestReset();++p->status.resets;}
   DirectView views[2]{};for(int i=0;i<2;++i){const auto& e=p->frame.eyes[i];auto& v=views[i];v.texture12=texture.Get();v.left=e.left;v.top=e.top;v.right=e.right;v.bottom=e.bottom;std::copy_n(e.orientation,4,v.q);v.fovL=e.fov[0];v.fovR=e.fov[1];v.fovU=e.fov[2];v.fovD=e.fov[3];}
   Require(engine->Process(views,2),"Evaluacion Neural fallida; imagen original conservada");
   Check(queue->Signal(output.Get(),p->request),"Worker output fence");
   last=p->frame;haveLast=true;p->result=1;++p->status.applied;p->status.cpuMs=engine->avgCpuMs();p->status.gpuMs=engine->avgGpuMs();p->status.modelMs=engine->avgModelMs();Message(p->status,Applied,"Neural aplicado a ambos ojos");
  }catch(const std::exception& e){failed=true;Message(p->status,Fault,e.what());BridgeLog("FAILED: %s",e.what());}
  p->response=p->request;MemoryBarrier();SetEvent(response);
 }
 // Never release resources while the GPU may still reference them. Process exit
 // performs OS cleanup if a lost device cannot retire; no game NGX context exists here.
 const bool drained=engine->Drain();
 if(debug && dev){ComPtr<ID3D12InfoQueue> info;if(SUCCEEDED(dev.As(&info))){uint64_t errors{};
  for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size{};info->GetMessage(i,nullptr,&size);std::vector<uint8_t> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());if(SUCCEEDED(info->GetMessage(i,message,&size))&&(message->Severity==D3D12_MESSAGE_SEVERITY_ERROR||message->Severity==D3D12_MESSAGE_SEVERITY_CORRUPTION)){++errors;BridgeLog("GPU_VALIDATION_ERROR: %s",message->pDescription);}}
  BridgeLog("GPU_VALIDATION_ERRORS=%llu",errors);
 }}
 if(drained)engine->Shutdown(false);else engine.release();
 if(logFile){fclose(logFile);logFile=nullptr;}return 0;
}
}
int wmain(int argc,wchar_t** argv){try{if(argc!=3||wcscmp(argv[1],L"--ipc")||wcsncmp(argv[2],L"Local\\ES2NP-",12))return 2;return es2np::Run(argv[2]);}catch(...){return 3;}}
