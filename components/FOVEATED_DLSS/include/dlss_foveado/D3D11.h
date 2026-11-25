#pragma once
#include "Foveation.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

namespace dlss_foveado::d3d11 {
struct Image {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    // Shared only by copies of this image. Operations keeps a weak reference,
    // so its lookup never prolongs the lifetime of an obsolete texture.
    std::shared_ptr<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> viewLifetime;
};
// One device/immediate render thread per instance. Each operation executes an
// isolated deferred command list with state restoration, or a state-neutral
// exact copy for compatible floating crops; no game state capture.
class Operations {
    struct State;
    std::unique_ptr<State> state;
public:
    explicit Operations(ID3D11Device* device);
    ~Operations();
    struct ViewStatistics {uint64_t created{},reused{};size_t registered{};};
    ViewStatistics Views() const;
    Operations(const Operations&) = delete;
    Operations& operator=(const Operations&) = delete;
    void Ensure(Image& image, Extent extent, DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT);
    void Sharpen(ID3D11DeviceContext* context, ID3D11Texture2D* source, Image& destination, float strength);
    void Resample(ID3D11DeviceContext* context, ID3D11Texture2D* source, Rect sourceRegion,
                  Image& destination, bool nearest = false, Point vectorScale = {1, 1});
    // Equivalent of full nearest resize then integer crop, without full-size intermediates.
    void GuideCrop(ID3D11DeviceContext* context, ID3D11Texture2D* source, Extent fullOutput,
                   Rect outputRegion, Image& destination, Point vectorScale = {1, 1});
    void Composite(ID3D11DeviceContext* context, ID3D11Texture2D* periphery,
                   ID3D11Texture2D* central, const RegionPlan& region, Image& destination,
                   bool encodedLdr = true, Point regionScale = {1, 1});
    // Blend a live outgoing central reconstruction over the incoming one while
    // its history settles. In-place RGB blending needs no extra full-eye texture.
    void BlendRegion(ID3D11DeviceContext*,ID3D11Texture2D* central,const RegionPlan&,
                     Image& destination,float opacity);
    // Call only after the host's clean stereo checkpoint, never on history inputs.
    void Indicator(ID3D11DeviceContext* context, ID3D11Texture2D* scene, Rect viewport,
                   const RegionPlan& region, bool neural = false);
    void ColoredIndicator(ID3D11DeviceContext*,ID3D11Texture2D*,Rect,const RegionPlan&,
                          std::array<float,4> color,float thickness=1.5f);
};
} // namespace dlss_foveado::d3d11
