#include "NeuralPostSettings.h"
#include "NeuralPostFrame.h"
#include "DoomNeuralFrame.h"
#include <iostream>
#include <stdexcept>
using namespace es2np;
void Test(bool ok,const char* name){if(!ok)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
int main(int argc,char** argv){try{
 auto c=Defaults(),parsed=c;std::string error;Test(Fields.size()==26,"26 parameters");Test(Decode(Encode(c),parsed,error)&&Same(c,parsed),"lossless INI roundtrip");
 if(argc>1){Test(Read(argv[1],parsed,error),"documented configuration parses");Test(parsed.values[Enabled]==1&&parsed.values[ScalePercent]==60&&parsed.values[Passes]==1,"documented startup values");}
 for(size_t i=0;i<Fields.size();++i){auto invalid=c;invalid.values[i]=std::numeric_limits<double>::quiet_NaN();Test(!Validate(invalid,error),Fields[i].key);}
 Test(!Decode("[Neural]\nEnabled=1\nEnabled=0\n",parsed,error),"duplicate rejected");Test(!Decode("[Neural]\nEnabled=1x\n",parsed,error),"trailing junk rejected");Test(!Decode("[Neural]\nMotionSign=0\n",parsed,error),"invalid direction rejected");Test(!Decode("[Neural]\nBackend=3\n",parsed,error),"diagnostic not configurable");Test(!Decode("[Neural]\nOther=1\n",parsed,error),"unknown key rejected");
 auto active=c;auto edited=c;edited.values[Enabled]=1;edited.values[ScalePercent]=60;edited.values[IntensityPercent]=90;edited.values[Style]=2;
 const auto file=std::filesystem::temp_directory_path()/(L"ES2NeuralPost-test-"+std::to_wstring(GetCurrentProcessId())+L".ini");Test(Save(file,edited,error),"atomic save");Test(Read(file,parsed,error)&&Same(edited,parsed),"saved verification");Test(Same(c,active),"running configuration immutable");active=parsed;Test(Same(active,edited),"next startup applies saved configuration");std::filesystem::remove(file);
 Frame f;f.width=512;f.height=256;f.source=f.epoch=f.timeNs=1;f.eyes[0]={0,0,256,256,{0,0,0,1},{-.8f,.8f,.8f,-.8f}};f.eyes[1]=f.eyes[0];f.eyes[1].left=256;f.eyes[1].right=512;
 Test(ValidFrame(f),"valid stereo pair");auto next=f;Test(SameFrame(f,next),"same image reused");next.source++;next.timeNs+=10000000;Test(!SameFrame(f,next)&&!ResetRequired(f,next),"new coherent image");next.eyes[0].fov[0]=-.9f;Test(ResetRequired(f,next),"FOV change resets");next=f;next.eyes[1].left=128;Test(!ValidFrame(next),"overlapping eyes rejected");next=f;next.timeNs+=300000000;Test(ResetRequired(f,next),"gap resets");
 doomnp::SourceIdentity(f,0,100,10000000);next=f;doomnp::SourceIdentity(next,0,101,20000000);Test(!ResetRequired(f,next),"DOOM consecutive source serials preserve history");
 doomnp::SourceIdentity(next,1,102,30000000);Test(ResetRequired(f,next),"DOOM source level change resets history");
 doomnp::SourceIdentity(next,0,100,10000000);Test(SameFrame(f,next),"DOOM repeated source identity stable");return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
