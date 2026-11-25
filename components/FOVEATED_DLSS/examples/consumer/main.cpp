#include <dlss_foveado/Foveation.h>
#include <dlss_foveado/Experimental.h>
#include <dlss_foveado/ConsumerPolicy.h>
#include <iostream>
#include <stdexcept>
#ifdef CONSUMER_D3D11
#include <dlss_foveado/Pipeline.h>
#endif
int main(){
    namespace f=dlss_foveado;
    static_assert(f::EngineVersion == FOVEATION_EXPECTED_VERSION,
                  "Installed header identity differs from CMake package version");
    f::Config config;config.enabled=true;
    // Synthetic overlapping projections for a standalone example, NOT headset calibration.
    // Left covers common X [0,.8], right [.2,1], both common Y [0,1].
    std::array<f::ViewGeometry,2> eyes{{{{1200,1100},{2200,2000},11},{{1202,1098},{2200,2000},12}}};
    eyes[0].binocular={{{.8/2200,0,0, 0,1./2000,0, 0,0,1}},1,1};
    eyes[1].binocular={{{.8/2200,0,.2, 0,1./2000,0, 0,0,1}},1,1};
    const auto plan=f::Plan(config,eyes,f::FixedPlacement(config),1,1,1);
    if(plan.version!=f::ContractVersion||plan.views[0].geometry.viewId!=11||plan.views[1].central.inputCrop.width<32)return 1;
    if(f::CentralWeight(plan.views[0].central,{1375,1000})!=1||f::CentralWeight(plan.views[1].central,{825,1000})!=1)return 2;
    if(f::CentralWeight(plan.views[0].central,{0,0})!=0||f::Config::Parse(config.Encode())!=config)return 3;
    static_assert(f::experimental::Version==1);
    f::experimental::Settings settings;settings.mode=f::experimental::Mode::Dynamic;
    if(!settings.Valid()||settings.sustainedOverloadMs!=1000||settings.recoveryDelayMs!=1000)return 4;
    // Apply the opt-in host policy to a COPY; keep independent presets stored.
    namespace policy=f::consumer_policy;
    const auto effective=policy::Effective({config,settings},policy::Renderer::Neural);
    if(effective.config.enabled||effective.settings.centralDlaa||settings.mode!=f::experimental::Mode::Dynamic)return 8;
    if(!policy::CanApply(f::experimental::Mode::Disabled,policy::Renderer::Unavailable,false,false))return 9;
    f::experimental::Controller controller;controller.Configure(settings,1000);controller.FramePeriod(10);
    for(unsigned i=1;i<=500;++i)controller.Update({i,1,1000+i*10,10},1,1000+i*10);
    if(controller.Current()!=settings.outer)return 5;
#ifdef CONSUMER_D3D11
    // Exercise the installed D3D11 export and embedded shaders on WARP.
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 6;
    f::d3d11::Operations operations(device.Get());
    f::d3d11::Image source,crop;operations.Ensure(source,{64,64});operations.Ensure(crop,{32,32});
    const float color[]{.2f,.4f,.6f,1.f};context->ClearRenderTargetView(source.target.Get(),color);
    operations.GuideCrop(context.Get(),source.texture.Get(),{64,64},{8,8,32,32},crop);
#endif
#ifdef CONSUMER_NGX
    // Force resolution of the installed NGX library/SDK symbols without loading
    // a runtime: invalid device is rejected before NGX initialization.
    bool rejected=false;try{f::d3d11::CreateNgxBackend(nullptr,{});}catch(const std::runtime_error&){rejected=true;}
    if(!rejected)return 7;
#endif
    std::cout<<"PASS: engine "<<f::EngineVersion<<"; independent installed-package consumer; base contracts, extension 1 and packaged backend exports\n";
}
