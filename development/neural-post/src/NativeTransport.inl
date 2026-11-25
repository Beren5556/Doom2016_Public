// ES2 native D3D12 transport, used ONLY in the isolated worker process.
// D3D12CreateDevice is a per-process singleton: the process boundary is essential.
inline bool DirectNR::WaitFence(ID3D12Fence* fence, UINT64 value) {
    if (!fence || !value) return true;
    auto completed=fence->GetCompletedValue();
    if(completed==UINT64_MAX)return Fail("device lost while waiting",DXGI_ERROR_DEVICE_REMOVED);
    if(completed>=value)return true;
    if(!ringEvent_ || FAILED(fence->SetEventOnCompletion(value,ringEvent_)))return Fail("SetEventOnCompletion",E_FAIL);
    if(WaitForSingleObject(ringEvent_,2000)!=WAIT_OBJECT_0)return Fail("GPU fence timeout",DXGI_ERROR_WAIT_TIMEOUT);
    completed=fence->GetCompletedValue();
    return (completed!=UINT64_MAX && completed>=value) || Fail("incomplete GPU fence",E_FAIL);
}
inline bool DirectNR::Drain() {
    if(nativeQueue_ && nativeFence_) {
        const auto v=++nativeNext_;
        if(FAILED(nativeQueue_->Signal(nativeFence_.Get(),v)) || !WaitFence(nativeFence_.Get(),v))return false;
    } else if(ctxA4_ && fenceAB_A_ && queue_) {
        if(FAILED(ctxA4_->Signal(fenceAB_A_.Get(),++vIn_)))return Fail("drain signal A",E_FAIL);
        ctxA_->Flush();
        if(FAILED(queue_->Wait(fenceAB_.Get(),vIn_)))return Fail("drain wait B",E_FAIL);
    }
    if(queue_ && ringFence_) {
        const auto v=ringNext_++;
        if(FAILED(queue_->Signal(ringFence_.Get(),v)) || !WaitFence(ringFence_.Get(),v))return false;
    }
    return true;
}
inline bool DirectNR::Init12(ID3D12Device* device,ID3D12CommandQueue* queue,const DirectConfig& cfg,const std::wstring& gameDir,const std::wstring& ownDir) {
    nativeDevice_=device;nativeQueue_=queue;cfg_=cfg;gameDir_=gameDir;ownDir_=ownDir;
    QueryPerformanceFrequency(&qpf_);scaled_=cfg_.scale<.999f;resolve_=true;
    if(cfg_.fovea>=.999f){cfg_.feather=0;cfg_.outer_scale=0;cfg_.fovea_shape=0;}
    return CreateD3D12() && CreateFences() && CreateShaders() && (cfg_.ngx>=3 || LoadNgx());
}
inline bool DirectNR::CreateNativeFences() {
    auto share=[&](ComPtr<ID3D12Fence>& b,ComPtr<ID3D12Fence>& a){
        HRESULT hr=dev_->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&b));if(FAILED(hr))return Fail("native fence",hr);
        HANDLE h{};hr=dev_->CreateSharedHandle(b.Get(),nullptr,GENERIC_ALL,nullptr,&h);if(FAILED(hr))return Fail("native fence share",hr);
        hr=nativeDevice_->OpenSharedHandle(h,IID_PPV_ARGS(&a));CloseHandle(h);return SUCCEEDED(hr)||Fail("native fence open",hr);
    };
    if(!share(fenceAB_,nativeAB_)||!share(fenceBA_,nativeBA_))return false;
    if(FAILED(nativeDevice_->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&nativeFence_))))return Fail("native retirement fence",E_FAIL);
    for(int side=0;side<2;++side){
        for(UINT i=0;i<kFrames;++i)if(FAILED(nativeDevice_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&nativeAlloc_[side][i]))))return Fail("native allocator",E_FAIL);
        if(FAILED(nativeDevice_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,nativeAlloc_[side][0].Get(),nullptr,IID_PPV_ARGS(&nativeList_[side])))||FAILED(nativeList_[side]->Close()))return Fail("native list",E_FAIL);
    }
    return true;
}
inline bool DirectNR::CreateNativePair(UINT w,UINT h,DXGI_FORMAT fmt) {
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Width=w;rd.Height=h;rd.DepthOrArraySize=rd.MipLevels=1;rd.Format=fmt;rd.SampleDesc.Count=1;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    auto share=[&](ComPtr<ID3D12Resource>& a,ComPtr<ID3D12Resource>& b){
        HRESULT hr=nativeDevice_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_SHARED,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&a));if(FAILED(hr))return Fail("native shared texture",hr);
        HANDLE handle{};hr=nativeDevice_->CreateSharedHandle(a.Get(),nullptr,GENERIC_ALL,nullptr,&handle);if(FAILED(hr))return Fail("native texture share",hr);
        hr=dev_->OpenSharedHandle(handle,IID_PPV_ARGS(&b));CloseHandle(handle);return SUCCEEDED(hr)||Fail("native texture open",hr);
    };
    if(!share(nativeColor_,sharedColor_)||!share(nativeOut_,sharedOut_))return false;
    if(!nativeRtv_){D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;if(FAILED(nativeDevice_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&nativeRtv_))))return Fail("native padding RTV",E_FAIL);}
    nativeDevice_->CreateRenderTargetView(nativeColor_.Get(),nullptr,nativeRtv_->GetCPUDescriptorHandleForHeapStart());return true;
}
inline bool DirectNR::NativeCopy(const DirectView* views,uint32_t count,bool scatter) {
    const int side=scatter?1:0;
    if(!scatter){nativeSlot_=(nativeSlot_+1)%kFrames;if(!WaitFence(nativeFence_.Get(),nativeValue_[nativeSlot_]))return false;}
    if(FAILED(nativeAlloc_[side][nativeSlot_]->Reset())||FAILED(nativeList_[side]->Reset(nativeAlloc_[side][nativeSlot_].Get(),nullptr)))return Fail("native command reset",E_FAIL);
    auto* cl=nativeList_[side].Get();auto* shared=scatter?nativeOut_.Get():nativeColor_.Get();
    bool uneven=false;for(uint32_t i=1;i<count;++i)uneven|=views[i].bottom-views[i].top!=views[0].bottom-views[0].top;
    if(!scatter&&uneven){
        Barrier(cl,shared,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);const float black[4]{};
        cl->ClearRenderTargetView(nativeRtv_->GetCPUDescriptorHandleForHeapStart(),black,0,nullptr);
        Barrier(cl,shared,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST);
    }else Barrier(cl,shared,D3D12_RESOURCE_STATE_COMMON,scatter?D3D12_RESOURCE_STATE_COPY_SOURCE:D3D12_RESOURCE_STATE_COPY_DEST);
    UINT x{};
    for(uint32_t i=0;i<count;++i){const auto& v=views[i];const UINT w=v.right-v.left,h=v.bottom-v.top;
        Barrier(cl,v.texture12,D3D12_RESOURCE_STATE_COMMON,scatter?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION eye{},atlas{};eye.pResource=v.texture12;atlas.pResource=shared;eye.Type=atlas.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_BOX box=scatter?D3D12_BOX{x,0,0,x+w,h,1}:D3D12_BOX{v.left,v.top,0,v.right,v.bottom,1};
        if(scatter)cl->CopyTextureRegion(&eye,v.left,v.top,0,&atlas,&box);else cl->CopyTextureRegion(&atlas,x,0,0,&eye,&box);
        Barrier(cl,v.texture12,scatter?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);x+=w;
    }
    Barrier(cl,shared,scatter?D3D12_RESOURCE_STATE_COPY_SOURCE:D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
    if(FAILED(cl->Close()))return Fail("native command close",E_FAIL);
    if(scatter && FAILED(nativeQueue_->Wait(nativeBA_.Get(),vOut_)))return Fail("native output wait",E_FAIL);
    ID3D12CommandList* lists[]{cl};nativeQueue_->ExecuteCommandLists(1,lists);
    if(scatter){nativeValue_[nativeSlot_]=++nativeNext_;if(FAILED(nativeQueue_->Signal(nativeFence_.Get(),nativeNext_)))return Fail("native retirement signal",E_FAIL);}
    else if(FAILED(nativeQueue_->Signal(nativeAB_.Get(),++vIn_)))return Fail("native input signal",E_FAIL);
    return SUCCEEDED(nativeDevice_->GetDeviceRemovedReason())||Fail("native device removed",nativeDevice_->GetDeviceRemovedReason());
}
