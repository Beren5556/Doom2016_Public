#include <dlss_foveado/D3D11.h>
#include "ShaderSource.h"
#include <d3dcompiler.h>
#include <array>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace dlss_foveado::d3d11 {
template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
namespace {
void check(HRESULT result, const char* what) { if (FAILED(result)) throw std::runtime_error(what); }
void need(bool condition, const char* what) { if (!condition) throw std::invalid_argument(what); }
DXGI_FORMAT viewFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32G32_FLOAT: case DXGI_FORMAT_R16G16_FLOAT: return format;
    default: throw std::invalid_argument("unsupported foveation texture format");
    }
}
struct Parameters { std::array<float, 4> sourceRect{}, sizes{}, crop{}, centerHalf{}, options{}, componentScale{1, 1, 1, 1};
    std::array<float, 4> commonX{}, commonY{}, commonW{}, radii{}; };
static_assert(sizeof(Parameters) == 160);
void regionParameters(Parameters& p, const RegionPlan& r) {
    p.crop = {float(r.cropScale.x), float(r.cropScale.y), float(r.cropBias.x), float(r.cropBias.y)};
    p.centerHalf = {float(r.centerPixels.x), float(r.centerPixels.y), float(r.halfSizePixels.x), float(r.halfSizePixels.y)};
    p.options[0] = r.shape == Shape::Ellipse ? 1.f : 0.f;
    p.options[1] = float(r.transitionPixels);
    if (r.geometryMode == GeometryMode::BinocularFourSides) {
        const auto& h=r.binocular.outputToCommon;
        p.commonX={float(h[0]),float(h[1]),float(h[2]),1};
        p.commonY={float(h[3]),float(h[4]),float(h[5]),0};
        p.commonW={float(h[6]),float(h[7]),float(h[8]),0};
        for(unsigned i=0;i<4;++i)p.radii[i]=float(r.radii[i]);
        p.options[1]=float(r.transitionFraction);
    }
}
}
struct Operations::State {
    Ptr<ID3D11Device> device;
    Ptr<ID3D11DeviceContext> deferred;
    Ptr<ID3D11VertexShader> vertex;
    Ptr<ID3D11PixelShader> resample, composite, indicator, sharpen, guideCrop, blendRegion;
    Ptr<ID3D11Buffer> buffer;
    Ptr<ID3D11SamplerState> sampler;
    Ptr<ID3D11RasterizerState> raster;
    Ptr<ID3D11DepthStencilState> depth;
    Ptr<ID3D11BlendState> blend;
    std::unordered_map<ID3D11Texture2D*,std::weak_ptr<Ptr<ID3D11ShaderResourceView>>> ownedViews;
    uint64_t createdViews{},reusedViews{};
    explicit State(ID3D11Device* d) : device(d) {
        need(d != nullptr, "missing foveation device");
        check(d->CreateDeferredContext(0, &deferred), "foveation deferred context");
        auto compile = [&](const char* entry, const char* target) {
            Ptr<ID3DBlob> code, error;
            const auto result = D3DCompile(ShaderSource, std::strlen(ShaderSource), "DLSS_FOVEADO/Operations.hlsl", nullptr,
                nullptr, entry, target, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);
            if (FAILED(result)) throw std::runtime_error(error ? static_cast<const char*>(error->GetBufferPointer()) : "foveation shader compilation failed");
            return code;
        };
        auto code = compile("VS", "vs_5_0"); check(d->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vertex), "foveation VS");
        auto ps = [&](const char* entry, Ptr<ID3D11PixelShader>& shader) { auto blob = compile(entry, "ps_5_0");
            check(d->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader), "foveation PS"); };
        ps("Resample", resample); ps("Composite", composite); ps("Indicator", indicator);
        ps("Sharpen", sharpen);
        ps("GuideCrop", guideCrop);
        ps("BlendRegion", blendRegion);
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth = sizeof(Parameters); cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check(d->CreateBuffer(&cb, nullptr, &buffer), "foveation constants");
        D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
        check(d->CreateSamplerState(&sd, &sampler), "foveation sampler");
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; rd.ScissorEnable = TRUE;
        check(d->CreateRasterizerState(&rd, &raster), "foveation rasterizer");
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
        check(d->CreateDepthStencilState(&dd, &depth), "foveation depth state");
        D3D11_BLEND_DESC bd{}; auto& b = bd.RenderTarget[0]; b.BlendEnable = TRUE;
        b.SrcBlend = D3D11_BLEND_SRC_ALPHA; b.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; b.BlendOp = D3D11_BLEND_OP_ADD;
        b.SrcBlendAlpha = D3D11_BLEND_ONE; b.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; b.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        b.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        check(d->CreateBlendState(&bd, &blend), "foveation indicator blend");
    }
    D3D11_TEXTURE2D_DESC describe(ID3D11Texture2D* texture) const {
        need(texture != nullptr, "missing foveation texture");
        Ptr<ID3D11Device> owner; texture->GetDevice(&owner); need(owner.Get() == device.Get(), "foveation texture device mismatch");
        D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d);
        need(d.MipLevels == 1 && d.ArraySize == 1 && d.SampleDesc.Count == 1, "foveation texture must be a single non-MSAA image");
        (void)viewFormat(d.Format); return d;
    }
    Ptr<ID3D11ShaderResourceView> view(ID3D11Texture2D* texture) {
        const auto d = describe(texture); need((d.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0, "foveation input requires SRV binding");
        if(auto it=ownedViews.find(texture);it!=ownedViews.end()) {
            if(auto existing=it->second.lock()){++reusedViews;return *existing;}
            ownedViews.erase(it);
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{}; desc.Format = viewFormat(d.Format); desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipLevels = 1; Ptr<ID3D11ShaderResourceView> result;
        check(device->CreateShaderResourceView(texture, &desc, &result), "foveation SRV");++createdViews;return result;
    }
    void draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, Rect area,
              ID3D11PixelShader* shader, const Parameters& parameters,
              ID3D11ShaderResourceView* input0 = nullptr, ID3D11ShaderResourceView* input1 = nullptr, bool alpha = false) {
        need(context && context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE, "foveation needs immediate execution context");
        Ptr<ID3D11Device> owner; context->GetDevice(&owner); need(owner.Get() == device.Get(), "foveation context device mismatch");
        deferred->ClearState(); deferred->UpdateSubresource(buffer.Get(), 0, nullptr, &parameters, 0, 0);
        deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        deferred->VSSetShader(vertex.Get(), nullptr, 0); deferred->PSSetShader(shader, nullptr, 0);
        auto* cb = buffer.Get(); deferred->PSSetConstantBuffers(0, 1, &cb);
        ID3D11ShaderResourceView* inputs[]{input0, input1}; deferred->PSSetShaderResources(0, 2, inputs);
        auto* ss = sampler.Get(); deferred->PSSetSamplers(0, 1, &ss);
        deferred->OMSetRenderTargets(1, &target, nullptr); deferred->OMSetDepthStencilState(depth.Get(), 0);
        deferred->OMSetBlendState(alpha ? blend.Get() : nullptr, nullptr, 0xffffffff);
        deferred->RSSetState(raster.Get());
        const D3D11_VIEWPORT vp{float(area.x), float(area.y), float(area.width), float(area.height), 0, 1};
        const D3D11_RECT scissor{LONG(area.x), LONG(area.y), LONG(area.x + area.width), LONG(area.y + area.height)};
        deferred->RSSetViewports(1, &vp); deferred->RSSetScissorRects(1, &scissor); deferred->Draw(3, 0);
        Ptr<ID3D11CommandList> commands; check(deferred->FinishCommandList(FALSE, &commands), "foveation command list");
        context->ExecuteCommandList(commands.Get(), TRUE);
        check(device->GetDeviceRemovedReason(), "foveation GPU device removed");
    }
};
Operations::Operations(ID3D11Device* device) : state(std::make_unique<State>(device)) {}
Operations::~Operations() = default;
Operations::ViewStatistics Operations::Views()const{return {state->createdViews,state->reusedViews,state->ownedViews.size()};}
void Operations::Sharpen(ID3D11DeviceContext* context,ID3D11Texture2D* source,Image& destination,float strength){
    const auto src=state->describe(source),dst=state->describe(destination.texture.Get());
    need(source!=destination.texture.Get()&&destination.target&&src.Width==dst.Width&&src.Height==dst.Height&&
        std::isfinite(strength)&&strength>=0&&strength<=1,"invalid foveation sharpness contract");
    Parameters p;p.sizes={float(src.Width),float(src.Height),0,0};p.options[0]=strength;
    auto input=state->view(source);state->draw(context,destination.target.Get(),{0,0,dst.Width,dst.Height},state->sharpen.Get(),p,input.Get());
}
void Operations::Ensure(Image& image, Extent extent, DXGI_FORMAT format) {
    need(extent.width && extent.height && extent.width <= 16384 && extent.height <= 16384, "invalid foveation image extent");
    (void)viewFormat(format);
    if (image.texture) { const auto d = state->describe(image.texture.Get());
        if (d.Width == extent.width && d.Height == extent.height && d.Format == format && image.view && image.target) return; }
    Image next;
    D3D11_TEXTURE2D_DESC d{}; d.Width = extent.width; d.Height = extent.height; d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
    d.Format = format; d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    // NGX output needs UAV. Depth/MV preparation does not need typed UAV support.
    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) d.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    check(state->device->CreateTexture2D(&d, nullptr, &next.texture), "foveation texture allocation");
    next.view = state->view(next.texture.Get());
    for(auto it=state->ownedViews.begin();it!=state->ownedViews.end();)if(it->second.expired())it=state->ownedViews.erase(it);else ++it;
    next.viewLifetime=std::make_shared<Ptr<ID3D11ShaderResourceView>>(next.view);
    state->ownedViews[next.texture.Get()]=next.viewLifetime;
    check(state->device->CreateRenderTargetView(next.texture.Get(), nullptr, &next.target), "foveation RTV"); image = std::move(next);
}
void Operations::Resample(ID3D11DeviceContext* context, ID3D11Texture2D* source, Rect region,
                           Image& destination, bool nearest, Point scale) {
    const auto src = state->describe(source), dst = state->describe(destination.texture.Get());
    need(source != destination.texture.Get() && destination.target, "foveation source aliases target");
    need(region.width && region.height && uint64_t(region.x) + region.width <= src.Width &&
        uint64_t(region.y) + region.height <= src.Height, "foveation source rectangle out of range");
    // A same-format, one-texel-to-one-texel crop needs no sampling shader or
    // temporary pipeline state. Copy the exact texels (including depth/motion).
    // Never take this path for scaling, format conversion, sRGB views or motion
    // vector unit conversion: those still require the shader below.
    const bool linearFloat = src.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
        src.Format == DXGI_FORMAT_R32_FLOAT || src.Format == DXGI_FORMAT_R32G32_FLOAT ||
        src.Format == DXGI_FORMAT_R32G32B32A32_FLOAT;
    if (linearFloat && src.Format == dst.Format && src.SampleDesc.Count == 1 &&
        dst.SampleDesc.Count == 1 && region.width == dst.Width && region.height == dst.Height &&
        scale.x == 1 && scale.y == 1) {
        const D3D11_BOX box{region.x, region.y, 0, region.x + region.width, region.y + region.height, 1};
        context->CopySubresourceRegion(destination.texture.Get(), 0, 0, 0, 0, source, 0, &box);
        return;
    }
    Parameters p; p.sourceRect = {float(region.x), float(region.y), float(region.width), float(region.height)};
    p.sizes = {float(src.Width), float(src.Height), float(dst.Width), float(dst.Height)};
    p.options[0] = nearest ? 1.f : 0.f; p.componentScale = {float(scale.x), float(scale.y), 1, 1};
    auto input = state->view(source); state->draw(context, destination.target.Get(), {0, 0, dst.Width, dst.Height}, state->resample.Get(), p, input.Get());
}
void Operations::GuideCrop(ID3D11DeviceContext* context, ID3D11Texture2D* source,
                          Extent fullOutput, Rect region, Image& destination, Point scale) {
    const auto src=state->describe(source),dst=state->describe(destination.texture.Get());
    need(source!=destination.texture.Get() && destination.target && fullOutput.width && fullOutput.height &&
        region.width==dst.Width && region.height==dst.Height &&
        uint64_t(region.x)+region.width<=fullOutput.width && uint64_t(region.y)+region.height<=fullOutput.height,
        "invalid experimental guide crop");
    Parameters p; p.sourceRect={float(region.x),float(region.y),float(region.width),float(region.height)};
    p.sizes={float(src.Width),float(src.Height),float(fullOutput.width),float(fullOutput.height)};
    p.componentScale={float(scale.x),float(scale.y),1,1};
    auto input=state->view(source);
    state->draw(context,destination.target.Get(),{0,0,dst.Width,dst.Height},state->guideCrop.Get(),p,input.Get());
}
void Operations::Composite(ID3D11DeviceContext* context, ID3D11Texture2D* periphery,
                            ID3D11Texture2D* central, const RegionPlan& region, Image& destination, bool encodedLdr, Point regionScale) {
    const auto dst = state->describe(destination.texture.Get());
    need(destination.target && destination.texture.Get() != periphery && destination.texture.Get() != central,
        "foveation composite aliases input");
    auto a = state->view(periphery), b = state->view(central); Parameters p;
    p.sizes = {0, 0, float(dst.Width), float(dst.Height)}; regionParameters(p, region); p.options[2] = encodedLdr ? 1.f : 0.f;
    p.componentScale[0] = float(regionScale.x); p.componentScale[1] = float(regionScale.y);
    state->draw(context, destination.target.Get(), {0, 0, dst.Width, dst.Height}, state->composite.Get(), p, a.Get(), b.Get());
}
void Operations::BlendRegion(ID3D11DeviceContext* context,ID3D11Texture2D* central,
                            const RegionPlan& region,Image& destination,float opacity){
    const auto dst=state->describe(destination.texture.Get());
    need(destination.target&&destination.texture.Get()!=central&&std::isfinite(opacity)&&opacity>=0&&opacity<=1,
        "invalid dynamic region blend");
    Parameters p;regionParameters(p,region);p.options[3]=opacity;
    auto input=state->view(central);
    state->draw(context,destination.target.Get(),{0,0,dst.Width,dst.Height},state->blendRegion.Get(),p,input.Get(),nullptr,true);
}
void Operations::Indicator(ID3D11DeviceContext* context, ID3D11Texture2D* scene, Rect viewport,
                            const RegionPlan& region, bool neural) {
    ColoredIndicator(context,scene,viewport,region,neural?std::array<float,4>{1,.4f,.8f,.8f}:std::array<float,4>{0,1,1,.8f});
}
void Operations::ColoredIndicator(ID3D11DeviceContext* context,ID3D11Texture2D* scene,Rect viewport,
                                  const RegionPlan& region,std::array<float,4> color,float thickness){
    const auto d = state->describe(scene);
    need(viewport.width && viewport.height && uint64_t(viewport.x) + viewport.width <= d.Width &&
        uint64_t(viewport.y) + viewport.height <= d.Height, "indicator viewport outside scene");
    D3D11_RENDER_TARGET_VIEW_DESC desc{}; desc.Format = viewFormat(d.Format); desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    Ptr<ID3D11RenderTargetView> target; check(state->device->CreateRenderTargetView(scene, &desc, &target), "indicator scene RTV");
    Parameters p; regionParameters(p, region); p.sourceRect = {float(viewport.x), float(viewport.y), 0, 0};
    need(std::isfinite(thickness)&&thickness>0&&thickness<=8,"invalid contour thickness");
    p.options[3] = thickness; p.componentScale = color;
    state->draw(context, target.Get(), viewport, state->indicator.Get(), p, nullptr, nullptr, true);
}
} // namespace dlss_foveado::d3d11
