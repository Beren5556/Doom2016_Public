// Reusable composition/input layer: no AC7/UEVR/render-settings dependencies.
#include <windows.h>
#include <psapi.h>
#include <d3d11.h>
#include <d3d12.h>
#include <wrl/client.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <mutex>
#include "Surface.h"
#include "HoldGesture.h"
#include "XrInformation.h"
#include "XrResolution.h"
#include "VulkanUpload.h"
using Microsoft::WRL::ComPtr;
using namespace external_overlay;
namespace {
PFN_xrGetInstanceProcAddr nextGet{};XrInstance instance{};XrSession session{};
XrActionSet actionSet{},gestureSet{};XrAction aim{},trigger{},scroll{},scrollTouch{},gestureTrigger{},gestureGrip{};XrSpace aimSpace{},viewSpace{};XrSwapchain swapchain{};
XrPath right{},left{};HoldGesture gesture;
XrInformation information;
XrResolution resolution;XrSystemId sessionSystem{};
PFN_xrBeginSession BeginSession{};
PFN_xrEnumerateViewConfigurationViews EnumerateViewConfigurationViews{};
bool picoEnabled{},plusEnabled{},inputReady{},profilesDirty=true;
ULONGLONG lastSync{},lastProfile{},lastScroll{};
XrPath currentProfiles[2]{};
const char* profileNames[]{"/interaction_profiles/oculus/touch_controller","/interaction_profiles/valve/index_controller","/interaction_profiles/microsoft/motion_controller","/interaction_profiles/htc/vive_controller","/interaction_profiles/bytedance/pico4_controller","/interaction_profiles/meta/touch_controller_plus"};
bool ProfileEnabled(size_t n){return n==4?picoEnabled:n==5?plusEnabled:true;}
// Touch/Index core bindings follow validated W40KRT_VR Bridge.cpp:877-958,
// commit 0fb98a2f991075256dffd2117f9d458360caa324. No headset-brand switch.

bool attached{},visible{},triggerDown{},clickArmed{},hasImage{},acquired{};uint32_t maxLayers=16,lastFrame{},acquiredIndex{},toggleSequence{},hostGeneration{},closeSequence{};
bool pointerHit{};float pointerX{},pointerY{};
ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
std::vector<XrSwapchainImageD3D11KHR> images;Surface surface;
ComPtr<ID3D12Device> device12;ComPtr<ID3D12CommandQueue> queue12;ComPtr<ID3D12CommandAllocator> allocator12;ComPtr<ID3D12GraphicsCommandList> list12;ComPtr<ID3D12Fence> fence12;ComPtr<ID3D12Resource> upload12;UINT64 fenceValue{};
std::vector<XrSwapchainImageD3D12KHR> images12;D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
// ReleaseSession owns GPU cleanup while the borrowed device is alive. Calling
// Vulkan from a process-detach destructor can reenter an already unloaded layer.
VulkanUpload vulkan{false};std::vector<XrSwapchainImageVulkanKHR> imagesVk;

std::vector<unsigned char> pendingPixels;uint32_t pendingFrame{};
std::recursive_mutex guard;
#define FN(name) PFN_xr##name name{}
FN(GetInstanceProperties);FN(GetCurrentInteractionProfile);FN(PathToString);FN(PollEvent);FN(GetActionStateBoolean);
FN(CreateSession);FN(DestroySession);FN(DestroyInstance);FN(EndFrame);FN(AttachSessionActionSets);FN(SyncActions);FN(SuggestInteractionProfileBindings);
FN(CreateActionSet);FN(DestroyActionSet);FN(CreateAction);FN(StringToPath);FN(CreateActionSpace);FN(CreateReferenceSpace);FN(DestroySpace);FN(LocateSpace);FN(GetActionStateFloat);FN(GetActionStateVector2f);FN(GetActionStatePose);
FN(CreateSwapchain);FN(DestroySwapchain);FN(EnumerateSwapchainFormats);FN(EnumerateSwapchainImages);FN(AcquireSwapchainImage);FN(WaitSwapchainImage);FN(ReleaseSwapchainImage);FN(GetSystemProperties);
#undef FN
XrPath Path(const char* name){XrPath p{};if(XR_FAILED(StringToPath(instance,name,&p)))return XR_NULL_PATH;return p;}
void PublishInformation(){
 information.tick=GetTickCount64();
 memcpy(reinterpret_cast<char*>(surface.H())+InformationOffset,&information,sizeof(information));
 memcpy(reinterpret_cast<char*>(surface.H())+ResolutionOffset,&resolution,sizeof(resolution));
}
XrResult XRAPI_CALL Begin(XrSession s,const XrSessionBeginInfo* info){
 std::scoped_lock lock(guard);const auto result=BeginSession(s,info);
 if(result==XR_SUCCESS&&s==session&&info){
  resolution.viewType=static_cast<uint32_t>(info->primaryViewConfigurationType);
  if(EnumerateViewConfigurationViews){uint32_t count{};
   if(XR_SUCCEEDED(EnumerateViewConfigurationViews(instance,sessionSystem,info->primaryViewConfigurationType,0,&count,nullptr))&&count<=4){
    XrViewConfigurationView views[4]{};for(auto& v:views)v.type=XR_TYPE_VIEW_CONFIGURATION_VIEW;
    if(XR_SUCCEEDED(EnumerateViewConfigurationViews(instance,sessionSystem,info->primaryViewConfigurationType,count,&count,views)))
     for(uint32_t n=0;n<count;++n){auto& v=resolution.views[n];v.recommendedWidth=views[n].recommendedImageRectWidth;v.recommendedHeight=views[n].recommendedImageRectHeight;v.maxWidth=views[n].maxImageRectWidth;v.maxHeight=views[n].maxImageRectHeight;}
   }
  }
 }
 return result;
}
void ObserveResolution(const XrFrameEndInfo* frame){
 resolution.flags=1;resolution.viewCount=0;resolution.tick=GetTickCount64();
 for(auto& view:resolution.views)view.width=view.height=0;
 const XrCompositionLayerProjection* projection=nullptr;
 for(uint32_t n=0;n<frame->layerCount;++n)if(frame->layers[n]&&frame->layers[n]->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION){
  if(projection)resolution.flags|=4;else projection=reinterpret_cast<const XrCompositionLayerProjection*>(frame->layers[n]);
 }
 if(!projection)return;
 resolution.flags|=2;resolution.viewCount=projection->viewCount;if(projection->viewCount>4)resolution.flags|=8;
 for(uint32_t n=0;n<std::min(4u,projection->viewCount);++n){const auto size=projection->views[n].subImage.imageRect.extent;
  if(size.width>0&&size.height>0){resolution.views[n].width=static_cast<uint32_t>(size.width);resolution.views[n].height=static_cast<uint32_t>(size.height);}
 }
}
void ResetInput(){
 inputReady=false;lastSync=0;lastScroll=0;gesture=HoldGesture{};clickArmed=false;triggerDown=false;pointerHit=false;information.input=0;
 if(surface.Lock()){surface.Push(4,0,0);PublishInformation();surface.Unlock();}
}
void Profiles(){
 const ULONGLONG now=GetTickCount64();if(!profilesDirty&&now-lastProfile<1000)return;
 profilesDirty=false;lastProfile=now;XrPath hands[]{left,right};
 for(int hand=0;hand<2;++hand){
  XrInteractionProfileState p{XR_TYPE_INTERACTION_PROFILE_STATE};
  const auto result=GetCurrentInteractionProfile(session,hands[hand],&p);
  if(result!=XR_SUCCESS)p.interactionProfile=XR_NULL_PATH;
  if(p.interactionProfile!=currentProfiles[hand]){ResetInput();currentProfiles[hand]=p.interactionProfile;}
  information.profiles[hand][0]=0;
  if(p.interactionProfile){char text[XR_MAX_PATH_LENGTH]{};uint32_t count{};
   if(XR_SUCCEEDED(PathToString(instance,p.interactionProfile,sizeof(text),&count,text)))InfoText(information.profiles[hand],text);
  }
 }
}
std::vector<XrActionSuggestedBinding> Bindings(XrPath profile){
 std::vector<XrActionSuggestedBinding> b;
 for(size_t n=0;n<std::size(profileNames);++n)if(ProfileEnabled(n)&&profile==Path(profileNames[n])){
  const bool pad=n==3,digital=n==2||pad;
  b={{aim,Path("/user/hand/right/input/aim/pose")},{trigger,Path("/user/hand/right/input/trigger/value")},{scroll,Path(pad?"/user/hand/right/input/trackpad":"/user/hand/right/input/thumbstick")},{gestureTrigger,Path("/user/hand/right/input/trigger/value")},{gestureTrigger,Path("/user/hand/left/input/trigger/value")}};
  b.push_back({gestureGrip,Path(digital?"/user/hand/right/input/squeeze/click":"/user/hand/right/input/squeeze/value")});
  b.push_back({gestureGrip,Path(digital?"/user/hand/left/input/squeeze/click":"/user/hand/left/input/squeeze/value")});
  if(pad)b.push_back({scrollTouch,Path("/user/hand/right/input/trackpad/touch")});break;
 }
 return b;
}
void BindingResult(XrPath profile,XrResult result){for(size_t n=0;n<std::size(profileNames);++n)if(profile==Path(profileNames[n]))information.bindings[n]=static_cast<int32_t>(result);}
XrResult XRAPI_CALL Suggest(XrInstance i,const XrInteractionProfileSuggestedBinding* info){
 if(!info)return XR_ERROR_VALIDATION_FAILURE;
 std::scoped_lock lock(guard);auto extra=Bindings(info->interactionProfile);if(extra.empty())return SuggestInteractionProfileBindings(i,info);auto copy=*info;
 std::vector<XrActionSuggestedBinding> bindings;if(info->countSuggestedBindings)bindings.assign(info->suggestedBindings,info->suggestedBindings+info->countSuggestedBindings);
 bindings.insert(bindings.end(),extra.begin(),extra.end());copy.countSuggestedBindings=static_cast<uint32_t>(bindings.size());copy.suggestedBindings=bindings.data();
 const auto result=SuggestInteractionProfileBindings(i,&copy);BindingResult(info->interactionProfile,result);
 // Optional overlay input must never invalidate the application's own bindings.
 return XR_FAILED(result)?SuggestInteractionProfileBindings(i,info):result;
}
XrResult XRAPI_CALL Poll(XrInstance i,XrEventDataBuffer* event){
 std::scoped_lock lock(guard);const auto result=PollEvent(i,event);if(result!=XR_SUCCESS||!event)return result;
 if(event->type==XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED){const auto* e=reinterpret_cast<const XrEventDataInteractionProfileChanged*>(event);if(e->session==session){profilesDirty=true;ResetInput();}}
 if(event->type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){const auto* e=reinterpret_cast<const XrEventDataSessionStateChanged*>(event);if(e->session==session&&e->state!=XR_SESSION_STATE_FOCUSED)ResetInput();}
 if(event->type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)ResetInput();
 return result; // Observe exactly the event the application requested; never drain it.
}
bool MakeActions(){
 XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};strcpy_s(set.actionSetName,"external_overlay");strcpy_s(set.localizedActionSetName,"External Overlay");set.priority=1000;
 if(XR_FAILED(CreateActionSet(instance,&set,&actionSet)))return false;right=Path("/user/hand/right");left=Path("/user/hand/left");
 auto action=[](const char* name,XrActionType type,XrAction* result){XrActionCreateInfo a{XR_TYPE_ACTION_CREATE_INFO};strcpy_s(a.actionName,name);strcpy_s(a.localizedActionName,name);a.actionType=type;a.countSubactionPaths=1;a.subactionPaths=&right;return XR_SUCCEEDED(CreateAction(actionSet,&a,result));};
 if(!action("scroll_touch",XR_ACTION_TYPE_BOOLEAN_INPUT,&scrollTouch)||!action("aim",XR_ACTION_TYPE_POSE_INPUT,&aim)||!action("click",XR_ACTION_TYPE_FLOAT_INPUT,&trigger)||!action("scroll",XR_ACTION_TYPE_VECTOR2F_INPUT,&scroll))return false;
 strcpy_s(set.actionSetName,"external_overlay_gesture");strcpy_s(set.localizedActionSetName,"Overlay opening gesture");set.priority=0;
 if(XR_FAILED(CreateActionSet(instance,&set,&gestureSet)))return false;
 XrPath hands[]{left,right};auto makeGesture=[&](const char* name,XrAction* result){XrActionCreateInfo a{XR_TYPE_ACTION_CREATE_INFO};strcpy_s(a.actionName,name);strcpy_s(a.localizedActionName,name);a.actionType=XR_ACTION_TYPE_FLOAT_INPUT;a.countSubactionPaths=2;a.subactionPaths=hands;return XR_SUCCEEDED(CreateAction(gestureSet,&a,result));};
 if(!makeGesture("gesture_trigger",&gestureTrigger)||!makeGesture("gesture_grip",&gestureGrip))return false;
 // Baseline suggestions; intercept later app suggestions to preserve both sets.
 for(size_t n=0;n<std::size(profileNames);++n)if(ProfileEnabled(n)){XrInteractionProfileSuggestedBinding b{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};b.interactionProfile=Path(profileNames[n]);auto v=Bindings(b.interactionProfile);b.countSuggestedBindings=static_cast<uint32_t>(v.size());b.suggestedBindings=v.data();BindingResult(b.interactionProfile,SuggestInteractionProfileBindings(instance,&b));}
 return true;
}
void ReleaseSession(){
 resolution=XrResolution{};sessionSystem=XR_NULL_SYSTEM_ID;
 information.sessionActive=0;information.system[0]=0;information.profiles[0][0]=information.profiles[1][0]=0;currentProfiles[0]=currentProfiles[1]=XR_NULL_PATH;profilesDirty=true;lastProfile=0;ResetInput();
 visible=false;attached=false;triggerDown=false;pointerHit=false;lastFrame=0;hasImage=false;acquired=false;pendingPixels.clear();
 if(surface.Lock()){surface.H()->visible=0;surface.Push(4,0,0);surface.Unlock();}
 vulkan.Reset();imagesVk.clear();
 if(swapchain){DestroySwapchain(swapchain);swapchain=XR_NULL_HANDLE;}if(aimSpace){DestroySpace(aimSpace);aimSpace=XR_NULL_HANDLE;}if(viewSpace){DestroySpace(viewSpace);viewSpace=XR_NULL_HANDLE;}
 images.clear();context.Reset();device.Reset();images12.clear();upload12.Reset();list12.Reset();allocator12.Reset();fence12.Reset();queue12.Reset();device12.Reset();fenceValue=0;session=XR_NULL_HANDLE;
}
XrResult XRAPI_CALL SessionCreate(XrInstance i,const XrSessionCreateInfo* info,XrSession* out){
 std::scoped_lock lock(guard);auto r=CreateSession(i,info,out);if(XR_FAILED(r)||session)return r;
 for(auto* next=reinterpret_cast<const XrBaseInStructure*>(info->next);next;next=next->next){if(next->type==XR_TYPE_GRAPHICS_BINDING_D3D11_KHR)device=reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(next)->device;if(next->type==XR_TYPE_GRAPHICS_BINDING_D3D12_KHR){auto binding=reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(next);device12=binding->device;queue12=binding->queue;}}
 for(auto* next=reinterpret_cast<const XrBaseInStructure*>(info->next);next;next=next->next)if(next->type==XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR){const auto* binding=reinterpret_cast<const XrGraphicsBindingVulkanKHR*>(next);vulkan.Initialize(binding->instance,binding->physicalDevice,binding->device,binding->queueFamilyIndex,binding->queueIndex,Width,Height);}
 if(!device&&!device12&&!vulkan.Active())return r;session=*out;sessionSystem=info->systemId;resolution=XrResolution{};resolution.flags=1;ResetInput();information.sessionActive=1;information.backend=device?11:device12?12:100;if(device)device->GetImmediateContext(&context);
 XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};if(XR_SUCCEEDED(GetSystemProperties(i,info->systemId,&properties))){maxLayers=properties.graphicsProperties.maxLayerCount;information.maxLayers=maxLayers;information.vendor=properties.vendorId;InfoText(information.system,properties.systemName);
 resolution.systemMaxWidth=properties.graphicsProperties.maxSwapchainImageWidth;resolution.systemMaxHeight=properties.graphicsProperties.maxSwapchainImageHeight;
 if(properties.graphicsProperties.maxSwapchainImageWidth<Width||properties.graphicsProperties.maxSwapchainImageHeight<Height){ReleaseSession();return r;}}
 XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;space.poseInReferenceSpace.orientation.w=1;
 if(XR_FAILED(CreateReferenceSpace(session,&space,&viewSpace))){ReleaseSession();return r;}
 XrActionSpaceCreateInfo a{XR_TYPE_ACTION_SPACE_CREATE_INFO};a.action=aim;a.subactionPath=right;a.poseInActionSpace.orientation.w=1;
 if(XR_FAILED(CreateActionSpace(session,&a,&aimSpace))){ReleaseSession();return r;}
 uint32_t count{};EnumerateSwapchainFormats(session,0,&count,nullptr);std::vector<int64_t> formats(count);EnumerateSwapchainFormats(session,count,&count,formats.data());
 const int64_t format=vulkan.Active()?static_cast<int64_t>(VK_FORMAT_B8G8R8A8_SRGB):static_cast<int64_t>(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);if(std::find(formats.begin(),formats.end(),format)==formats.end()){ReleaseSession();return r;}
 XrSwapchainCreateInfo s{XR_TYPE_SWAPCHAIN_CREATE_INFO};s.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;s.format=format;s.sampleCount=1;s.width=Width;s.height=Height;s.faceCount=1;s.arraySize=1;s.mipCount=1;
 if(XR_FAILED(CreateSwapchain(session,&s,&swapchain))){ReleaseSession();return r;}
 if(XR_FAILED(EnumerateSwapchainImages(swapchain,0,&count,nullptr))||!count){ReleaseSession();return r;}
 if(device){images.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});if(XR_FAILED(EnumerateSwapchainImages(swapchain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()))))ReleaseSession();}
 else if(vulkan.Active()){
  imagesVk.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});if(XR_FAILED(EnumerateSwapchainImages(swapchain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(imagesVk.data())))){ReleaseSession();return r;}
 }
 else {
  images12.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});if(XR_FAILED(EnumerateSwapchainImages(swapchain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images12.data())))){ReleaseSession();return r;}
  auto desc=images12[0].texture->GetDesc();UINT64 bytes{};device12->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if(FAILED(device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload12)))||FAILED(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator12)))||FAILED(device12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator12.Get(),nullptr,IID_PPV_ARGS(&list12)))||FAILED(device12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence12)))){ReleaseSession();return r;}list12->Close();
 }
 return r;
}
XrResult XRAPI_CALL SessionDestroy(XrSession s){std::scoped_lock lock(guard);if(s==session)ReleaseSession();return DestroySession(s);}
XrResult XRAPI_CALL InstanceDestroy(XrInstance i){std::scoped_lock lock(guard);if(i==instance){if(session)ReleaseSession();if(actionSet)DestroyActionSet(actionSet);if(gestureSet)DestroyActionSet(gestureSet);actionSet=gestureSet=XR_NULL_HANDLE;instance=XR_NULL_HANDLE;}return DestroyInstance(i);}
XrResult XRAPI_CALL Attach(XrSession s,const XrSessionActionSetsAttachInfo* info){
 std::scoped_lock lock(guard);if(s!=session||!actionSet)return AttachSessionActionSets(s,info);
 std::vector<XrActionSet> sets;if(info->countActionSets)sets.assign(info->actionSets,info->actionSets+info->countActionSets);sets.push_back(actionSet);sets.push_back(gestureSet);auto copy=*info;copy.countActionSets=static_cast<uint32_t>(sets.size());copy.actionSets=sets.data();auto r=AttachSessionActionSets(s,&copy);attached=XR_SUCCEEDED(r);return r;
}
XrResult XRAPI_CALL Sync(XrSession s,const XrActionsSyncInfo* info){
 std::scoped_lock lock(guard);if(s!=session||!attached)return SyncActions(s,info);
 std::vector<XrActiveActionSet> sets;if(info->countActiveActionSets)sets.assign(info->activeActionSets,info->activeActionSets+info->countActiveActionSets);sets.push_back({gestureSet,XR_NULL_PATH});if(visible)sets.push_back({actionSet,right});auto copy=*info;copy.countActiveActionSets=static_cast<uint32_t>(sets.size());copy.activeActionSets=sets.data();auto result=SyncActions(s,&copy);if(result==XR_SUCCESS){Profiles();inputReady=true;lastSync=GetTickCount64();}else ResetInput();if(surface.Lock()){surface.H()->reserved[6]=static_cast<uint32_t>(result);++surface.H()->reserved[7];PublishInformation();surface.Unlock();}return result;
}
bool OpeningGesture(){
 if(!inputReady||GetTickCount64()-lastSync>1000){ResetInput();return false;}
 bool active=true,allDown=true,released=true;
 for(auto hand:{left,right})for(auto action:{gestureTrigger,gestureGrip}){XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};get.action=action;get.subactionPath=hand;XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};const bool valid=XR_SUCCEEDED(GetActionStateFloat(session,&get,&state))&&state.isActive;active&=valid;allDown&=valid&&state.currentState>=.8f;released&=valid&&state.currentState<=.25f;}
 information.input=(information.input&~16u)|(active?16u:0u);return gesture.Update(GetTickCount64(),active,allDown,released);
}
void Input(XrTime time){
 if(!inputReady){pointerHit=false;surface.Push(4,0,0);return;}
 XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};get.subactionPath=right;get.action=aim;XrActionStatePose pose{XR_TYPE_ACTION_STATE_POSE};
 XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};float x{},y{};bool hit=false;uint32_t inputFlags=0;
 if(XR_SUCCEEDED(GetActionStatePose(session,&get,&pose))&&pose.isActive)inputFlags|=1;
 if((inputFlags&1)&&XR_SUCCEEDED(LocateSpace(aimSpace,viewSpace,time,&loc))&&(loc.locationFlags&(XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))==(XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)){
  inputFlags|=2;
  auto p=loc.pose.position;auto q=loc.pose.orientation;hit=Intersect(p.x,p.y,p.z,q.x,q.y,q.z,q.w,x,y);
 }
 XrActionStateFloat click{XR_TYPE_ACTION_STATE_FLOAT};get.action=trigger;const bool validClick=XR_SUCCEEDED(GetActionStateFloat(session,&get,&click))&&click.isActive;if(!(inputFlags&2)||!validClick)clickArmed=false;else if(click.currentState<=.25f)clickArmed=true;const bool down=clickArmed&&validClick&&click.currentState>.55f;
 XrActionStateVector2f wheel{XR_TYPE_ACTION_STATE_VECTOR2F};get.action=scroll;GetActionStateVector2f(session,&get,&wheel);
 surface.H()->reserved[3]=inputFlags|(hit?4u:0u)|(validClick?8u:0u);information.input=(information.input&16u)|inputFlags|(validClick?4u:0u)|(wheel.isActive?8u:0u);
 pointerHit=hit;pointerX=x;pointerY=y;
 surface.Push(hit?1u:4u,x*PanelWidth,y*PanelHeight);
 if(down!=triggerDown){if(!down||hit)surface.Push(down?2u:3u,x*PanelWidth,y*PanelHeight);triggerDown=down;}
 bool contact=true;if(currentProfiles[1]==Path(profileNames[3])){XrActionStateBoolean touch{XR_TYPE_ACTION_STATE_BOOLEAN};get.action=scrollTouch;contact=GetActionStateBoolean(session,&get,&touch)==XR_SUCCESS&&touch.isActive&&touch.currentState;}
 if(contact&&hit&&wheel.isActive&&std::abs(wheel.currentState.y)>.25f&&GetTickCount64()-lastScroll>80){surface.Push(5,x*PanelWidth,y*PanelHeight,static_cast<int32_t>(-wheel.currentState.y*90));lastScroll=GetTickCount64();}
}
XrResult XRAPI_CALL End(XrSession s,const XrFrameEndInfo* info){
 std::scoped_lock lock(guard);if(s!=session||!swapchain||!attached||!info)return EndFrame(s,info);
 ObserveResolution(info);
 const bool openGesture=OpeningGesture();
 const bool wasVisible=visible;
 bool fresh=false;
 if(surface.Lock()){
  auto* h=surface.H();if(h->reserved[1]!=hostGeneration){visible=false;triggerDown=false;toggleSequence=0;closeSequence=h->reserved[2];lastFrame=0;hostGeneration=h->reserved[1];}if((h->reserved[0]-toggleSequence)%2)visible=!visible;toggleSequence=h->reserved[0];if(openGesture)visible=true;if(h->reserved[2]!=closeSequence){visible=false;closeSequence=h->reserved[2];}fresh=GetTickCount64()>=h->hostTick&&GetTickCount64()-h->hostTick<2000&&h->frame!=0&&h->status!=2;
  if(!fresh)visible=false;h->visible=visible?1u:0u;
  if(visible&&!wasVisible)clickArmed=false;
  if(visible){Input(info->displayTime);if(h->frame!=lastFrame){pendingPixels.resize(Width*Height*4);memcpy(pendingPixels.data(),surface.Pixels(),pendingPixels.size());StampCursor(pendingPixels.data());pendingFrame=h->frame;}}else {pointerHit=false;if(triggerDown){surface.Push(3,0,0);triggerDown=false;}}
  PublishInformation();surface.Unlock();
 }
 if(!visible||info->layerCount>=maxLayers||info->layerCount==0)return EndFrame(s,info);
 auto upload=[&]{
 if(vulkan.Active()&&vulkan.Poll()!=VK_SUCCESS)return;
 if(!pendingPixels.empty()&&(!device12||fence12->GetCompletedValue()>=fenceValue)){
 XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
 if(!acquired){if(XR_FAILED(AcquireSwapchainImage(swapchain,&acquire,&acquiredIndex)))return;acquired=true;}
 XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=0;
 const auto waited=WaitSwapchainImage(swapchain,&wait);XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
 if(waited==XR_TIMEOUT_EXPIRED)return;
 if(XR_FAILED(waited)||acquiredIndex>=(device?images.size():device12?images12.size():imagesVk.size()))return;
 if(device){context->UpdateSubresource(images[acquiredIndex].texture,0,nullptr,pendingPixels.data(),Width*4,0);context->Flush();}
 else if(vulkan.Active()){
  if(!vulkan.Submit(imagesVk[acquiredIndex].image,pendingPixels.data(),pendingPixels.size()))return;
  // Release after queue submission, as required by OpenXR. The runtime
  // synchronizes its compositor; our staging buffer remains fenced in Poll().
 }
 else {
  void* mapped{};D3D12_RANGE read{0,0};if(FAILED(upload12->Map(0,&read,&mapped)))return;
  for(uint32_t row=0;row<Height;++row)memcpy(static_cast<char*>(mapped)+footprint.Offset+row*footprint.Footprint.RowPitch,pendingPixels.data()+row*Width*4,Width*4);upload12->Unmap(0,nullptr);
  if(FAILED(allocator12->Reset())||FAILED(list12->Reset(allocator12.Get(),nullptr)))return;
  D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=images12[acquiredIndex].texture;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;list12->ResourceBarrier(1,&barrier);
  D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=upload12.Get();source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;source.PlacedFootprint=footprint;D3D12_TEXTURE_COPY_LOCATION target{};target.pResource=images12[acquiredIndex].texture;target.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list12->CopyTextureRegion(&target,0,0,0,&source,nullptr);
  std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list12->ResourceBarrier(1,&barrier);if(FAILED(list12->Close()))return;ID3D12CommandList* lists[]{list12.Get()};queue12->ExecuteCommandLists(1,lists);if(FAILED(queue12->Signal(fence12.Get(),++fenceValue)))return;
 }
 if(XR_FAILED(ReleaseSwapchainImage(swapchain,&release)))return;acquired=false;hasImage=true;lastFrame=pendingFrame;pendingPixels.clear();
 }
 };upload();
 if(!hasImage)return EndFrame(s,info);
 XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};quad.space=viewSpace;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;quad.pose.orientation.w=1;quad.pose.position={CenterX,CenterY,-Distance};quad.size={WidthMeters,HeightMeters};quad.subImage.swapchain=swapchain;quad.subImage.imageRect.extent={static_cast<int32_t>(PanelWidth),static_cast<int32_t>(PanelHeight)};
 std::vector<const XrCompositionLayerBaseHeader*> layers(info->layers,info->layers+info->layerCount);layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad));
 // Static sprite in the same released swapchain image. Only its quad pose
 // changes each OpenXR frame, independent of browser captures or GPU uploads.
 XrCompositionLayerQuad cursor=quad;
 if(pointerHit&&layers.size()<maxLayers){cursor.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;cursor.pose.position={CenterX+(pointerX-.5f)*WidthMeters,CenterY+(.5f-pointerY)*HeightMeters,-Distance+.001f};cursor.size={.025f,.025f};cursor.subImage.imageRect={{0,static_cast<int32_t>(PanelHeight)},{static_cast<int32_t>(CursorSize),static_cast<int32_t>(CursorSize)}};layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&cursor));}
 auto copy=*info;copy.layerCount=static_cast<uint32_t>(layers.size());copy.layers=layers.data();return EndFrame(s,&copy);
}
// An earlier layer in the OpenXR chain can probe global functions
// before Create supplies nextGet. Resolve only the two loader enumerators,
// never call the uninitialized next link or create another runtime/instance.
XrResult Global(const char* name,PFN_xrVoidFunction* fn){
 if(strcmp(name,"xrEnumerateInstanceExtensionProperties")&&strcmp(name,"xrEnumerateApiLayerProperties"))return XR_ERROR_FUNCTION_UNSUPPORTED;
 auto entry=[](HMODULE m)->PFN_xrGetInstanceProcAddr{
  if(!m||GetProcAddress(m,"xrNegotiateLoaderApiLayerInterface")||GetProcAddress(m,"xrNegotiateLoaderRuntimeInterface")||!GetProcAddress(m,"xrEnumerateApiLayerProperties"))return nullptr;
  return reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(m,"xrGetInstanceProcAddr"));
 };
 auto loader=entry(GetModuleHandleW(L"openxr_loader.dll"));
 if(!loader){
  std::vector<HMODULE> modules(256);DWORD bytes{};
  if(!K32EnumProcessModules(GetCurrentProcess(),modules.data(),static_cast<DWORD>(modules.size()*sizeof(HMODULE)),&bytes))return XR_ERROR_FUNCTION_UNSUPPORTED;
  if(bytes>modules.size()*sizeof(HMODULE)){modules.resize((bytes+sizeof(HMODULE)-1)/sizeof(HMODULE));if(!K32EnumProcessModules(GetCurrentProcess(),modules.data(),static_cast<DWORD>(modules.size()*sizeof(HMODULE)),&bytes))return XR_ERROR_FUNCTION_UNSUPPORTED;}
  for(size_t n=0;n<std::min(modules.size(),size_t(bytes/sizeof(HMODULE)))&&!loader;++n)loader=entry(modules[n]);
 }
 if(!loader)return XR_ERROR_FUNCTION_UNSUPPORTED;
 const auto result=loader(XR_NULL_HANDLE,name,fn);if(XR_FAILED(result)||!*fn){*fn=nullptr;return XR_ERROR_FUNCTION_UNSUPPORTED;}return XR_SUCCESS;
}
XrResult XRAPI_CALL Get(XrInstance i,const char* name,PFN_xrVoidFunction* fn){
 if(!name||!fn)return XR_ERROR_VALIDATION_FAILURE;
 *fn=nullptr;
 if(!strcmp(name,"xrGetInstanceProcAddr")){*fn=reinterpret_cast<PFN_xrVoidFunction>(Get);return XR_SUCCESS;}
 if(!i&&(!strcmp(name,"xrEnumerateInstanceExtensionProperties")||!strcmp(name,"xrEnumerateApiLayerProperties")))return Global(name,fn);
#define MAP(n,f) if(!strcmp(name,"xr" #n)){if(!i||!instance||i!=instance)return XR_ERROR_HANDLE_INVALID;*fn=reinterpret_cast<PFN_xrVoidFunction>(f);return XR_SUCCESS;}
 if(BeginSession){MAP(BeginSession,Begin)}
 MAP(PollEvent,Poll) MAP(CreateSession,SessionCreate) MAP(DestroySession,SessionDestroy) MAP(DestroyInstance,InstanceDestroy) MAP(EndFrame,End) MAP(AttachSessionActionSets,Attach) MAP(SyncActions,Sync) MAP(SuggestInteractionProfileBindings,Suggest)
#undef MAP
 if(!i)return XR_ERROR_FUNCTION_UNSUPPORTED;
 if(!nextGet||!instance||i!=instance)return XR_ERROR_HANDLE_INVALID;
 return nextGet(i,name,fn);
}
XrResult XRAPI_CALL Create(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,XrInstance* out){
 if(!info||!layer||!layer->nextInfo||!out||instance)return XR_ERROR_INITIALIZATION_FAILED;
 auto next=*layer;next.nextInfo=layer->nextInfo->next;nextGet=layer->nextInfo->nextGetInstanceProcAddr;
 information=XrInformation{};picoEnabled=false;plusEnabled=false;
 auto create=*info;std::vector<const char*> extensions;
 if(info->enabledExtensionCount)extensions.assign(info->enabledExtensionNames,info->enabledExtensionNames+info->enabledExtensionCount);
 PFN_xrEnumerateInstanceExtensionProperties enumerate{};std::vector<XrExtensionProperties> available;
 if(XR_SUCCEEDED(nextGet(XR_NULL_HANDLE,"xrEnumerateInstanceExtensionProperties",reinterpret_cast<PFN_xrVoidFunction*>(&enumerate)))&&enumerate){uint32_t count{};
  if(XR_SUCCEEDED(enumerate(nullptr,0,&count,nullptr))&&count<4096){available.resize(count,{XR_TYPE_EXTENSION_PROPERTIES});if(XR_FAILED(enumerate(nullptr,count,&count,available.data())))available.clear();}}
 auto enable=[&](const char* name){if(std::any_of(extensions.begin(),extensions.end(),[&](auto x){return !strcmp(x,name);}))return true;
  if(std::any_of(available.begin(),available.end(),[&](const auto& x){return !strcmp(x.extensionName,name);})){extensions.push_back(name);return true;}return false;};
 picoEnabled=info->applicationInfo.apiVersion>=XR_MAKE_VERSION(1,1,0)||enable("XR_BD_controller_interaction");
 plusEnabled=enable("XR_META_touch_controller_plus");
 create.enabledExtensionCount=static_cast<uint32_t>(extensions.size());create.enabledExtensionNames=extensions.data();
 auto r=layer->nextInfo->nextCreateApiLayerInstance(&create,&next,out);if(XR_FAILED(r))return r;instance=*out;
#define LOAD(n) if(XR_FAILED(nextGet(instance,"xr" #n,reinterpret_cast<PFN_xrVoidFunction*>(&n))))return XR_ERROR_INITIALIZATION_FAILED;
 LOAD(GetInstanceProperties) LOAD(GetCurrentInteractionProfile) LOAD(PathToString) LOAD(PollEvent) LOAD(GetActionStateBoolean)
 LOAD(CreateSession) LOAD(DestroySession) LOAD(DestroyInstance) LOAD(EndFrame) LOAD(AttachSessionActionSets) LOAD(SyncActions) LOAD(SuggestInteractionProfileBindings)
 LOAD(CreateActionSet) LOAD(DestroyActionSet) LOAD(CreateAction) LOAD(StringToPath) LOAD(CreateActionSpace) LOAD(CreateReferenceSpace) LOAD(DestroySpace) LOAD(LocateSpace) LOAD(GetActionStateFloat) LOAD(GetActionStateVector2f) LOAD(GetActionStatePose)
 LOAD(CreateSwapchain) LOAD(DestroySwapchain) LOAD(EnumerateSwapchainFormats) LOAD(EnumerateSwapchainImages) LOAD(AcquireSwapchainImage) LOAD(WaitSwapchainImage) LOAD(ReleaseSwapchainImage) LOAD(GetSystemProperties)
#undef LOAD
 BeginSession=nullptr;EnumerateViewConfigurationViews=nullptr;
 nextGet(instance,"xrBeginSession",reinterpret_cast<PFN_xrVoidFunction*>(&BeginSession));
 nextGet(instance,"xrEnumerateViewConfigurationViews",reinterpret_cast<PFN_xrVoidFunction*>(&EnumerateViewConfigurationViews));
 XrInstanceProperties runtime{XR_TYPE_INSTANCE_PROPERTIES};if(XR_SUCCEEDED(GetInstanceProperties(instance,&runtime))){InfoText(information.runtime,runtime.runtimeName);information.runtimeVersion=runtime.runtimeVersion;}
 if(!MakeActions())return XR_ERROR_INITIALIZATION_FAILED;return r;
}
}
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loader,const char* name,XrNegotiateApiLayerRequest* request){
 if(!loader||!name||!request||strcmp(name,"XR_APILAYER_EXTERNAL_control_overlay")||loader->structType!=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO||loader->structVersion!=XR_LOADER_INFO_STRUCT_VERSION||loader->structSize!=sizeof(*loader)||request->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST||request->structVersion!=XR_API_LAYER_INFO_STRUCT_VERSION||request->structSize!=sizeof(*request)||loader->minInterfaceVersion>1||loader->maxInterfaceVersion<1||loader->minApiVersion>XR_MAKE_VERSION(1,0,0)||loader->maxApiVersion<XR_MAKE_VERSION(1,0,0))return XR_ERROR_INITIALIZATION_FAILED;
 request->layerInterfaceVersion=1;request->layerApiVersion=XR_MAKE_VERSION(1,0,0);request->getInstanceProcAddr=Get;request->createApiLayerInstance=Create;return XR_SUCCESS;
}
