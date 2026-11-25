#include "WorkerProtocol.h"
#include <objbase.h>
#include <mutex>
#include <memory>
#include <array>

namespace es2np {
struct Context {
 std::mutex mutex;Config config;Status status;Frame last{};bool haveLast{},suspended=true;uint32_t diagnostic{},api{};
 std::filesystem::path runtime,logs;std::wstring base;Packet* packet{};
 Handle map,request,response,stop,process,textureHandle,inputHandle,outputHandle,event;
 uint64_t sequence{},generation{},fenceNext{},fenceValues[3]{};UINT slot{};
 ComPtr<ID3D11Device> d11;ComPtr<ID3D11Device5> d115;ComPtr<ID3D11DeviceContext4> c11;
 ComPtr<ID3D11Texture2D> shared11,output11;ComPtr<ID3D11Fence> in11,out11;
 ComPtr<ID3D12Device> d12;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> in12,out12,retirement;
 ComPtr<ID3D12Resource> shared12,output12;ComPtr<ID3D12CommandAllocator> alloc[2][3];ComPtr<ID3D12GraphicsCommandList> list[2];
 ComPtr<ID3D12Resource> retained[3];uint32_t width{},height{};DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 Context(const Config& c,const wchar_t* r,const wchar_t* l,uint32_t test):config(c),diagnostic(test),runtime(r),logs(l){Message(status,c.values[Enabled]?Ready:Disabled,c.values[Enabled]?"Esperando una escena estereo valida":"Desactivado en esta sesion");}
 ~Context(){if(stop.h)SetEvent(stop);if(process.h)WaitForSingleObject(process,250);if(packet)UnmapViewOfFile(packet);}
 void Wait(ID3D12Fence* fence,uint64_t value){if(!value)return;const auto done=fence->GetCompletedValue();Require(done!=UINT64_MAX,"Dispositivo GPU perdido");if(done>=value)return;Check(fence->SetEventOnCompletion(value,event),"Fence event");Require(WaitForSingleObject(event,2000)==WAIT_OBJECT_0&&fence->GetCompletedValue()!=UINT64_MAX&&fence->GetCompletedValue()>=value,"Tiempo de espera GPU agotado");}
 void InitIPC(LUID adapter){
  GUID g{};Check(CoCreateGuid(&g),"IPC identity");wchar_t guid[64]{};StringFromGUID2(g,guid,64);base=L"Local\\ES2NP-"+std::to_wstring(GetCurrentProcessId())+L"-"+guid;
  map.reset(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Packet),(base+L"-map").c_str()));Require(map.h&&GetLastError()!=ERROR_ALREADY_EXISTS,"IPC map creation failed");
  packet=static_cast<Packet*>(MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(Packet)));Require(packet,"IPC map view failed");new(packet)Packet;
  packet->config=config;packet->adapter=adapter;packet->parentPid=GetCurrentProcessId();packet->diagnostic=diagnostic;
  Put(packet->runtime,runtime.wstring());Put(packet->logs,logs.wstring());Put(packet->inputFence,base+L"-input");Put(packet->outputFence,base+L"-output");
  request.reset(CreateEventW(nullptr,FALSE,FALSE,(base+L"-request").c_str()));response.reset(CreateEventW(nullptr,FALSE,FALSE,(base+L"-response").c_str()));stop.reset(CreateEventW(nullptr,TRUE,FALSE,(base+L"-stop").c_str()));event.reset(CreateEventW(nullptr,FALSE,FALSE,nullptr));Require(request.h&&response.h&&stop.h&&event.h,"IPC events creation failed");
 }
 void Start(){
  const auto exe=runtime.parent_path()/L"DOOMNeuralWorker.exe";Require(std::filesystem::is_regular_file(exe),"Falta DOOMNeuralWorker.exe");
  const auto text=L"\""+exe.wstring()+L"\" --ipc \""+base+L"\"";std::vector<wchar_t> command(text.begin(),text.end());command.push_back(0);
  STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION info{};
  Require(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,runtime.parent_path().c_str(),&startup,&info)!=0,"No se puede iniciar el auxiliar Neural");process.reset(info.hProcess);CloseHandle(info.hThread);
 }
 void Init11(ID3D11Device* device){
  d11=device;Check(d11.As(&d115),"D3D11.4 device");ComPtr<ID3D11DeviceContext> ctx;d11->GetImmediateContext(&ctx);Check(ctx.As(&c11),"D3D11.4 context");
  ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC desc{};Check(d11.As(&dxgi),"DXGI device");Check(dxgi->GetAdapter(&adapter),"D3D11 adapter");Check(adapter->GetDesc(&desc),"Adapter description");InitIPC(desc.AdapterLuid);
  Check(d115->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&in11)),"Input fence");Check(d115->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&out11)),"Output fence");
  Check(in11->CreateSharedHandle(nullptr,GENERIC_ALL,packet->inputFence,&inputHandle.h),"Input fence share");Check(out11->CreateSharedHandle(nullptr,GENERIC_ALL,packet->outputFence,&outputHandle.h),"Output fence share");api=status.graphicsApi=11;Start();
 }
 void Init12(ID3D12Device* device,ID3D12CommandQueue* q){
  Require(q->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT,"La cola Neural debe ser DIRECT");ComPtr<ID3D12Device> owner;Check(q->GetDevice(IID_PPV_ARGS(&owner)),"Queue owner");Require(owner.Get()==device,"Dispositivo y cola incompatibles");d12=device;queue=q;InitIPC(d12->GetAdapterLuid());
  Check(d12->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&in12)),"Input fence");Check(d12->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&out12)),"Output fence");Check(d12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&retirement)),"Retirement fence");
  Check(d12->CreateSharedHandle(in12.Get(),nullptr,GENERIC_ALL,packet->inputFence,&inputHandle.h),"Input fence share");Check(d12->CreateSharedHandle(out12.Get(),nullptr,GENERIC_ALL,packet->outputFence,&outputHandle.h),"Output fence share");
  for(int side=0;side<2;++side){for(int i=0;i<3;++i)Check(d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc[side][i])),"Transport allocator");Check(d12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc[side][0].Get(),nullptr,IID_PPV_ARGS(&list[side])),"Transport list");Check(list[side]->Close(),"Initial list close");}
  api=status.graphicsApi=12;Start();
 }
 bool Accept(const Frame& f){if(!config.values[Enabled]||status.state==Fault)return false;if(!ValidFrame(f)){Message(status,Suspended,"Esperando identidad, pose y recortes estereo coherentes");suspended=true;haveLast=false;return false;}return true;}
 void Sized(uint32_t w,uint32_t h,DXGI_FORMAT fmt){
  if(width==w&&height==h&&format==fmt)return;
  if(api==12&&width){const auto v=++fenceNext;Check(queue->Signal(retirement.Get(),v),"Resize fence");Wait(retirement.Get(),v);}
  shared11.Reset();output11.Reset();shared12.Reset();output12.Reset();textureHandle.reset();
  Put(packet->texture,base+L"-texture-"+std::to_wstring(++generation));
  if(api==11){D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.ArraySize=d.MipLevels=1;d.Format=fmt;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;d.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
   Check(d11->CreateTexture2D(&d,nullptr,&shared11),"Shared scene D3D11");ComPtr<IDXGIResource1> res;Check(shared11.As(&res),"Shared scene interface");Check(res->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,packet->texture,&textureHandle.h),"Share scene D3D11");d.MiscFlags=0;Check(d11->CreateTexture2D(&d,nullptr,&output11),"Private result D3D11");
  }else{D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=1;d.Format=fmt;d.SampleDesc.Count=1;
   Check(d12->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_SHARED,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&shared12)),"Shared scene D3D12");Check(d12->CreateSharedHandle(shared12.Get(),nullptr,GENERIC_ALL,packet->texture,&textureHandle.h),"Share scene D3D12");Check(d12->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&output12)),"Private result D3D12");
  }
  width=w;height=h;format=fmt;haveLast=false;
 }
 void Evaluate(const Frame& f){
  packet->frame=f;if(suspended)packet->frame.epoch^=(uint64_t(++resumeEpoch)<<32);else packet->frame.epoch^=(uint64_t(resumeEpoch)<<32);
  packet->generation=generation;packet->request=sequence;packet->response=0;packet->result=0;MemoryBarrier();Require(SetEvent(request)!=0,"IPC request failed");
  HANDLE waits[]{response.h,process.h};Require(WaitForMultipleObjects(2,waits,FALSE,sequence==1?15000:2000)==WAIT_OBJECT_0,"El auxiliar Neural no respondio; original conservado");MemoryBarrier();Require(packet->response==sequence,"Respuesta Neural de otro fotograma");
  Require(packet->result==1,packet->status.message);const auto repeated=status.repeated;status=packet->status;status.graphicsApi=api;status.repeated=repeated;
  last=f;haveLast=true;suspended=false;
 }
 uint32_t resumeEpoch{};
 void Copy12(ID3D12Resource* source,D3D12_RESOURCE_STATES sourceState,bool after){
  const int side=after?1:0;
  if(!after){slot=(slot+1)%3;Wait(retirement.Get(),fenceValues[slot]);retained[slot]=source;}
  Check(alloc[side][slot]->Reset(),"Copy allocator reset");Check(list[side]->Reset(alloc[side][slot].Get(),nullptr),"Copy list reset");auto* cl=list[side].Get();
  ID3D12Resource* from=after?shared12.Get():source;ID3D12Resource* to=after?output12.Get():shared12.Get();auto state=after?D3D12_RESOURCE_STATE_COMMON:sourceState;
  Barrier(cl,from,state,D3D12_RESOURCE_STATE_COPY_SOURCE);Barrier(cl,to,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);cl->CopyResource(to,from);Barrier(cl,to,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);Barrier(cl,from,D3D12_RESOURCE_STATE_COPY_SOURCE,state);Check(cl->Close(),"Copy close");
  if(after)Check(queue->Wait(out12.Get(),sequence),"Neural output wait");ID3D12CommandList* lists[]{cl};queue->ExecuteCommandLists(1,lists);
  if(after){fenceValues[slot]=++fenceNext;Check(queue->Signal(retirement.Get(),fenceNext),"Copy retirement");}else Check(queue->Signal(in12.Get(),sequence),"Neural input signal");
 }
 void Fail(const std::exception& e){haveLast=false;Message(status,Fault,e.what());if(stop.h)SetEvent(stop);}
};
}
#define EXPORT extern "C" __declspec(dllexport)
EXPORT void* __cdecl ES2NeuralPost_Create(const es2np::Config* config,const wchar_t* runtime,const wchar_t* logs,uint32_t diagnostic){try{std::string error;if(!config||!runtime||!logs||!es2np::Validate(*config,error)||(diagnostic&&diagnostic!=3&&diagnostic!=4))return nullptr;return new es2np::Context(*config,runtime,logs,diagnostic);}catch(...){return nullptr;}}
EXPORT bool __cdecl ES2NeuralPost_Process11(void* handle,ID3D11Device* dev,ID3D11Texture2D* source,DXGI_FORMAT declared,const es2np::Frame* frame,ID3D11Texture2D** out){
 if(out)*out=nullptr;if(!handle||!dev||!source||!frame||!out)return false;auto& c=*static_cast<es2np::Context*>(handle);std::lock_guard lock(c.mutex);
 try{if(!c.Accept(*frame))return false;if(!c.api)c.Init11(dev);es2np::Require(c.api==11&&c.d11.Get()==dev,"Cambio de dispositivo; reinicia Neural");
  D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);es2np::ComPtr<ID3D11Device> owner;source->GetDevice(&owner);auto fmt=es2np::Family(declared);
  es2np::Require(owner.Get()==dev&&fmt!=DXGI_FORMAT_UNKNOWN&&fmt==es2np::Family(d.Format)&&d.Width==frame->width&&d.Height==frame->height&&d.ArraySize==1&&d.MipLevels==1&&d.SampleDesc.Count==1,"Escena D3D11 incompatible");
  c.Sized(d.Width,d.Height,fmt);if(c.haveLast&&es2np::SameFrame(c.last,*frame)){++c.status.repeated;c.output11.CopyTo(out);return true;}
  ++c.sequence;c.c11->CopyResource(c.shared11.Get(),source);es2np::Check(c.c11->Signal(c.in11.Get(),c.sequence),"Neural input signal");c.c11->Flush();c.Evaluate(*frame);
  es2np::Check(c.c11->Wait(c.out11.Get(),c.sequence),"Neural output wait");c.c11->CopyResource(c.output11.Get(),c.shared11.Get());c.c11->Flush();c.output11.CopyTo(out);return true;
 }catch(const std::exception& e){c.Fail(e);return false;}catch(...){es2np::Message(c.status,es2np::Fault,"Error inesperado Neural");return false;}
}
EXPORT bool __cdecl ES2NeuralPost_Process12(void* handle,ID3D12Device* dev,ID3D12CommandQueue* queue,ID3D12Resource* source,D3D12_RESOURCE_STATES state,DXGI_FORMAT declared,const es2np::Frame* frame,ID3D12Resource** out){
 if(out)*out=nullptr;if(!handle||!dev||!queue||!source||!frame||!out)return false;auto& c=*static_cast<es2np::Context*>(handle);std::lock_guard lock(c.mutex);
 try{if(!c.Accept(*frame))return false;if(!c.api)c.Init12(dev,queue);es2np::Require(c.api==12&&c.d12.Get()==dev&&c.queue.Get()==queue,"Cambio de dispositivo o cola; reinicia Neural");
  const auto d=source->GetDesc();es2np::ComPtr<ID3D12Device> owner;es2np::Check(source->GetDevice(IID_PPV_ARGS(&owner)),"Scene owner");const auto fmt=es2np::Family(declared);
  es2np::Require(owner.Get()==dev&&fmt!=DXGI_FORMAT_UNKNOWN&&fmt==es2np::Family(d.Format)&&d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==frame->width&&d.Height==frame->height&&d.DepthOrArraySize==1&&d.MipLevels==1&&d.SampleDesc.Count==1,"Escena D3D12 incompatible");
  c.Sized(UINT(d.Width),d.Height,fmt);if(c.haveLast&&es2np::SameFrame(c.last,*frame)){++c.status.repeated;c.output12.CopyTo(out);return true;}
  ++c.sequence;c.Copy12(source,state,false);c.Evaluate(*frame);c.Copy12(nullptr,D3D12_RESOURCE_STATE_COMMON,true);c.output12.CopyTo(out);return true;
 }catch(const std::exception& e){c.Fail(e);return false;}catch(...){es2np::Message(c.status,es2np::Fault,"Error inesperado Neural");return false;}
}
EXPORT void __cdecl ES2NeuralPost_Suspend(void* handle){if(!handle)return;auto& c=*static_cast<es2np::Context*>(handle);std::lock_guard lock(c.mutex);c.haveLast=false;c.suspended=true;if(c.status.state!=es2np::Fault&&c.config.values[es2np::Enabled])es2np::Message(c.status,es2np::Suspended,"Suspendido durante menu o cambio de escena");}
EXPORT bool __cdecl ES2NeuralPost_Status(void* handle,es2np::Status* out){if(!handle||!out||out->size!=sizeof(*out)||out->abi!=es2np::Abi)return false;auto& c=*static_cast<es2np::Context*>(handle);std::lock_guard lock(c.mutex);*out=c.status;return true;}
EXPORT void __cdecl ES2NeuralPost_Destroy(void* handle){if(!handle)return;auto* c=static_cast<es2np::Context*>(handle);try{
 if(c->queue&&c->retirement){const auto v=++c->fenceNext;es2np::Check(c->queue->Signal(c->retirement.Get(),v),"Destroy fence");c->Wait(c->retirement.Get(),v);}delete c;
 }catch(...){if(c->stop.h)SetEvent(c->stop);/* GPU ownership cannot be proved: bounded retention until process exit. */}}
