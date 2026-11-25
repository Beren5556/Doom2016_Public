#pragma once
#include "NeuralPostSettings.h"
#include "NeuralPostFrame.h"
#include <wrl/client.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <string>
#include <stdexcept>
#include <vector>
namespace es2np {
using Microsoft::WRL::ComPtr;
struct Packet {
 uint32_t magic=0x45534e50,abi=Abi,parentPid{},command{},result{},diagnostic{};
 LUID adapter{};uint64_t request{},response{},generation{};
 Config config;Frame frame;Status status;
 wchar_t texture[192]{},inputFence[192]{},outputFence[192]{},runtime[1024]{},logs[1024]{};
};
inline void Check(HRESULT hr,const char* operation){if(FAILED(hr))throw std::runtime_error(std::string(operation)+" HRESULT="+std::to_string(uint32_t(hr)));}
inline void Require(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
template<size_t N>void Put(wchar_t(&to)[N],const std::wstring& value){Require(value.size()<N,"Ruta o nombre demasiado largo");wcscpy_s(to,value.c_str());}
inline void Message(Status& s,uint32_t state,const char* text){s.state=state;strncpy_s(s.message,text,_TRUNCATE);}
inline DXGI_FORMAT Family(DXGI_FORMAT f){switch(f){
 case DXGI_FORMAT_B8G8R8A8_TYPELESS:case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:return DXGI_FORMAT_B8G8R8A8_UNORM;
 case DXGI_FORMAT_R8G8B8A8_TYPELESS:case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:return DXGI_FORMAT_R8G8B8A8_UNORM;
 default:return DXGI_FORMAT_UNKNOWN;}}
inline void Barrier(ID3D12GraphicsCommandList* cl,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){if(a==b)return;D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};cl->ResourceBarrier(1,&x);}
struct Handle {HANDLE h{};Handle()=default;explicit Handle(HANDLE v):h(v){};~Handle(){if(h)CloseHandle(h);}Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;operator HANDLE()const{return h;}void reset(HANDLE v=nullptr){if(h)CloseHandle(h);h=v;}};
}
