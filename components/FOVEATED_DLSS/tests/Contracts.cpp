#include <dlss_foveado/Foveation.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace dlss_foveado;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F f) { bool failed = false; try { f(); } catch (const std::invalid_argument&) { failed = true; } check(failed, "invalid contract accepted"); }
int main() try {
    Config config; config.geometryMode = GeometryMode::Legacy; config.enabled = true;
    check(Config::Parse(config.Encode()) == config, "configuration round trip");
    check(!Config::Parse(config.Encode() + ";0"), "trailing fields accepted");
    check(!Config::Parse("4294967296"), "integer overflow accepted");
    auto bad = config.Encode(); bad.replace(0, 1, "9"); check(!Config::Parse(bad), "unknown version accepted");
    auto invalid = config; invalid.peripheralPreset = 5; check(!invalid.Valid(), "unvalidated preset accepted");
    const std::array<ViewGeometry, 2> geometry{{{{1024, 1024}, {2048, 2048}, 1}, {{1025, 1023}, {2050, 2046}, 2}}};
    auto p = Plan(config, geometry, FixedPlacement(config), 1, 1, 1);
    check(p.views[0].central.inputCrop.width < 1024, "no actual crop");
    for (const auto& v : p.views) {
        const auto& r = v.central;
        check(r.halfSizePixels.x == r.halfSizePixels.y, "linked circle distorted on asymmetric eye");
        check(CentralWeight(r, r.centerPixels) == 1, "center weight");
        check(CentralWeight(r, {r.centerPixels.x + r.halfSizePixels.x + r.transitionPixels + 1, r.centerPixels.y}) == 0, "peripheral weight");
        const Point cropOrigin{r.inputCrop.x * double(v.geometry.output.width) / v.geometry.input.width,
                               r.inputCrop.y * double(v.geometry.output.height) / v.geometry.input.height};
        check(std::abs(cropOrigin.x * r.cropScale.x + r.cropBias.x) < 1e-12, "crop transform x");
        check(std::abs(cropOrigin.y * r.cropScale.y + r.cropBias.y) < 1e-12, "crop transform y");
    }
    check(p.views[0].central.reconstruction != p.views[1].central.reconstruction, "eyes incorrectly share extents");
    const auto original = p;
    config.centerXBps[0] = 0; config.centerYBps[1] = 10000;
    p = Plan(config, geometry, FixedPlacement(config), 2, 2, 1);
    check(p.views[0].central.inputCrop.x == 0, "left boundary");
    const auto& r = p.views[1].central.inputCrop;
    check(uint64_t(r.y) + r.height == geometry[1].input.height, "bottom boundary");
    check(original.configRevision == 1 && original.views[0].central.centerPixels.x == 1024, "plan is not immutable by value");
    auto duplicate = geometry; duplicate[1].viewId = 1;
    rejects([&] { Plan(config, duplicate, FixedPlacement(config), 1, 1, 1); });
    auto placement = FixedPlacement(config); placement.centers[0].x = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { Plan(config, geometry, placement, 1, 1, 1); });
    config = {}; config.geometryMode = GeometryMode::Legacy; config.supersampleBps = 20000;
    const std::array<ViewGeometry, 2> huge{{{{8192, 8192}, {16384, 16384}, 1}, {{8192, 8192}, {16384, 16384}, 2}}};
    config.central.widthBps = 10000;
    rejects([&] { Plan(config, huge, FixedPlacement(config), 1, 1, 1); });
    const auto mv = CropMotion({-3, 4}, {20, 2}, {10, 5});
    check(mv.x == 7 && mv.y == 1, "crop-motion analytical oracle");
    // The old 24-field schema migrates in memory; new controls round-trip in v2.
    const auto migrated=Config::Parse("1;1;1;8000;6000;1000;1;0;7500;0;0;10000;5000;5000;5000;5000;1;1;1;6000;6000;1000;1;0");
    check(migrated && migrated->enabled && migrated->central.widthBps==8000 && migrated->central.lockHeight,
        "legacy fixed settings did not migrate with height locked");
    check(ConfigSchemaVersion == 3 && config.Encode().starts_with("2;"), "legacy configuration was silently converted");
    check(!Config::Parse(config.Encode()+";") && !Config::Parse(";"),"malformed schema accepted");
    config = {}; config.geometryMode = GeometryMode::Legacy; config.enabled = true; config.central.widthBps = 4000;
    config.central.heightBps = 5000; config.central.linkedAxes = false;
    config.neuralLinked = false; config.neural.widthBps = 3000;
    const std::array<ViewGeometry,2> protectedViews{{
        {{256,128},{512,256},37,NasalSide::Right},{{256,128},{512,256},12,NasalSide::Left}}};
    for (const auto shape : {Shape::Ellipse,Shape::Rectangle}) for (bool locked : {false,true}) {
        config.central.shape = config.neural.shape = shape;
        config.central.lockHeight=config.neural.lockHeight=locked;
        config.central.verticalRatioBps=9000;config.central.upperShareBps=7000;
        config.neural.verticalRatioBps=6000;config.neural.upperShareBps=2000;
        check(Config::Parse(config.Encode())==config,"new regional settings failed round trip");
        const auto nasal = Plan(config,protectedViews,FixedPlacement(config),3,3,3);
        for (unsigned eye=0;eye<2;++eye) for (const auto* region : {&nasal.views[eye].central,&nasal.views[eye].neural}) {
            const auto& v=nasal.views[eye]; const auto& mask=*region;
            if(locked)check(mask.inputCrop.y==0 && mask.inputCrop.height==v.geometry.input.height,"locked oval crop lost height");
            check(eye ? mask.inputCrop.x==0 : mask.inputCrop.x+mask.inputCrop.width==v.geometry.input.width,"nasal crop missed inner edge");
            for (unsigned y=0;y<256;y+=7) for (unsigned x=0;x<512;x+=7) {
                const Point point{x+.5,y+.5}; const auto w=CentralWeight(mask,point);
                if(w>0) {
                    const auto u=point.x*mask.cropScale.x+mask.cropBias.x, t=point.y*mask.cropScale.y+mask.cropBias.y;
                    check(u>=0 && u<=1 && t>=0 && t<=1,"positive mask samples outside actual reconstruction");
                }
            }
            check(CentralWeight(mask,{eye?511.5:.5,mask.centerPixels.y})==0,"outer edge no longer foveated");
        }
        for(unsigned y=0;y<256;y+=5) for(unsigned x=0;x<512;x+=5)
            check(std::abs(CentralWeight(nasal.views[0].central,{x+.5,y+.5})-
                           CentralWeight(nasal.views[1].central,{511.5-x,y+.5}))<1e-10,"two eyes are not mirrored");
    }
    // Independent drawing oracle: 80% horizontal coverage, 90% of its reduction
    // vertically, split 70/30. Height = 82%, top = 12.6%, bottom = 5.4%.
    config.central={};config.central.widthBps=8000;config.central.lockHeight=false;
    config.central.verticalRatioBps=9000;config.central.upperShareBps=7000;config.central.transitionBps=0;
    auto oval=Plan(config,protectedViews,FixedPlacement(config),4,4,4).views[0].central;
    check(std::abs(oval.halfSizePixels.x-409.6)<1e-9 && std::abs(oval.halfSizePixels.y-104.96)<1e-9,
        "80/90 example size differs from drawing");
    check(std::abs(oval.centerPixels.y-137.216)<1e-9,"70/30 distribution wrong");
    check(CentralWeight(oval,{511.5,1})==0 && CentralWeight(oval,{511.5,255})==0,
        "unlocked height still protects entire nasal half-plane");
    // Above/below the oval are peripheral even on the former straight seam.
    check(CentralWeight(oval,{256,1})==0 && CentralWeight(oval,{256,250})==0,"old center-X seam survives");
    check(CentralWeight(oval,{511.5,137})==1,"nasal center lost detail");
    for(uint32_t ratio:{0u,9000u,10000u}) for(uint32_t share:{0u,5000u,10000u}){
        config.central.verticalRatioBps=ratio;config.central.upperShareBps=share;
        auto m=Plan(config,protectedViews,FixedPlacement(config),5,5,5).views[0].central;
        const double removed=256*.2*ratio/10000.;
        check(std::abs(m.halfSizePixels.y*2-(256-removed))<1e-9,"relative vertical reduction wrong");
        check(std::abs(m.centerPixels.y-m.halfSizePixels.y-removed*share/10000.)<1e-9,"upper/lower extrema wrong");
    }
    config.central.lockHeight=true;
    oval=Plan(config,protectedViews,FixedPlacement(config),6,6,6).views[0].central;
    check(oval.halfSizePixels.y==128 && oval.centerPixels.y==128,"lock did not restore full field height");
    for(unsigned y=0;y<256;++y)check(CentralWeight(oval,{511.5,y+.5})==1,"locked nasal axis lost quality");
    invalid=config;invalid.central.verticalRatioBps=10001;check(!invalid.Valid(),"invalid vertical ratio");
    invalid=config;invalid.neural.upperShareBps=10001;check(!invalid.Valid(),"invalid vertical distribution");
    auto wrongSide=protectedViews;wrongSide[0].nasalSide=static_cast<NasalSide>(99);
    rejects([&]{Plan(config,wrongSide,FixedPlacement(config),1,1,1);});
    std::cout << "PASS: independent configuration, stereo geometry, coverage, limits and motion contract\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
