#pragma once
#include "NeuralPostAPI.h"
#include <array>
#include <cmath>
#include <string>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <locale>
#include <set>
#include <windows.h>

namespace es2np {
enum Option { Enabled, ScalePercent, IntensityPercent, Passes, Residual, Style, Preset,
 LocalStructure, LocalTone, SkinAuto, SkinStrength, AutoMask, FoveaPercent, FoveaShape,
 FeatherPixels, OuterEnabled, OuterScalePercent, OuterPack, MotionVectors, MotionSign,
 MotionScale, ConstantDepth, ViewportReferencePercent, Backend, AppId, LogFrames };
enum Kind { Flag, Integer, Real };
struct Field { const char* key; const char* label; double initial, minimum, maximum; Kind kind; };
inline constexpr std::array<Field, OptionCount> Fields{{
 {"Enabled","Activar Neural",0,0,1,Flag},
 {"ScalePercent","Escala interna (%)",25,25,100,Integer},
 {"IntensityPercent","Intensidad (%)",60,0,100,Integer},
 {"Passes","Pasadas",1,1,4,Integer},
 {"Residual","Conservar detalle original (residual)",1,0,1,Flag},
 {"Style","Estilo: 0 equilibrado / 1 nitido / 2 cinematico",0,0,2,Integer},
 {"Preset","Preset del modelo",0,0,1,Integer},
 {"LocalStructure","Estructura local",1,0,1,Real},
 {"LocalTone","Tono local",1,0,1,Real},
 {"SkinAuto","Piel automatica",1,0,1,Flag},
 {"SkinStrength","Estructura de piel manual",1,0,1,Real},
 {"AutoMask","Mascara automatica",1,0,1,Flag},
 {"FoveaPercent","Region central (%)",100,20,100,Integer},
 {"FoveaShape","Forma: 0 rectangulo / 1 elipse",1,0,1,Integer},
 {"FeatherPixels","Transicion (pixeles)",400,0,4096,Real},
 {"OuterEnabled","Procesar periferia",0,0,1,Flag},
 {"OuterScalePercent","Escala periferica (%)",25,10,100,Integer},
 {"OuterPack","Empaquetar regiones",1,0,1,Flag},
 {"MotionVectors","Vectores estimados de giro",1,0,1,Flag},
 {"MotionSign","Signo del movimiento (-1 / +1)",1,-1,1,Integer},
 {"MotionScale","Escala del movimiento",1,0,4,Real},
 {"ConstantDepth","Profundidad constante",.5,0,1,Real},
 {"ViewportReferencePercent","Referencia del viewport (%)",100,25,100,Integer},
 {"Backend","Proveedor: 0 auto / 1 driver / 2 DLL local",2,0,2,Integer},
 {"AppId","NGX AppId",608699473,0,4294967295.,Integer},
 {"LogFrames","Fotogramas de diagnostico inicial",3,0,60,Integer}
}};
inline Config Defaults() { Config c; for(size_t i=0;i<Fields.size();++i)c.values[i]=Fields[i].initial; return c; }
inline bool Validate(const Config& c, std::string& error) {
 if(c.size!=sizeof(Config)||c.abi!=Abi){error="Version de configuracion incompatible";return false;}
 for(size_t i=0;i<Fields.size();++i){const auto& f=Fields[i];const double v=c.values[i];
  if(!std::isfinite(v)||v<f.minimum||v>f.maximum||(f.kind!=Real&&v!=std::floor(v))||(i==MotionSign&&v!=1&&v!=-1)){
   error=std::string("Valor invalido: ")+f.key;return false;
  }
 }error.clear();return true;
}
inline bool Same(const Config& a,const Config& b){for(size_t i=0;i<Fields.size();++i)if(a.values[i]!=b.values[i])return false;return true;}
inline std::string Encode(const Config& c){std::ostringstream s;s.imbue(std::locale::classic());s<<"[Neural]\nSchema=1\n"<<std::setprecision(17);for(size_t i=0;i<Fields.size();++i)s<<Fields[i].key<<'='<<c.values[i]<<'\n';return s.str();}
inline std::string Trim(std::string s){auto a=s.find_first_not_of(" \t\r\n");if(a==s.npos)return {};return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);}
inline bool Decode(const std::string& text,Config& result,std::string& error){
 Config candidate=Defaults();std::istringstream stream(text);std::string line;bool in=false,section=false;std::set<std::string> seen;
 while(std::getline(stream,line)){line=Trim(line);if(line.empty()||line[0]==';'||line[0]=='#')continue;
  if(line[0]=='['){if(line=="[Neural]"){if(section){error="Seccion Neural duplicada";return false;}in=section=true;}else in=false;continue;}
  if(!in)continue;const auto at=line.find('=');if(at==line.npos){error="Linea Neural sin valor";return false;}
  const auto key=Trim(line.substr(0,at)), value=Trim(line.substr(at+1));if(!seen.insert(key).second){error="Clave duplicada: "+key;return false;}
  std::istringstream number(value);number.imbue(std::locale::classic());double v{};if(!(number>>v)||(number>>std::ws,!number.eof())){error="Numero invalido: "+key;return false;}
  if(key=="Schema"){if(v!=1){error="Schema Neural incompatible";return false;}continue;}
  bool found=false;for(size_t i=0;i<Fields.size();++i)if(key==Fields[i].key){candidate.values[i]=v;found=true;break;}
  if(!found){error="Parametro desconocido: "+key;return false;}
 }
 if(!section){error="Falta [Neural]";return false;}if(!Validate(candidate,error))return false;result=candidate;return true;
}
inline bool Read(const std::filesystem::path& file,Config& c,std::string& error){
 if(!std::filesystem::exists(file)){c=Defaults();error.clear();return true;}
 std::ifstream stream(file,std::ios::binary);if(!stream){error="No se puede leer la configuracion";return false;}
 std::string text((std::istreambuf_iterator<char>(stream)),{});if(text.size()>65536){error="Configuracion demasiado grande";return false;}
 if(text.starts_with("\xef\xbb\xbf"))text.erase(0,3);return Decode(text,c,error);
}
inline bool Save(const std::filesystem::path& file,const Config& c,std::string& error){
 if(!Validate(c,error))return false;
 const auto temp=file.wstring()+L".pending-"+std::to_wstring(GetCurrentProcessId());const auto bytes=Encode(c);
 HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(h==INVALID_HANDLE_VALUE){error="No se puede crear el archivo temporal";return false;}
 DWORD n{};const bool ok=WriteFile(h,bytes.data(),DWORD(bytes.size()),&n,nullptr)&&n==bytes.size()&&FlushFileBuffers(h);CloseHandle(h);
 Config readback;const bool exact=ok&&Read(temp,readback,error)&&Same(c,readback);
 if(!exact||!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
  DeleteFileW(temp.c_str());if(error.empty())error="No se pudo guardar; ajustes anteriores conservados";return false;
 }error.clear();return true;
}
}
