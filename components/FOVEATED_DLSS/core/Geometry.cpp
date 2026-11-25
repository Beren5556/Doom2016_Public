#include <dlss_foveado/Foveation.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace dlss_foveado {
namespace {
void need(bool value, const char* message) { if (!value) throw std::invalid_argument(message); }
bool valid(Extent e, Limits l) { return e.width >= l.minimumInput && e.height >= l.minimumInput &&
    e.width <= l.maximumExtent && e.height <= l.maximumExtent; }
using Matrix = std::array<double, 9>;
Point commonPoint(const Matrix& h, Point p) {
    const double w = h[6]*p.x + h[7]*p.y + h[8];
    return {(h[0]*p.x+h[1]*p.y+h[2])/w, (h[3]*p.x+h[4]*p.y+h[5])/w};
}
BinocularMapping mapping(const ViewGeometry& g) {
    auto m = g.binocular; auto& h = m.outputToCommon;
    need(m.spaceId && m.revision, "missing binocular projection identity");
    for (auto v : h) need(std::isfinite(v), "non-finite binocular projection");
    const double w = h[6]*g.output.width*.5 + h[7]*g.output.height*.5 + h[8];
    need(std::isfinite(w) && std::abs(w) > 1e-12, "invalid binocular projection scale");
    for (auto& v : h) { v /= w; need(std::isfinite(v) && std::abs(v) <= 1e6, "unbounded binocular projection"); }
    auto n = h;
    for (unsigned row=0; row<3; ++row) { n[row*3] *= g.output.width; n[row*3+1] *= g.output.height; }
    double norm = 0; for (auto v : n) norm = std::max(norm, std::abs(v));
    const double determinant = n[0]*(n[4]*n[8]-n[5]*n[7]) - n[1]*(n[3]*n[8]-n[5]*n[6]) + n[2]*(n[3]*n[7]-n[4]*n[6]);
    need(std::abs(determinant) > 1e-10*norm*norm*norm, "degenerate binocular projection");
    for (double y : {0., double(g.output.height)}) for (double x : {0., double(g.output.width)}) {
        need(h[6]*x+h[7]*y+h[8] >= 1e-4, "binocular projection crosses horizon");
        const auto p = commonPoint(h, {x,y});
        need(std::abs(p.x) <= 1e4 && std::abs(p.y) <= 1e4, "unbounded binocular coordinates");
    }
    return m;
}
// Intersect the eye rectangle with the four common-plane bounding half-planes.
// A projective map with positive W keeps these linear in output pixel space.
// This remains conservative even when the inverse bounding box crosses a horizon.
std::array<double,4> projectedBounds(const RegionPlan& p, Extent output) {
    std::vector<Point> polygon{{0,0},{double(output.width),0},
        {double(output.width),double(output.height)},{0,double(output.height)}};
    const auto& h = p.binocular.outputToCommon;
    const double e = 1 + p.transitionFraction;
    const std::array<double,4> edges{.5-p.radii[3]*e, .5+p.radii[2]*e,
                                    .5-p.radii[0]*e, .5+p.radii[1]*e};
    for (unsigned edge=0; edge<4; ++edge) {
        const unsigned row = edge<2 ? 0 : 3;
        const double sign = edge%2 ? -1. : 1.;
        const auto distance = [&](Point v) { return sign*((h[row]-edges[edge]*h[6])*v.x+
            (h[row+1]-edges[edge]*h[7])*v.y+h[row+2]-edges[edge]*h[8]); };
        std::vector<Point> clipped;
        if (!polygon.empty()) {
            auto a=polygon.back(); double da=distance(a);
            for (auto b : polygon) {
                const double db=distance(b);
                if ((da>=0)!=(db>=0)) {
                    const double t=da/(da-db);
                    clipped.push_back({a.x+t*(b.x-a.x),a.y+t*(b.y-a.y)});
                }
                if (db>=0) clipped.push_back(b);
                a=b; da=db;
            }
        }
        polygon=std::move(clipped);
    }
    need(!polygon.empty(), "foveated region is outside the binocular view");
    std::array<double,4> b{double(output.width),double(output.height),0,0};
    for (auto v : polygon) { b[0]=std::min(b[0],v.x); b[1]=std::min(b[1],v.y);
        b[2]=std::max(b[2],v.x); b[3]=std::max(b[3],v.y); }
    return b;
}
RegionPlan makeFourSideRegion(const Region& r, const ViewGeometry& g, uint32_t supersample, Limits l) {
    RegionPlan p; p.shape=r.shape; p.geometryMode=GeometryMode::BinocularFourSides;
    p.binocular=mapping(g); p.radii={.5-r.topBps/10000., .5-r.bottomBps/10000.,
                                     .5-r.rightBps/10000., .5-r.leftBps/10000.};
    p.transitionFraction=r.transitionBps/10000.;
    const auto b=projectedBounds(p,g.output);
    const double sx=double(g.input.width)/g.output.width, sy=double(g.input.height)/g.output.height;
    // One input texel guard covers float shader rounding at the outer boundary.
    const auto lo=[](double v,uint32_t extent){return uint32_t(std::floor(std::clamp(v-1.,0.,double(extent))));};
    const auto hi=[](double v,uint32_t extent){return uint32_t(std::ceil(std::clamp(v+1.,0.,double(extent))));};
    const uint32_t x=lo(b[0]*sx,g.input.width),y=lo(b[1]*sy,g.input.height);
    const uint32_t right=hi(b[2]*sx,g.input.width),bottom=hi(b[3]*sy,g.input.height);
    need(right>x && bottom>y,"empty binocular crop");
    p.inputCrop={x,y,right-x,bottom-y};
    need(valid({right-x,bottom-y},l),"foveated input outside backend limits");
    const double factor=supersample/10000.;
    p.reconstruction={uint32_t(std::ceil((right-x)/sx*factor)),uint32_t(std::ceil((bottom-y)/sy*factor))};
    need(valid(p.reconstruction,l),"foveated reconstruction outside backend limits");
    p.cropScale={sx/(right-x),sy/(bottom-y)}; p.cropBias={-double(x)/(right-x),-double(y)/(bottom-y)};
    return p;
}
RegionPlan makeRegion(const Region& r, const ViewGeometry& g, Point center, uint32_t supersample, Limits l) {
    RegionPlan p{}; p.shape = r.shape; p.nasalSide = g.nasalSide;
    p.centerPixels = {center.x * g.output.width, center.y * g.output.height};
    const double basis = std::min(g.output.width, g.output.height);
    p.halfSizePixels = r.linkedAxes ? Point{basis * r.widthBps / 20000., basis * r.widthBps / 20000.} :
        Point{g.output.width * r.widthBps / 20000., g.output.height * r.heightBps / 20000.};
    if (g.nasalSide != NasalSide::None) {
        // Each eye supplies an open half of the oval, anchored at its nasal edge.
        // There is no union with a half-plane and no vertical seam in the mask.
        const double reduction = r.lockHeight ? 0. :
            (1. - r.widthBps / 10000.) * (r.verticalRatioBps / 10000.);
        const double top = reduction * (r.upperShareBps / 10000.);
        const double bottom = reduction - top;
        p.centerPixels = {(g.nasalSide == NasalSide::Right ? 1. : 0.) * g.output.width +
            (center.x - .5) * g.output.width,
            ((1. + top - bottom) * .5 + (r.lockHeight ? 0. : center.y - .5)) * g.output.height};
        p.halfSizePixels = {g.output.width * r.widthBps / 10000.,
            g.output.height * (1. - reduction) * .5};
    }
    p.transitionPixels = std::min(p.halfSizePixels.x, p.halfSizePixels.y) * r.transitionBps / 10000.;
    const double sx = double(g.input.width) / g.output.width, sy = double(g.input.height) / g.output.height;
    const auto lo = [](double v, uint32_t extent) { return uint32_t(std::floor(std::clamp(v, 0., double(extent)))); };
    const auto hi = [](double v, uint32_t extent) { return uint32_t(std::ceil(std::clamp(v, 0., double(extent)))); };
    const double expansion = 1. + r.transitionBps / 10000.;
    const double radiusX = r.shape == Shape::Ellipse ? p.halfSizePixels.x * expansion : p.halfSizePixels.x + p.transitionPixels;
    const double radiusY = r.shape == Shape::Ellipse ? p.halfSizePixels.y * expansion : p.halfSizePixels.y + p.transitionPixels;
    uint32_t x = lo((p.centerPixels.x - radiusX) * sx, g.input.width);
    uint32_t y = lo((p.centerPixels.y - radiusY) * sy, g.input.height);
    uint32_t right = hi((p.centerPixels.x + radiusX) * sx, g.input.width);
    uint32_t bottom = hi((p.centerPixels.y + radiusY) * sy, g.input.height);
    need(right > x && bottom > y, "foveated region is outside the view");
    p.inputCrop = {x, y, right - x, bottom - y};
    need(valid({p.inputCrop.width, p.inputCrop.height}, l), "foveated input outside backend limits");
    const double factor = supersample / 10000.;
    p.reconstruction = {uint32_t(std::ceil(p.inputCrop.width / sx * factor)),
                        uint32_t(std::ceil(p.inputCrop.height / sy * factor))};
    need(valid(p.reconstruction, l), "foveated reconstruction outside backend limits");
    p.cropScale = {sx / p.inputCrop.width, sy / p.inputCrop.height};
    p.cropBias = {-double(x) / p.inputCrop.width, -double(y) / p.inputCrop.height};
    return p;
}
}
Placement FixedPlacement(const Config& c, uint64_t revision) {
    need(c.Valid(), "invalid foveation configuration");
    Placement p{}; p.revision = revision;
    if (c.geometryMode == GeometryMode::Legacy)
        for (size_t eye = 0; eye < 2; ++eye) p.centers[eye] = {c.centerXBps[eye] / 10000., c.centerYBps[eye] / 10000.};
    return p;
}
FramePlan Plan(const Config& c, const std::array<ViewGeometry, 2>& geometry, const Placement& placement,
               uint64_t revision, uint64_t frame, uint64_t epoch, Limits limits) {
    need(c.Valid() && revision && frame && epoch, "invalid foveation frame identity/configuration");
    need(limits.minimumInput && limits.minimumInput <= limits.maximumExtent && limits.maximumExtent <= 16384,
        "invalid backend limits");
    need(placement.source == PlacementSource::Fixed, "unsupported placement source");
    need(geometry[0].viewId != geometry[1].viewId, "stereo views must have distinct identities");
    if (c.geometryMode == GeometryMode::BinocularFourSides)
        need(geometry[0].binocular.spaceId == geometry[1].binocular.spaceId &&
             geometry[0].binocular.revision == geometry[1].binocular.revision, "binocular projection pair mismatch");
    FramePlan result{}; result.configRevision = revision; result.frame = frame; result.epoch = epoch;
    result.placementRevision = placement.revision; result.config = c;
    for (size_t eye = 0; eye < 2; ++eye) {
        const auto& g = geometry[eye]; const auto center = placement.centers[eye];
        need(uint32_t(g.nasalSide) <= uint32_t(NasalSide::Right), "invalid nasal side");
        need(valid(g.input, limits) && valid(g.output, limits), "view extent outside backend limits");
        need(g.input.width <= g.output.width && g.input.height <= g.output.height, "input exceeds output");
        need(std::isfinite(center.x) && std::isfinite(center.y) && center.x >= 0 && center.x <= 1 &&
            center.y >= 0 && center.y <= 1, "invalid fixed center");
        auto& v = result.views[eye]; v.geometry = g;
        if (c.geometryMode == GeometryMode::BinocularFourSides) {
            need(center == Point{}, "four-side placement must remain fixed at the common center");
            v.central = makeFourSideRegion(c.central, g, c.supersampleBps, limits);
            v.neural = makeFourSideRegion(c.neuralLinked ? c.central : c.neural, g, 10000, limits);
        } else {
            v.central = makeRegion(c.central, g, center, c.supersampleBps, limits);
            v.neural = makeRegion(c.neuralLinked ? c.central : c.neural, g, center, 10000, limits);
        }
        v.peripheral = {uint32_t(std::lround(g.input.width * (c.peripheralScaleBps / 10000.))),
                        uint32_t(std::lround(g.input.height * (c.peripheralScaleBps / 10000.)))};
        need(valid(v.peripheral, limits), "peripheral extent outside backend limits");
    }
    return result;
}
double CentralWeight(const RegionPlan& r, Point outputPixel) noexcept {
    if (r.geometryMode == GeometryMode::BinocularFourSides) {
        const auto p=commonPoint(r.binocular.outputToCommon,outputPixel);
        const double rx=r.radii[p.x<.5?3:2], ry=r.radii[p.y<.5?0:1];
        if (!(rx>0 && ry>0) || !std::isfinite(p.x) || !std::isfinite(p.y)) return 0;
        const double x=std::abs(p.x-.5)/rx,y=std::abs(p.y-.5)/ry;
        const double q=r.shape==Shape::Ellipse?std::hypot(x,y):std::max(x,y);
        if (q<=1) return 1;
        if (r.transitionFraction<=0) return 0;
        const double t=std::clamp((q-1)/r.transitionFraction,0.,1.);
        return 1-t*t*(3-2*t);
    }
    const double x = std::abs(outputPixel.x - r.centerPixels.x), y = std::abs(outputPixel.y - r.centerPixels.y);
    if (r.halfSizePixels.x <= 0 || r.halfSizePixels.y <= 0) return 0;
    double distance;
    if (r.shape == Shape::Ellipse) {
        const double q = std::hypot(x / r.halfSizePixels.x, y / r.halfSizePixels.y);
        distance = (q - 1.) * std::min(r.halfSizePixels.x, r.halfSizePixels.y);
    } else distance = std::max(x - r.halfSizePixels.x, y - r.halfSizePixels.y);
    if (distance <= 0) return 1;
    if (r.transitionPixels <= 0) return 0;
    const double t = std::clamp(distance / r.transitionPixels, 0., 1.);
    return 1. - t * t * (3. - 2. * t);
}
Point CropMotion(Point motion, Point current, Point previous) noexcept {
    return {motion.x + current.x - previous.x, motion.y + current.y - previous.y};
}
} // namespace dlss_foveado
