#pragma once
#include <stdint.h>
#include <d3d11.h>
#include <d3d12.h>

// Versioned optional module ABI. No UEVR, Unreal or private Full types cross it.
namespace es2np {
inline constexpr uint32_t Abi = 1, OptionCount = 26;
struct Config { uint32_t size = sizeof(Config), abi = Abi; double values[OptionCount]{}; };
struct Eye {
    uint32_t left{}, top{}, right{}, bottom{};
    float orientation[4]{0,0,0,1};
    float fov[4]{}; // left, right, up, down; radians of this exact rectangle
};
struct Frame {
    uint32_t size = sizeof(Frame), abi = Abi, width{}, height{};
    uint64_t epoch{}, source{}, timeNs{}, generation{};
    Eye eyes[2]{};
};
struct Status {
    uint32_t size = sizeof(Status), abi = Abi, state{}, graphicsApi{};
    uint64_t applied{}, repeated{}, resets{};
    double gpuMs{}, modelMs{}, cpuMs{};
    char message[384]{};
};
enum State : uint32_t { Disabled, Ready, Applied, Suspended, Fault };
using Create = void* (__cdecl*)(const Config*, const wchar_t*, const wchar_t*, uint32_t);
using Process11 = bool (__cdecl*)(void*, ID3D11Device*, ID3D11Texture2D*, DXGI_FORMAT, const Frame*, ID3D11Texture2D**);
using Process12 = bool (__cdecl*)(void*, ID3D12Device*, ID3D12CommandQueue*, ID3D12Resource*, D3D12_RESOURCE_STATES, DXGI_FORMAT, const Frame*, ID3D12Resource**);
using Suspend = void (__cdecl*)(void*);
using ReadStatus = bool (__cdecl*)(void*, Status*);
using Destroy = void (__cdecl*)(void*);
// Successful processing returns an AddRef'd private output. D3D12 output is
// COMMON, produced on exactly the supplied DIRECT queue. Keep it alive until
// the consumer's GPU fence completes. Originals are never written by the module.
}
