#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace external_overlay {
// Reserved bytes after the event ring, before pixels. Surface v1 stays intact.
constexpr uint32_t InformationOffset=3136;
struct XrInformation {
 uint32_t version=1,sessionActive{},vendor{},maxLayers{},backend{},input{};
 uint64_t tick{},runtimeVersion{};
 char system[256]{},runtime[128]{},profiles[2][192]{};
 int32_t bindings[6]{-1,-1,-1,-1,-1,-1};
};
static_assert(sizeof(XrInformation)==832);
static_assert(offsetof(XrInformation,tick)==24);
static_assert(offsetof(XrInformation,system)==40);
static_assert(offsetof(XrInformation,profiles)==424);
static_assert(offsetof(XrInformation,bindings)==808);
static_assert(InformationOffset+sizeof(XrInformation)<=4096);
template<size_t N>void InfoText(char (&out)[N],const char* in){
 const size_t n=strnlen_s(in,N-1);memcpy(out,in,n);out[n]=0;
}
}
