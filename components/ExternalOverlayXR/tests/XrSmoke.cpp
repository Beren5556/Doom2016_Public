// The exact built layer with a synthetic OpenXR chain and real WARP device.
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#define XR_USE_GRAPHICS_API_VULKAN
#include "VulkanFixture.h"
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <iostream>
#include <map>
#include <string>
#include "../native/Surface.h"
#include "../native/HoldGesture.h"
#include "../native/XrInformation.h"
#include "../native/XrResolution.h"

using Microsoft::WRL::ComPtr;using namespace external_overlay;
namespace {
std::map<std::string,XrPath> paths;
std::string selected="/interaction_profiles/oculus/touch_controller";
bool extensionsAvailable=true,api11=false,picoEnabled=false,plusEnabled=false,focused=true,contact=true,wheel=false,rejectOverlay=false;
XrSession eventSession{};XrStructureType pendingEvent=XR_TYPE_UNKNOWN;
HMODULE layerLibrary{};VulkanFixture vk;bool useVk{},useVk2{};
bool aimActive=true;bool d12{},down{},gestureDown{},timeout{},attached{},quiet{};int acquires{},releases{},layerCount{},syncCount{},bindingCount{},destroyed{};uintptr_t handle=10;float aimX=CenterX,cursorX{};
XrAction aim{},trigger{},scroll{},gestureTrigger{},gestureGrip{};XrSpace view{};const XrCompositionLayerBaseHeader* original{};
ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;ComPtr<ID3D11Texture2D> texture;
ComPtr<ID3D12Device> dev12;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Resource> texture12;
template<class T>T Handle(){return reinterpret_cast<T>(++handle);}
void need(bool yes,const char* name){if(!yes)throw std::runtime_error(name);if(!quiet)std::cout<<"PASS "<<name<<'\n';}
XrResult XRAPI_CALL create(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* next,XrInstance* out){need(!next->nextInfo,"layer chain advanced exactly once");
 need(info->applicationInfo.apiVersion==(api11?XR_MAKE_VERSION(1,1,0):XR_MAKE_VERSION(1,0,22)),"application API version preserved");
 for(uint32_t n=0;n<info->enabledExtensionCount;++n){picoEnabled|=!strcmp(info->enabledExtensionNames[n],"XR_BD_controller_interaction");plusEnabled|=!strcmp(info->enabledExtensionNames[n],"XR_META_touch_controller_plus");}
 need(picoEnabled==(extensionsAvailable&&!api11)&&plusEnabled==extensionsAvailable,"optional extensions negotiated without changing runtime");*out=Handle<XrInstance>();return XR_SUCCESS;}
XrResult XRAPI_CALL createSession(XrInstance,const XrSessionCreateInfo*,XrSession* out){*out=Handle<XrSession>();return XR_SUCCESS;}
XrResult XRAPI_CALL beginSession(XrSession,const XrSessionBeginInfo*){return XR_SUCCESS;}
XrResult XRAPI_CALL viewConfigs(XrInstance,XrSystemId,XrViewConfigurationType,uint32_t cap,uint32_t* count,XrViewConfigurationView* out){*count=2;if(cap)for(uint32_t n=0;n<2;++n){out[n].recommendedImageRectWidth=3000;out[n].recommendedImageRectHeight=3200;out[n].maxImageRectWidth=out[n].maxImageRectHeight=4096;}return XR_SUCCESS;}
XrResult XRAPI_CALL createSet(XrInstance,const XrActionSetCreateInfo* info,XrActionSet* out){need(info->priority==1000||info->priority==0,"interactive priority or shared gesture priority");*out=Handle<XrActionSet>();return XR_SUCCESS;}
XrResult XRAPI_CALL createAction(XrActionSet,const XrActionCreateInfo* info,XrAction* out){*out=Handle<XrAction>();if(!strcmp(info->actionName,"aim"))aim=*out;if(!strcmp(info->actionName,"click"))trigger=*out;if(!strcmp(info->actionName,"scroll"))scroll=*out;if(!strcmp(info->actionName,"gesture_trigger"))gestureTrigger=*out;if(!strcmp(info->actionName,"gesture_grip"))gestureGrip=*out;return XR_SUCCESS;}
XrResult XRAPI_CALL path(XrInstance,const char* text,XrPath* out){auto [it,inserted]=paths.emplace(text,paths.size()+1);(void)inserted;*out=it->second;return XR_SUCCESS;}
XrResult XRAPI_CALL suggest(XrInstance,const XrInteractionProfileSuggestedBinding* info){bindingCount=static_cast<int>(info->countSuggestedBindings);
 XrPath pico{},plus{};path({},"/interaction_profiles/bytedance/pico4_controller",&pico);path({},"/interaction_profiles/meta/touch_controller_plus",&plus);
 need(info->interactionProfile!=pico||picoEnabled||api11,"Pico profile requires enabled extension or core 1.1");need(info->interactionProfile!=plus||plusEnabled,"Plus profile requires extension");
 if(rejectOverlay&&info->countSuggestedBindings>1)return XR_ERROR_PATH_UNSUPPORTED;
 for(uint32_t n=0;n<info->countSuggestedBindings;++n){const auto& b=info->suggestedBindings[n];std::string profileName,binding;
  for(const auto& [text,id]:paths){if(id==info->interactionProfile)profileName=text;if(id==b.binding)binding=text;}
  if(profileName.find("vive_controller")!=std::string::npos){need(binding.find("thumbstick")==std::string::npos&&binding.find("squeeze/value")==std::string::npos,"Vive uses trackpad and digital squeeze");}
 }
 return XR_SUCCESS;}
XrResult XRAPI_CALL attach(XrSession,const XrSessionActionSetsAttachInfo* info){need(info->countActionSets==3,"application and overlay action sets retained");attached=true;return XR_SUCCESS;}
XrResult XRAPI_CALL sync(XrSession,const XrActionsSyncInfo* info){syncCount=static_cast<int>(info->countActiveActionSets);return focused?XR_SUCCESS:XR_SESSION_NOT_FOCUSED;}
XrResult XRAPI_CALL refSpace(XrSession,const XrReferenceSpaceCreateInfo* info,XrSpace* out){need(info->referenceSpaceType==XR_REFERENCE_SPACE_TYPE_VIEW,"head-relative OpenXR space");*out=view=Handle<XrSpace>();return XR_SUCCESS;}
XrResult XRAPI_CALL actionSpace(XrSession,const XrActionSpaceCreateInfo*,XrSpace* out){*out=Handle<XrSpace>();return XR_SUCCESS;}
XrResult XRAPI_CALL locate(XrSpace,XrSpace base,XrTime,XrSpaceLocation* out){need(base==view,"right aim located in same view space");out->locationFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;out->pose={{0,0,0,1},{aimX,CenterY,0}};return XR_SUCCESS;}
XrResult XRAPI_CALL getPose(XrSession,const XrActionStateGetInfo*,XrActionStatePose* out){out->isActive=aimActive;return XR_SUCCESS;}
XrResult XRAPI_CALL getFloat(XrSession,const XrActionStateGetInfo* info,XrActionStateFloat* out){out->isActive=true;out->currentState=(info->action==gestureTrigger||info->action==gestureGrip?gestureDown:down)?1.f:0.f;return XR_SUCCESS;}
XrResult XRAPI_CALL getVector(XrSession,const XrActionStateGetInfo*,XrActionStateVector2f* out){out->isActive=true;out->currentState={0,wheel?.75f:0};return XR_SUCCESS;}
XrResult XRAPI_CALL properties(XrInstance,XrSystemId,XrSystemProperties* out){out->graphicsProperties.maxLayerCount=4;out->graphicsProperties.maxSwapchainImageWidth=4096;out->graphicsProperties.maxSwapchainImageHeight=4096;out->vendorId=123;strcpy_s(out->systemName,"Runtime-reported test system");return XR_SUCCESS;}
XrResult XRAPI_CALL formats(XrSession,uint32_t cap,uint32_t* count,int64_t* out){*count=1;if(cap)out[0]=useVk?static_cast<int64_t>(VK_FORMAT_B8G8R8A8_SRGB):static_cast<int64_t>(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);return XR_SUCCESS;}
XrResult XRAPI_CALL makeSwap(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* out){need(info->width==Width&&info->height==Height,"exact shared-surface geometry");if(useVk){vk.MakeImage(Width,Height);}else if(!d12){D3D11_TEXTURE2D_DESC d{};d.Width=Width;d.Height=Height;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;d.BindFlags=D3D11_BIND_RENDER_TARGET;need(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&texture)),"real D3D11 swapchain texture");}else{D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=Width;d.Height=Height;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;need(SUCCEEDED(dev12->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&texture12))),"real D3D12 swapchain texture");}*out=Handle<XrSwapchain>();return XR_SUCCESS;}
XrResult XRAPI_CALL swapImages(XrSwapchain,uint32_t cap,uint32_t* count,XrSwapchainImageBaseHeader* out){*count=1;if(cap){if(useVk)reinterpret_cast<XrSwapchainImageVulkanKHR*>(out)->image=vk.image;else if(d12)reinterpret_cast<XrSwapchainImageD3D12KHR*>(out)->texture=texture12.Get();else reinterpret_cast<XrSwapchainImageD3D11KHR*>(out)->texture=texture.Get();}return XR_SUCCESS;}
XrResult XRAPI_CALL acquire(XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* index){++acquires;*index=0;return XR_SUCCESS;}
XrResult XRAPI_CALL waitImage(XrSwapchain,const XrSwapchainImageWaitInfo* info){need(info->timeout==0,"no blocking wait on game render thread");return timeout?XR_TIMEOUT_EXPIRED:XR_SUCCESS;}
XrResult XRAPI_CALL release(XrSwapchain,const XrSwapchainImageReleaseInfo*){++releases;return XR_SUCCESS;}
XrResult XRAPI_CALL end(XrSession,const XrFrameEndInfo* info){layerCount=static_cast<int>(info->layerCount);need(info->layers[0]==original,"original game layer preserved by identity");if(info->layerCount>=2){auto* quad=reinterpret_cast<const XrCompositionLayerQuad*>(info->layers[1]);need(quad->type==XR_TYPE_COMPOSITION_LAYER_QUAD&&quad->space==view&&quad->eyeVisibility==XR_EYE_VISIBILITY_BOTH&&quad->pose.position.z==-Distance,"real stereo OpenXR quad submitted");need(quad->subImage.imageRect.extent.width==static_cast<int32_t>(PanelWidth)&&quad->subImage.imageRect.extent.height==static_cast<int32_t>(PanelHeight)&&quad->pose.position.x==CenterX&&quad->size.height==HeightMeters,"shorter centered panel geometry");if(info->layerCount==3){auto* cursor=reinterpret_cast<const XrCompositionLayerQuad*>(info->layers[2]);need(cursor->subImage.swapchain==quad->subImage.swapchain&&cursor->subImage.imageRect.offset.y==static_cast<int32_t>(PanelHeight)&&cursor->subImage.imageRect.extent.width==static_cast<int32_t>(CursorSize)&&(cursor->layerFlags&XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT),"native transparent pointer shares released atlas");cursorX=cursor->pose.position.x;}}return XR_SUCCESS;}
XrResult XRAPI_CALL destroySession(XrSession){++destroyed;return XR_SUCCESS;}
XrResult XRAPI_CALL destroyInstance(XrInstance){++destroyed;return XR_SUCCESS;}
XrResult XRAPI_CALL destroySet(XrActionSet){++destroyed;return XR_SUCCESS;}
XrResult XRAPI_CALL destroySpace(XrSpace){++destroyed;return XR_SUCCESS;}
XrResult XRAPI_CALL destroySwap(XrSwapchain){if(useVk)vk.DestroyImage();++destroyed;return XR_SUCCESS;}
XrResult XRAPI_CALL instanceProperties(XrInstance,XrInstanceProperties* out){strcpy_s(out->runtimeName,"Synthetic runtime (not a headset)");out->runtimeVersion=XR_MAKE_VERSION(9,8,7);return XR_SUCCESS;}
XrResult XRAPI_CALL profile(XrSession,XrPath,XrInteractionProfileState* out){return path({},selected.c_str(),&out->interactionProfile);}
XrResult XRAPI_CALL pathText(XrInstance,XrPath value,uint32_t cap,uint32_t* count,char* out){for(const auto& [name,id]:paths)if(id==value){*count=static_cast<uint32_t>(name.size()+1);if(cap<*count)return XR_ERROR_SIZE_INSUFFICIENT;strcpy_s(out,cap,name.c_str());return XR_SUCCESS;}return XR_ERROR_PATH_INVALID;}
XrResult XRAPI_CALL getBool(XrSession,const XrActionStateGetInfo*,XrActionStateBoolean* out){out->isActive=true;out->currentState=contact;return XR_SUCCESS;}
XrResult XRAPI_CALL poll(XrInstance,XrEventDataBuffer* out){if(pendingEvent==XR_TYPE_UNKNOWN)return XR_EVENT_UNAVAILABLE;
 if(pendingEvent==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){XrEventDataSessionStateChanged event{pendingEvent};event.session=eventSession;event.state=XR_SESSION_STATE_VISIBLE;memcpy(out,&event,sizeof(event));}
 else {XrEventDataInteractionProfileChanged event{pendingEvent};event.session=eventSession;memcpy(out,&event,sizeof(event));}
 pendingEvent=XR_TYPE_UNKNOWN;return XR_SUCCESS;}
XrResult XRAPI_CALL extensions(const char*,uint32_t cap,uint32_t* count,XrExtensionProperties* out){*count=extensionsAvailable?2:0;if(cap&&extensionsAvailable){strcpy_s(out[0].extensionName,"XR_BD_controller_interaction");strcpy_s(out[1].extensionName,"XR_META_touch_controller_plus");}return XR_SUCCESS;}
XrResult XRAPI_CALL get(XrInstance,const char* name,PFN_xrVoidFunction* out){
#define MAP(n,f) if(!strcmp(name,"xr" #n)){*out=reinterpret_cast<PFN_xrVoidFunction>(f);return XR_SUCCESS;}
 MAP(BeginSession,beginSession) MAP(EnumerateViewConfigurationViews,viewConfigs)
 MAP(GetInstanceProperties,instanceProperties) MAP(GetCurrentInteractionProfile,profile) MAP(PathToString,pathText) MAP(GetActionStateBoolean,getBool) MAP(PollEvent,poll) MAP(EnumerateInstanceExtensionProperties,extensions)
 MAP(CreateSession,createSession) MAP(DestroySession,destroySession) MAP(DestroyInstance,destroyInstance) MAP(EndFrame,end) MAP(CreateActionSet,createSet) MAP(DestroyActionSet,destroySet) MAP(CreateAction,createAction) MAP(StringToPath,path) MAP(SuggestInteractionProfileBindings,suggest) MAP(AttachSessionActionSets,attach) MAP(SyncActions,sync) MAP(CreateReferenceSpace,refSpace) MAP(CreateActionSpace,actionSpace) MAP(DestroySpace,destroySpace) MAP(LocateSpace,locate) MAP(GetActionStatePose,getPose) MAP(GetActionStateFloat,getFloat) MAP(GetActionStateVector2f,getVector) MAP(GetSystemProperties,properties) MAP(EnumerateSwapchainFormats,formats) MAP(CreateSwapchain,makeSwap) MAP(EnumerateSwapchainImages,swapImages) MAP(AcquireSwapchainImage,acquire) MAP(WaitSwapchainImage,waitImage) MAP(ReleaseSwapchainImage,release) MAP(DestroySwapchain,destroySwap)
#undef MAP
 *out=nullptr;return XR_ERROR_FUNCTION_UNSUPPORTED;
}
HWND testWindow{};bool standalone=true;Header* transport{};
void Key(UINT m){if(m==WM_KEYUP)++transport->reserved[0];}
void Toggle(){Key(WM_KEYDOWN);Key(WM_KEYUP);}

}
int main(int argc,char** argv){try{
 const bool abandonDevice=GetEnvironmentVariableW(L"OVERLAY_TEST_ABANDON_DEVICE",nullptr,0)!=0;
 for(int n=1;n<argc;++n){std::string arg=argv[n];if(arg=="--standalone")standalone=true;else if(arg=="--d3d12")d12=true;else if(arg=="--vulkan")useVk=true;else if(arg=="--vulkan2")useVk=useVk2=true;else if(arg=="--no-extensions")extensionsAvailable=false;else if(arg=="--api11")api11=true;else if(arg=="--index")selected="/interaction_profiles/valve/index_controller";else if(arg=="--vive")selected="/interaction_profiles/htc/vive_controller";else if(arg=="--pico")selected="/interaction_profiles/bytedance/pico4_controller";else if(arg=="--plus")selected="/interaction_profiles/meta/touch_controller_plus";else if(arg=="--motion")selected="/interaction_profiles/microsoft/motion_controller";else throw std::runtime_error("Unknown test option");}
 HoldGesture gesture;need(!gesture.Update(100,true,true,false)&&!gesture.Update(2099,true,true,false)&&gesture.Update(2100,true,true,false),"gesture requires full two seconds");need(!gesture.Update(9000,true,true,false),"held gesture fires only once");gesture.Update(9100,true,false,true);need(!gesture.Update(9200,true,true,false)&&!gesture.Update(10000,false,true,false)&&!gesture.Update(12000,true,true,false)&&gesture.Update(14000,true,true,false),"release re-arms; missing action interrupts duration");
 float x{},y{};need(Intersect(CenterX,CenterY,0,0,0,0,1,x,y)&&x==.5f&&y==.5f,"right aim hits exact panel center");need(!Intersect(2,0,0,0,0,0,1,x,y),"outside panel does not click");need(!Intersect(0,0,0,0,1,0,0,x,y),"back-facing ray rejected");
 if(useVk)vk.Initialize();else if(!d12)need(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&ctx)),"WARP D3D11 created offline");
 else {ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> adapter;need(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev12))),"WARP D3D12 created offline");D3D12_COMMAND_QUEUE_DESC q{};need(SUCCEEDED(dev12->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))),"D3D12 queue");}
 const auto name=L"Local\\ExternalOverlay.Surface."+std::to_wstring(GetCurrentProcessId());HANDLE mutex=CreateMutexW(nullptr,FALSE,(name+L".Mutex").c_str());HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,Bytes,name.c_str());auto* h=static_cast<Header*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,Bytes));need(h!=nullptr,"real shared memory surface");h->version=1;h->frame=1;h->hostTick=GetTickCount64();memset(reinterpret_cast<char*>(h)+PixelsOffset,0x7f,Width*Height*4);
 transport=h;need(!GetModuleHandleW(L"AC7_ExternalOverlay.dll"),"independent consumer without AC7 or UEVR");
 auto dll=layerLibrary=LoadLibraryW(L"ExternalOverlayXR.dll");need(dll!=nullptr,"exact built XR layer loads without VR runtime");auto negotiate=reinterpret_cast<PFN_xrNegotiateLoaderApiLayerInterface>(GetProcAddress(dll,"xrNegotiateLoaderApiLayerInterface"));XrNegotiateLoaderInfo loader{XR_LOADER_INTERFACE_STRUCT_LOADER_INFO,XR_LOADER_INFO_STRUCT_VERSION,sizeof(loader),1,1,XR_MAKE_VERSION(1,0,0),XR_MAKE_VERSION(1,1,0)};XrNegotiateApiLayerRequest request{XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST,XR_API_LAYER_INFO_STRUCT_VERSION,sizeof(request)};
 need(XR_FAILED(negotiate(&loader,"wrong",&request)),"wrong layer identity rejected");need(XR_SUCCEEDED(negotiate(&loader,"XR_APILAYER_EXTERNAL_control_overlay",&request)),"loader negotiation");
 XrApiLayerNextInfo next{};next.nextGetInstanceProcAddr=get;next.nextCreateApiLayerInstance=create;XrApiLayerCreateInfo li{};li.nextInfo=&next;XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};ci.applicationInfo.apiVersion=api11?XR_MAKE_VERSION(1,1,0):XR_MAKE_VERSION(1,0,22);XrInstance i{};need(XR_SUCCEEDED(request.createApiLayerInstance(&ci,&li,&i)),"instance and controller actions created");
#define LOAD(n) PFN_xr##n n{};need(XR_SUCCEEDED(request.getInstanceProcAddr(i,"xr" #n,reinterpret_cast<PFN_xrVoidFunction*>(&n))),"load " #n);
 LOAD(BeginSession) LOAD(PollEvent) LOAD(CreateSession) LOAD(AttachSessionActionSets) LOAD(SyncActions) LOAD(EndFrame) LOAD(DestroySession) LOAD(DestroyInstance) LOAD(SuggestInteractionProfileBindings)
#undef LOAD
 XrGraphicsBindingD3D11KHR bind{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};bind.device=dev.Get();XrGraphicsBindingD3D12KHR bind12{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};bind12.device=dev12.Get();bind12.queue=queue.Get();XrSessionCreateInfo si{XR_TYPE_SESSION_CREATE_INFO};XrGraphicsBindingVulkanKHR bindVk{useVk2?XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR:XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};bindVk.instance=vk.instance;bindVk.physicalDevice=vk.physical;bindVk.device=vk.device;bindVk.queueFamilyIndex=vk.family;si.next=useVk?static_cast<void*>(&bindVk):d12?static_cast<void*>(&bind12):static_cast<void*>(&bind);XrSession s{};need(XR_SUCCEEDED(CreateSession(i,&si,&s)),"session integration");
 XrActionSuggestedBinding existing{Handle<XrAction>(),1};XrInteractionProfileSuggestedBinding suggestion{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};path(i,"/interaction_profiles/oculus/touch_controller",&suggestion.interactionProfile);suggestion.countSuggestedBindings=1;suggestion.suggestedBindings=&existing;SuggestInteractionProfileBindings(i,&suggestion);need(bindingCount==8,"Touch bindings preserve existing app suggestions");
 rejectOverlay=true;need(SuggestInteractionProfileBindings(i,&suggestion)==XR_SUCCESS&&bindingCount==1,"rejected overlay bindings preserve application suggestions");rejectOverlay=false;SuggestInteractionProfileBindings(i,&suggestion);
 XrActionSet app=Handle<XrActionSet>();XrSessionActionSetsAttachInfo a{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};a.countActionSets=1;a.actionSets=&app;need(XR_SUCCEEDED(AttachSessionActionSets(s,&a))&&attached,"attach actions once");XrActiveActionSet active{app,XR_NULL_PATH};XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};syncInfo.countActiveActionSets=1;syncInfo.activeActionSets=&active;SyncActions(s,&syncInfo);need(syncCount==2,"hidden overlay does not capture controller");
 XrCompositionLayerQuad scene{XR_TYPE_COMPOSITION_LAYER_QUAD};original=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&scene);XrFrameEndInfo frame{XR_TYPE_FRAME_END_INFO};frame.layerCount=1;frame.layers=&original;frame.displayTime=1;
 bool syncFrames=true;auto Frame=[&](XrSession value,const XrFrameEndInfo* f){if(syncFrames)SyncActions(value,&syncInfo);auto r=EndFrame(value,f);if(useVk&&acquires>releases&&!timeout){VulkanFixture::Check(vk.vkQueueWaitIdle(vk.queue));r=EndFrame(value,f);}return r;};
 XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;need(BeginSession(s,&begin)==XR_SUCCESS,"application begin preserved");
 XrCompositionLayerProjectionView pv[2]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
 pv[0].subImage.imageRect={{25,30},{2100,2250}};pv[1].subImage.imageRect={{2200,40},{2050,2200}};pv[1].subImage.imageArrayIndex=1;
 XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.viewCount=2;projection.views=pv;
 original=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);Frame(s,&frame);
 auto* output=reinterpret_cast<XrResolution*>(reinterpret_cast<char*>(h)+ResolutionOffset);
 need(output->version==1&&output->viewCount==2&&output->flags==3&&output->viewType==2&&output->views[0].width==2100&&output->views[1].width==2050&&output->views[1].height==2200&&output->views[0].recommendedWidth==3000,"actual per-view subrects and recommendation remain distinct, including array views");
 pv[0].subImage.imageRect.extent={2150,2300};Frame(s,&frame);need(output->views[0].width==2150,"external hot output changes observed without changing game layers");
 original=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&scene);Frame(s,&frame);need(output->viewCount==0&&!(output->flags&2)&&!output->views[0].width,"quad-only frame clears previous projection dimensions");
 auto* xrInfo=reinterpret_cast<XrInformation*>(reinterpret_cast<char*>(h)+InformationOffset);
 need(xrInfo->version==1&&xrInfo->sessionActive&&std::string(xrInfo->system)=="Runtime-reported test system"&&std::string(xrInfo->runtime)=="Synthetic runtime (not a headset)"&&std::string(xrInfo->profiles[1])==selected,"real DLL publishes system/runtime/profile separately");
 Frame(s,&frame);need(layerCount==1,"hidden by default");Key(WM_KEYDOWN);Frame(s,&frame);need(layerCount==1,"F10 hold does not toggle repeatedly");Key(WM_KEYUP);timeout=true;Frame(s,&frame);need(layerCount==1&&acquires==1&&releases==0,"swapchain timeout retains pending acquisition");timeout=false;Frame(s,&frame);need(layerCount==3&&acquires==1&&releases==1,"F10 opens quad, pending image released exactly once");SyncActions(s,&syncInfo);need(syncCount==3,"visible overlay activates controller actions");
 down=true;Frame(s,&frame);down=false;Frame(s,&frame);bool press=false,up=false;auto* events=reinterpret_cast<Event*>(reinterpret_cast<char*>(h)+64);for(uint32_t n=0;n<h->writeSequence;++n){press|=events[n].type==2;up|=events[n].type==3;}need(press&&up,"right trigger delivers ordered down/up to real shared memory");
 // Every visible frame must retain the last released image during an update.
 // Do not hide a missing first submission behind the Frame() GPU-drain helper.
 ++h->frame;h->hostTick=GetTickCount64();timeout=true;
 SyncActions(s,&syncInfo);EndFrame(s,&frame);
 need(layerCount==3,"updating panel stays visible during acquire timeout");
 timeout=false;SyncActions(s,&syncInfo);EndFrame(s,&frame);
 need(layerCount==3,"new panel upload does not omit the visible quad");
 if(useVk)VulkanFixture::Check(vk.vkQueueWaitIdle(vk.queue));
 Frame(s,&frame);need(acquires==releases,"updated image released exactly once");
 // A slow browser must not accumulate 1,000 frame poses in a 128-slot queue.
 h->readSequence=h->writeSequence;quiet=true;
 for(int n=0;n<1000;++n){h->hostTick=GetTickCount64();Frame(s,&frame);}
 quiet=false;need(h->status==0&&h->writeSequence-h->readSequence==1&&layerCount==3,"1000 undrained pose samples keep latest pointer and overlay open");
 down=true;Frame(s,&frame);down=false;Frame(s,&frame);
 const auto first=h->readSequence;need(h->writeSequence-first==4&&events[first%128].type==1&&events[(first+1)%128].type==2&&events[(first+2)%128].type==1&&events[(first+3)%128].type==3,"pose coalescing preserves ordered button transitions");
 h->readSequence=h->writeSequence;
 // Browser image stays unchanged while the native pointer follows each frame.
 const auto uploads=acquires;quiet=true;
 for(int n=0;n<90;++n){aimX=CenterX+float(n)*.001f;h->hostTick=GetTickCount64();Frame(s,&frame);need(layerCount==3&&std::abs(cursorX-aimX)<.00001f,"pointer follows current OpenXR pose without browser frame");}
 quiet=false;need(acquires==uploads,"90 moving-pointer frames need zero extra texture uploads");aimX=CenterX;aimActive=false;Frame(s,&frame);need(layerCount==2,"inactive controller hides native cursor");aimActive=true;Frame(s,&frame);need(layerCount==3,"tracking recovery restores native cursor");
 if(useVk){auto pixels=vk.Readback(Width,Height);need(pixels[0]==0x7f,"shared BGRA pixels reached Vulkan image");const auto offset=size_t(PanelHeight)*Width*4;need(pixels[offset+3]==0&&pixels[offset+12*Width*4+12*4+3]==255&&pixels[offset+12*Width*4+12*4]==234,"Vulkan cursor transparency and color read back");}else if(!d12){D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;need(SUCCEEDED(dev->CreateTexture2D(&desc,nullptr,&staging)),"readback texture");ctx->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped{};need(SUCCEEDED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"readback maps");need(*static_cast<unsigned char*>(mapped.pData)==0x7f,"actual shared pixels reached GPU texture");auto* sprite=static_cast<unsigned char*>(mapped.pData)+PanelHeight*mapped.RowPitch;
 need(sprite[3]==0&&sprite[12*mapped.RowPitch+12*4+3]==255&&sprite[12*mapped.RowPitch+12*4]==234,"GPU cursor sprite has transparent corners and opaque blue center");ctx->Unmap(staging.Get(),0);}
 else {ComPtr<ID3D12Fence> fence;dev12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence));queue->Signal(fence.Get(),1);for(int n=0;n<1000&&fence->GetCompletedValue()<1;++n)Sleep(1);need(fence->GetCompletedValue()==1,"D3D12 upload commands completed");}
 Toggle();Frame(s,&frame);need(layerCount==1,"F10 closes quad");SyncActions(s,&syncInfo);need(syncCount==2,"closing releases controller input");Toggle();Frame(s,&frame);++h->reserved[2];Frame(s,&frame);need(layerCount==1,"Close button protocol closes quad");
 gestureDown=down=true;const auto beforeGesture=h->writeSequence;Frame(s,&frame);need(layerCount==1,"four-trigger gesture does not open immediately");Sleep(2050);h->hostTick=GetTickCount64();Frame(s,&frame);need(layerCount==3,"four triggers held two seconds open exact compiled layer");bool accidental=false;for(uint32_t n=beforeGesture;n<h->writeSequence;++n)accidental|=events[n%128].type==2;need(!accidental,"opening gesture cannot accidentally click panel");++h->reserved[2];Frame(s,&frame);Sleep(2050);h->hostTick=GetTickCount64();Frame(s,&frame);need(layerCount==1,"closing while gesture held does not reopen");gestureDown=down=false;Frame(s,&frame);
 Toggle();h->hostTick=0;Frame(s,&frame);need(layerCount==1,"stale UI cannot be displayed");
 // Input recovery: loss of focus and profile change cancel old button state.
 h->hostTick=GetTickCount64();Toggle();Frame(s,&frame);down=false;Frame(s,&frame);down=true;Frame(s,&frame);
 focused=false;Frame(s,&frame);need(!xrInfo->input&&layerCount==2,"unfocused sync cancels input but retains panel");
 focused=true;const auto prior=h->writeSequence;Frame(s,&frame);
 bool phantom=false;for(uint32_t n=prior;n<h->writeSequence;++n)phantom|=events[n%128].type==2;need(!phantom,"focus recovery with held trigger cannot click");down=false;Frame(s,&frame);
 aimActive=false;down=true;Frame(s,&frame);aimActive=true;const auto beforeTracking=h->writeSequence;Frame(s,&frame);phantom=false;for(uint32_t n=beforeTracking;n<h->writeSequence;++n)phantom|=events[n%128].type==2;need(!phantom,"tracking recovery with held trigger cannot click");down=false;Frame(s,&frame);
 eventSession=s;pendingEvent=XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED;XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};need(PollEvent(i,&event)==XR_SUCCESS&&event.type==XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED,"profile event forwarded unmodified");
 syncFrames=false;Frame(s,&frame);need(layerCount==2,"profile event invalidates input until sync");syncFrames=true;Frame(s,&frame);
 syncFrames=false;Sleep(1050);h->hostTick=GetTickCount64();Frame(s,&frame);need(layerCount==2&&!xrInfo->input,"stale sync cancels controller state");syncFrames=true;Frame(s,&frame);
 if(selected.find("vive_controller")!=std::string::npos){wheel=true;contact=false;auto start=h->writeSequence;Sleep(90);Frame(s,&frame);bool scrolled=false;for(uint32_t n=start;n<h->writeSequence;++n)scrolled|=events[n%128].type==5;need(!scrolled,"untouched trackpad cannot scroll");contact=true;start=h->writeSequence;Frame(s,&frame);scrolled=false;for(uint32_t n=start;n<h->writeSequence;++n)scrolled|=events[n%128].type==5;need(scrolled,"touched Vive/Sword-compatible trackpad scrolls");}
 if(abandonDevice){
  need(useVk,"abandoned-device regression requires Vulkan");
  vk.DestroyImage();vk.vkDestroyDevice(vk.device,nullptr);vk.device=VK_NULL_HANDLE;
  // A faulty host may exit without xrDestroySession. Process detach must not
  // call through the now-invalid borrowed device, even on this error path.
  FreeLibrary(layerLibrary);layerLibrary=nullptr;
  UnmapViewOfFile(h);CloseHandle(mapping);CloseHandle(mutex);
  std::cout<<"PASS abandoned session: DLL unload does not access a destroyed Vulkan device\n";return 0;
 }
 if(testWindow)DestroyWindow(testWindow);DestroySession(s);need(!xrInfo->sessionActive&&!xrInfo->system[0],"destroy clears live headset information");need(XR_SUCCEEDED(CreateSession(i,&si,&s)),"session can be recreated");AttachSessionActionSets(s,&a);SyncActions(s,&syncInfo);need(xrInfo->sessionActive&&std::string(xrInfo->system)=="Runtime-reported test system"&&!h->visible,"new session resets visibility and republishes identity");DestroySession(s);DestroyInstance(i);need(destroyed==11,"session and instance resources destroyed");UnmapViewOfFile(h);CloseHandle(mapping);CloseHandle(mutex);FreeLibrary(layerLibrary);layerLibrary=nullptr;std::cout<<"PASS exact DLL OpenXR chain, synthetic runtime; NO physical VR or FPS certification\n";return 0;
 }catch(const std::exception& e){if(layerLibrary){FreeLibrary(layerLibrary);layerLibrary=nullptr;}std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}


