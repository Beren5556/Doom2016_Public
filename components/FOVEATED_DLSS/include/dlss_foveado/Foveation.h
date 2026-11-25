#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dlss_foveado {
inline constexpr uint32_t ContractVersion = 4;
inline constexpr uint32_t ConfigSchemaVersion = 3;
inline constexpr std::string_view EngineVersion = "1.3.0";
enum class GeometryMode : uint32_t { Legacy, BinocularFourSides };
enum class Shape : uint32_t { Rectangle, Ellipse };
// Direction in eye-local image coordinates; the host declares it, never a view ID heuristic.
enum class NasalSide : uint32_t { None, Left, Right };
enum class Periphery : uint32_t { Spatial, Temporal };
enum class PlacementSource : uint32_t { Fixed }; // Gaze requires a future contract/capability.
struct Extent {
    uint32_t width{}, height{};
    bool operator==(const Extent&) const = default;
};
struct Rect {
    uint32_t x{}, y{}, width{}, height{};
    bool operator==(const Rect&) const = default;
};
struct Point {
    double x{0.5}, y{0.5};
    bool operator==(const Point&) const = default;
};
struct Region {
    Shape shape{Shape::Ellipse};
    // Non-nasal views: linked dimensions use the shorter output side (circle/square).
    uint32_t widthBps{6000}, heightBps{6000}, transitionBps{1000};
    bool linkedAxes{true};
    // Nasal views form a continuous binocular oval. Width is retained coverage;
    // vertical reduction = (1 - width) * verticalRatio, distributed by upperShare.
    bool lockHeight{true};
    uint32_t verticalRatioBps{10000}, upperShareBps{5000};
    // Cuts from the full common-plane diameter, not retained coverage. Schema 3.
    uint32_t topBps{2000}, bottomBps{2000}, rightBps{2000}, leftBps{2000};
    bool operator==(const Region&) const = default;
};
struct Config {
    bool enabled{};
    Region central{};
    Periphery periphery{Periphery::Spatial};
    uint32_t peripheralScaleBps{7500}, peripheralPreset{}, centralPreset{};
    uint32_t supersampleBps{10000};
    // Eye-local normalized centers; the host supplies optical alignment separately.
    std::array<uint32_t, 2> centerXBps{5000, 5000}, centerYBps{5000, 5000};
    bool showRegion{}, neuralLinked{true};
    Region neural{};
    uint32_t indicatorRegions{}; // 0=SR, 1=NR, 2=both.
    GeometryMode geometryMode{GeometryMode::BinocularFourSides};
    bool operator==(const Config&) const = default;
    bool Valid() const noexcept;
    std::string Encode() const;
    // Explicit proposed migration, never performed by Parse or persistence.
    Config FourSideDraft() const;
    static std::optional<Config> Parse(std::string_view text) noexcept;
};
struct Placement {
    PlacementSource source{PlacementSource::Fixed};
    std::array<Point, 2> centers{};
    uint64_t revision{};
};
struct BinocularMapping {
    // Output pixel centers -> common plane [X/W,Y/W]. Row-major, no jitter/atlas.
    // Same spatial units on both axes; common center (0.5,0.5). Host supplied.
    std::array<double, 9> outputToCommon{};
    uint64_t spaceId{}, revision{};
    bool operator==(const BinocularMapping&) const = default;
};
struct ViewGeometry {
    Extent input{}, output{};
    uint64_t viewId{};
    NasalSide nasalSide{NasalSide::None};
    BinocularMapping binocular{};
    bool operator==(const ViewGeometry&) const = default;
};
struct RegionPlan {
    Point centerPixels{}, halfSizePixels{};
    double transitionPixels{};
    Shape shape{};
    Rect inputCrop{};
    Extent reconstruction{};
    // Full-eye output pixel center -> cropped feature UV. No atlas offset here.
    Point cropScale{}, cropBias{};
    NasalSide nasalSide{NasalSide::None};
    GeometryMode geometryMode{GeometryMode::Legacy};
    BinocularMapping binocular{};
    // Common-plane radii in T/B/R/L order. Only used by BinocularFourSides.
    std::array<double, 4> radii{};
    double transitionFraction{};
};
struct ViewPlan {
    ViewGeometry geometry{};
    RegionPlan central{}, neural{};
    Extent peripheral{};
};
struct FramePlan {
    uint32_t version{ContractVersion};
    uint64_t configRevision{}, frame{}, epoch{}, placementRevision{};
    std::array<ViewPlan, 2> views{};
    Config config{};
};
struct Limits { uint32_t minimumInput{32}, maximumExtent{16384}; };
Placement FixedPlacement(const Config& config, uint64_t revision = 0);
FramePlan Plan(const Config& config, const std::array<ViewGeometry, 2>& geometry,
               const Placement& placement, uint64_t revision, uint64_t frame,
               uint64_t epoch, Limits limits = {});
// Weight of central reconstruction at an output pixel center. Outside the
// fully weighted region, transition fades to the periphery. Same shape as UI.
double CentralWeight(const RegionPlan& region, Point outputPixel) noexcept;
// Future moving placement oracle. Input-pixel units, previous-minus-current MV.
Point CropMotion(Point fullEyeMotion, Point currentOrigin, Point previousOrigin) noexcept;
} // namespace dlss_foveado
