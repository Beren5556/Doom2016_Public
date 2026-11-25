#include "WorkerProtocol.h"
#include <iostream>
#include <d3d12sdklayers.h>
using namespace es2np;
struct GPU {
 int api;UINT w{},h{};D3D12_RESOURCE_STATES sourceState=D3D12_RESOURCE_STATE_COMMON;ComPtr<ID3D11Device>d11;ComPtr<ID3D11DeviceContext>c11;ComPtr<ID3D11Texture2D>in11;
 ComPtr<ID3D12Device>d12;ComPtr<ID3D12CommandQueue>q;ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;ComPtr<ID3D12Fence>f;ComPtr<ID3D12Resource>in12;Handle event;uint64_t value{};
 GPU(int which):api(which){
  if(api==12){ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"parent debug layer");debug->EnableDebugLayer();}
  if(api==11)Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d11,nullptr,&c11),"D3D11 device");
  else{Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d12)),"D3D12 device");D3D12_COMMAND_QUEUE_DESC d{};Check(d12->CreateCommandQueue(&d,IID_PPV_ARGS(&q)),"queue");Check(d12->CreateCommandAllocator(d.Type,IID_PPV_ARGS(&a)),"allocator");Check(d12->CreateCommandList(0,d.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)),"list");Check(l->Close(),"close");Check(d12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)),"fence");event.reset(CreateEventW(nullptr,FALSE,FALSE,nullptr));}
 }
 void Begin(){Check(a->Reset(),"allocator reset");Check(l->Reset(a.Get(),nullptr),"list reset");}
 void End(){Check(l->Close(),"close");ID3D12CommandList* lists[]{l.Get()};q->ExecuteCommandLists(1,lists);Check(q->Signal(f.Get(),++value),"signal");Check(f->SetEventOnCompletion(value,event),"event");Require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"GPU timeout");}
 ComPtr<ID3D12Resource> Buffer(uint64_t n,D3D12_HEAP_TYPE t){D3D12_HEAP_PROPERTIES hp{};hp.Type=t;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=n;d.Height=d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource>b;Check(d12->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,t==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&b)),"buffer");return b;}
 void Upload(const std::vector<uint8_t>& data,UINT width,UINT height){w=width;h=height;
  if(api==11){in11.Reset();D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.ArraySize=d.MipLevels=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;D3D11_SUBRESOURCE_DATA s{data.data(),w*4,0};Check(d11->CreateTexture2D(&d,&s,&in11),"input11");return;}
  in12.Reset();D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;Check(d12->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&in12)),"input12");
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes{};d12->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto b=Buffer(bytes,D3D12_HEAP_TYPE_UPLOAD);void*p{};D3D12_RANGE none{};Check(b->Map(0,&none,&p),"upload map");for(UINT y=0;y<h;++y)memcpy(static_cast<uint8_t*>(p)+y*fp.Footprint.RowPitch,data.data()+y*w*4,w*4);b->Unmap(0,nullptr);
  Begin();Barrier(l.Get(),in12.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=in12.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.pResource=b.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(l.Get(),in12.Get(),D3D12_RESOURCE_STATE_COPY_DEST,sourceState);End();
 }
 std::vector<uint8_t> Read(ID3D11Texture2D*t11,ID3D12Resource*t12){std::vector<uint8_t>data(w*h*4);
  if(api==11){D3D11_TEXTURE2D_DESC d{};t11->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.BindFlags=d.MiscFlags=0;ComPtr<ID3D11Texture2D>r;Check(d11->CreateTexture2D(&d,nullptr,&r),"staging");c11->CopyResource(r.Get(),t11);D3D11_MAPPED_SUBRESOURCE m{};Check(c11->Map(r.Get(),0,D3D11_MAP_READ,0,&m),"read11");for(UINT y=0;y<h;++y)memcpy(data.data()+y*w*4,static_cast<uint8_t*>(m.pData)+y*m.RowPitch,w*4);c11->Unmap(r.Get(),0);return data;}
  const auto state=t12==in12.Get()?sourceState:D3D12_RESOURCE_STATE_COMMON;
  const auto d=t12->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes{};d12->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto b=Buffer(bytes,D3D12_HEAP_TYPE_READBACK);Begin();Barrier(l.Get(),t12,state,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION dst{},src{};src.pResource=t12;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=b.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(l.Get(),t12,D3D12_RESOURCE_STATE_COPY_SOURCE,state);End();void*p{};D3D12_RANGE range{0,SIZE_T(bytes)};Check(b->Map(0,&range,&p),"read12");for(UINT y=0;y<h;++y)memcpy(data.data()+y*w*4,static_cast<uint8_t*>(p)+y*fp.Footprint.RowPitch,w*4);D3D12_RANGE none{};b->Unmap(0,&none);return data;
 }
 void Validate(){if(api!=12)return;ComPtr<ID3D12InfoQueue> info;Check(d12.As(&info),"parent debug queue");for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size{};info->GetMessage(i,nullptr,&size);std::vector<uint8_t> bytes(size);auto*m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Check(info->GetMessage(i,m,&size),"parent debug message");if(m->Severity==D3D12_MESSAGE_SEVERITY_ERROR||m->Severity==D3D12_MESSAGE_SEVERITY_CORRUPTION)throw std::runtime_error(m->pDescription);}}
};
int wmain(int argc,wchar_t**argv){try{
 Require(argc>=4,"usage: NeuralGpuTests API diagnostic output-folder [variant]");const int api=_wtoi(argv[1]),diagnostic=_wtoi(argv[2]),variant=argc>4?_wtoi(argv[4]):0;Require(api==11||api==12,"API");
 wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto dir=std::filesystem::path(exe).parent_path();auto dll=LoadLibraryW((dir/L"DOOMNeuralPost.dll").c_str());Require(dll,"module missing");
 auto create=(Create)GetProcAddress(dll,"ES2NeuralPost_Create");auto p11=(Process11)GetProcAddress(dll,"ES2NeuralPost_Process11");auto p12=(Process12)GetProcAddress(dll,"ES2NeuralPost_Process12");auto status=(ReadStatus)GetProcAddress(dll,"ES2NeuralPost_Status");auto suspend=(Suspend)GetProcAddress(dll,"ES2NeuralPost_Suspend");auto destroy=(Destroy)GetProcAddress(dll,"ES2NeuralPost_Destroy");Require(create&&p11&&p12&&status&&suspend&&destroy,"ABI");
 auto config=Defaults();config.values[Enabled]=1;config.values[ScalePercent]=60;config.values[IntensityPercent]=90;config.values[Style]=2;
 if(variant==1){config.values[Residual]=0;config.values[ScalePercent]=100;}if(variant==2){config.values[FoveaPercent]=60;config.values[OuterEnabled]=1;config.values[Passes]=2;config.values[FeatherPixels]=24;}
 if(variant==6)config.values[Passes]=4;
 if(variant==7){config.values[Passes]=4;config.values[FoveaPercent]=60;config.values[OuterEnabled]=1;config.values[OuterPack]=0;config.values[FeatherPixels]=24;}
 if(variant==4)config.values[Enabled]=0;
 auto log=std::filesystem::absolute(argv[3]);void*context=create(&config,(dir/(variant==3?L"AbsentNeuralRuntime":L"NeuralRuntime")).c_str(),log.c_str(),diagnostic);Require(context,"create");GPU gpu(api);uint64_t checked{},changed{};Status final{};
 if(variant==9)gpu.sourceState=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
 for(int step=0;step<5;++step){const UINT w=step==4?768:(variant==5?8192:1024),h=step==4?384:(variant==5?4096:512);std::vector<uint8_t>pattern(w*h*4);
  for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){auto at=(y*w+x)*4;pattern[at]=uint8_t(35+x%80+step);pattern[at+1]=uint8_t(40+y%110);pattern[at+2]=x<w/2?220:55;pattern[at+3]=uint8_t(60+(x+y)%170);}
  gpu.Upload(pattern,w,h);Frame f;f.width=w;f.height=h;f.source=step+1;f.epoch=1;f.generation=step==4?2:1;f.timeNs=uint64_t(step+1)*11000000;f.eyes[0]={16,8,w/2-16,h-8,{0,0,0,1},{-.8f,.8f,.8f,-.8f}};f.eyes[1]=f.eyes[0];f.eyes[1].left=w/2+16;f.eyes[1].right=w-16;
  if(variant==8){f.eyes[1].right-=1;f.eyes[1].bottom-=3;}
  if(step==2)suspend(context);if(step==3){auto bad=f;bad.eyes[0].fov[0]=NAN;ID3D11Texture2D*a{};ID3D12Resource*b{};const bool ok=api==11?p11(context,gpu.d11.Get(),gpu.in11.Get(),DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,&bad,&a):p12(context,gpu.d12.Get(),gpu.q.Get(),gpu.in12.Get(),D3D12_RESOURCE_STATE_COMMON,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,&bad,&b);Require(!ok&&!a&&!b,"invalid frame accepted");}
  ComPtr<ID3D11Texture2D>out11;ComPtr<ID3D12Resource>out12;
  auto run=[&]{out11.Reset();out12.Reset();return api==11?p11(context,gpu.d11.Get(),gpu.in11.Get(),DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,&f,&out11):p12(context,gpu.d12.Get(),gpu.q.Get(),gpu.in12.Get(),gpu.sourceState,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,&f,&out12);};
  const bool ok=run();status(context,&final);
  if(variant==3||variant==4){Require(!ok&&!out11&&!out12,"negative case returned output");Require(final.state==(variant==3?Fault:Disabled),"negative state mismatch");Require(!run(),"negative case retried");Require(gpu.Read(gpu.in11.Get(),gpu.in12.Get())==pattern,"negative original changed");destroy(context);std::cout<<"{\"pass\":true,\"api\":"<<api<<",\"variant\":"<<variant<<",\"originalPreserved\":true,\"state\":"<<final.state<<"}\n";return 0;}
  if(!ok)std::cerr<<final.message<<'\n';Require(ok,"process");auto pixels=gpu.Read(out11.Get(),out12.Get());Require(gpu.Read(gpu.in11.Get(),gpu.in12.Get())==pattern,"original changed");
  for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){auto at=(y*w+x)*4;bool inside=false;for(auto&e:f.eyes)inside|=x>=e.left&&x<e.right&&y>=e.top&&y<e.bottom;Require(pixels[at+3]==pattern[at+3],"alpha changed");for(int k=0;k<3;++k){if(!inside)Require(pixels[at+k]==pattern[at+k],"outside crop changed");if(diagnostic==3&&variant!=1)Require(pixels[at+k]==pattern[at+k],"identity changed");if(inside&&pixels[at+k]!=pattern[at+k])++changed;}++checked;}
  const auto applied=final.applied;Require(run(),"repeat");status(context,&final);Require(final.applied==applied&&final.repeated==uint64_t(step+1),"repeat evaluated");
 }
 Require(diagnostic==3||changed>100,"no measured change");Require(final.resets>=4,"history resets missing");destroy(context);gpu.Validate();
 std::cout<<"{\"pass\":true,\"api\":"<<api<<",\"diagnostic\":"<<diagnostic<<",\"variant\":"<<variant<<",\"pixelsChecked\":"<<checked<<",\"changedChannels\":"<<changed<<",\"applied\":"<<final.applied<<",\"repeated\":"<<final.repeated<<",\"resets\":"<<final.resets<<",\"gpuMs\":"<<final.gpuMs<<",\"modelMs\":"<<final.modelMs<<"}\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

