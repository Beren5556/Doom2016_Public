#include <dlss_foveado/Foveation.h>
#include <charconv>
#include <vector>

namespace dlss_foveado {
namespace {
bool preset(uint32_t p) { return p == 0 || (p >= 10 && p <= 13); }
bool region(const Region& r, GeometryMode mode) {
    if (uint32_t(r.shape) > 1 || r.transitionBps > 5000) return false;
    if (mode == GeometryMode::BinocularFourSides)
        return r.topBps <= 4500 && r.bottomBps <= 4500 && r.rightBps <= 4500 && r.leftBps <= 4500;
    return r.widthBps >= 1000 && r.widthBps <= 10000 &&
        r.heightBps >= 1000 && r.heightBps <= 10000 && r.transitionBps <= 5000 &&
        r.verticalRatioBps <= 10000 && r.upperShareBps <= 10000;
}
}
bool Config::Valid() const noexcept {
    if (uint32_t(geometryMode) > 1 || !region(central, geometryMode) || !region(neural, geometryMode) || uint32_t(periphery) > 1 ||
        peripheralScaleBps < 2500 || peripheralScaleBps > 10000 ||
        !preset(peripheralPreset) || !preset(centralPreset) ||
        supersampleBps < 10000 || supersampleBps > 20000 || indicatorRegions > 2) return false;
    if (geometryMode == GeometryMode::Legacy) for (unsigned eye = 0; eye < 2; ++eye)
        if (centerXBps[eye] > 10000 || centerYBps[eye] > 10000) return false;
    return true;
}
std::string Config::Encode() const {
    const std::vector<uint32_t> values = geometryMode == GeometryMode::BinocularFourSides ?
        std::vector<uint32_t>{3, enabled, uint32_t(central.shape), central.topBps, central.bottomBps,
        central.rightBps, central.leftBps, central.transitionBps, uint32_t(periphery), peripheralScaleBps,
        peripheralPreset, centralPreset, supersampleBps, showRegion, neuralLinked, uint32_t(neural.shape),
        neural.topBps, neural.bottomBps, neural.rightBps, neural.leftBps, neural.transitionBps, indicatorRegions} :
        std::vector<uint32_t>{2, enabled, uint32_t(central.shape), central.widthBps,
        central.heightBps, central.transitionBps, central.linkedAxes, uint32_t(periphery),
        peripheralScaleBps, peripheralPreset, centralPreset, supersampleBps,
        centerXBps[0], centerYBps[0], centerXBps[1], centerYBps[1], showRegion, neuralLinked,
        uint32_t(neural.shape), neural.widthBps, neural.heightBps, neural.transitionBps,
        neural.linkedAxes, indicatorRegions, central.lockHeight, central.verticalRatioBps,
        central.upperShareBps, neural.lockHeight, neural.verticalRatioBps, neural.upperShareBps};
    std::string result;
    for (auto value : values) { if (!result.empty()) result += ';'; result += std::to_string(value); }
    return result;
}
Config Config::FourSideDraft() const {
    if (geometryMode == GeometryMode::BinocularFourSides) return *this;
    auto result = *this;
    result.geometryMode = GeometryMode::BinocularFourSides;
    const auto reset = [](Region& r) {
        const auto shape = r.shape; const auto transition = r.transitionBps;
        r = {}; r.shape = shape; r.transitionBps = transition;
    };
    reset(result.central); reset(result.neural);
    result.centerXBps = result.centerYBps = {5000, 5000};
    return result;
}
std::optional<Config> Config::Parse(std::string_view text) noexcept {
    try {
        if (text.empty() || text.size() > 1024) return {};
        std::vector<uint32_t> v;
        while (!text.empty()) {
            const auto end = text.find(';');
            const auto item = text.substr(0, end);
            if (item.empty()) return {};
            uint32_t value{};
            const auto parsed = std::from_chars(item.data(), item.data() + item.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != item.data() + item.size()) return {};
            v.push_back(value);
            if (v.size() > 30) return {};
            if (end == std::string_view::npos) break;
            text.remove_prefix(end + 1);
            if (text.empty()) return {};
        }
        if (v[0] == 3 && v.size() == 22) {
            for (auto index : {1u, 13u, 14u}) if (v[index] > 1) return {};
            Config c; c.enabled = v[1] != 0;
            c.central.shape = Shape(v[2]); c.central.topBps = v[3]; c.central.bottomBps = v[4];
            c.central.rightBps = v[5]; c.central.leftBps = v[6]; c.central.transitionBps = v[7];
            c.periphery = Periphery(v[8]); c.peripheralScaleBps = v[9]; c.peripheralPreset = v[10];
            c.centralPreset = v[11]; c.supersampleBps = v[12]; c.showRegion = v[13] != 0;
            c.neuralLinked = v[14] != 0; c.neural.shape = Shape(v[15]);
            c.neural.topBps = v[16]; c.neural.bottomBps = v[17]; c.neural.rightBps = v[18];
            c.neural.leftBps = v[19]; c.neural.transitionBps = v[20]; c.indicatorRegions = v[21];
            return c.Valid() ? std::optional<Config>(c) : std::nullopt;
        }
        if (!((v[0] == 1 && v.size() == 24) || (v[0] == 2 && v.size() == 30))) return {};
        for (auto index : {1u, 6u, 16u, 17u, 22u}) if (v[index] > 1) return {};
        Config c; c.geometryMode = GeometryMode::Legacy;
        c.enabled = v[1] != 0;
        c.central = {Shape(v[2]), v[3], v[4], v[5], v[6] != 0};
        c.periphery = Periphery(v[7]); c.peripheralScaleBps = v[8];
        c.peripheralPreset = v[9]; c.centralPreset = v[10]; c.supersampleBps = v[11];
        c.centerXBps = {v[12], v[14]}; c.centerYBps = {v[13], v[15]};
        c.showRegion = v[16] != 0; c.neuralLinked = v[17] != 0;
        c.neural = {Shape(v[18]), v[19], v[20], v[21], v[22] != 0}; c.indicatorRegions = v[23];
        if (v[0] == 2) {
            if (v[24] > 1 || v[27] > 1) return {};
            c.central.lockHeight = v[24] != 0; c.central.verticalRatioBps = v[25]; c.central.upperShareBps = v[26];
            c.neural.lockHeight = v[27] != 0; c.neural.verticalRatioBps = v[28]; c.neural.upperShareBps = v[29];
        }
        return c.Valid() ? std::optional<Config>(c) : std::nullopt;
    } catch (...) { return {}; }
}
} // namespace dlss_foveado
