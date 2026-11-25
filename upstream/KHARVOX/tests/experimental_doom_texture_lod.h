#pragma once
// Offline experiment only. Not a validated DOOM texture-quality correction.
#include <string>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <locale>
#include <optional>
namespace kharvox::native {
inline std::optional<std::string> doomTextureLod(const std::string& nativeBias,const char* fraction){
 if(!fraction)return {};
 std::istringstream baseStream(nativeBias),scaleStream(fraction);baseStream.imbue(std::locale::classic());scaleStream.imbue(std::locale::classic());
 double base{},scale{};char extra{};
 if(!(baseStream>>base)||(baseStream>>extra)||!(scaleStream>>scale)||(scaleStream>>extra)||
    !std::isfinite(base)||!std::isfinite(scale)||std::abs(base)>16||scale<.3333||scale>1)return {};
 if(scale==1)return nativeBias;
 std::ostringstream text;text.imbue(std::locale::classic());text<<std::fixed<<std::setprecision(6)<<base+std::log2(scale);
 return text.str();
}
}
