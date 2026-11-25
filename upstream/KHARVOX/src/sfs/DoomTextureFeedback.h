#pragma once
#include <regex>
#include <iterator>
#include <string>

namespace kharvox::sfs {
// Observed in DOOM's original virtual-material shaders and the supplied SFS
// profile. The engine supplies both paging pressure and vt_feedbackLodBias in
// physicalfilterparms.y. Negative SR compensation must be clamped AFTER adding
// that bias: converting a negative float to uint can corrupt the packed LOD.
// Positive/native feedback, material page coordinates, and the page streamer
// remain unchanged. This is enabled only for the own Layer's fragment shaders.
inline unsigned correctDoomTextureFeedback(std::string& source){
    static const std::regex feedback(
        R"((uint feedbackLOD(?:_\d+)? = uint\()(min\((\w+)\.physicalfilterparms\.w, max\(0\.0, anisoLOD(?:_\d+)?\) \+ feedbackBias(?:_\d+)?\))(\);))");
    const auto count=unsigned(std::distance(
        std::sregex_iterator(source.begin(),source.end(),feedback),std::sregex_iterator()));
    if(count)source=std::regex_replace(source,feedback,"$1max(0.0, $2)$4");
    return count;
}
}
