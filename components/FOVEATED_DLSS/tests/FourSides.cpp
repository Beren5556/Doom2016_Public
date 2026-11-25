#include <dlss_foveado/Foveation.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace dlss_foveado;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::invalid_argument&){failed=true;}check(failed,"invalid new contract accepted");}
int main()try{
    Config c;c.enabled=true;
    check(ContractVersion==4 && EngineVersion==DLSS_FOVEADO_TEST_VERSION && ConfigSchemaVersion==3,"engine/package version or base contract mismatch");
    check(c.Encode()=="3;1;1;2000;2000;2000;2000;1000;0;7500;0;0;10000;0;1;1;2000;2000;2000;2000;1000;0","wire field order");
    c.central.topBps=500;c.central.bottomBps=3000;c.central.rightBps=3500;c.central.leftBps=1000;
    c.neuralLinked=false;c.neural.topBps=4500;c.neural.bottomBps=0;c.neural.rightBps=4000;c.neural.leftBps=200;
    c.periphery=Periphery::Temporal;c.centralPreset=12;c.peripheralPreset=13;c.indicatorRegions=2;
    check(Config::Parse(c.Encode())==c,"independent four sides round trip");
    for(unsigned i=0;i<22;++i){
        auto encoded=c.Encode();size_t start=0;
        for(unsigned j=0;j<i;++j)start=encoded.find(';',start)+1;
        auto end=encoded.find(';',start);if(end==std::string::npos)end=encoded.size();
        for(const char* bad:{"","-1","+1"," 1","1 ","4294967296","99999"}){
            auto altered=encoded;altered.replace(start,end-start,bad);
            check(!Config::Parse(altered),"invalid wire field accepted");
        }
    }
    check(!Config::Parse(c.Encode()+";")&&!Config::Parse(c.Encode()+";0"),"extra delimiter/field");
    auto legacy=Config::Parse("2;1;1;8000;6000;1500;1;1;7500;13;12;12000;5100;4500;4900;5500;1;0;0;6000;6000;2000;1;2;0;9000;7000;1;10000;5000");
    check(legacy && legacy->geometryMode==GeometryMode::Legacy && legacy->Encode().starts_with("2;"),"legacy read changed geometry");
    const auto old=*legacy, draft=legacy->FourSideDraft();
    check(*legacy==old && draft.geometryMode==GeometryMode::BinocularFourSides,"migration changed original");
    check(draft.central.topBps==2000 && draft.neural.leftBps==2000 && draft.central.transitionBps==1500 &&
        draft.neural.transitionBps==2000 && draft.neural.shape==Shape::Rectangle && !draft.neuralLinked &&
        draft.centralPreset==12 && draft.peripheralPreset==13 && draft.supersampleBps==12000 &&
        draft.showRegion && draft.indicatorRegions==2 && Config::Parse(draft.Encode())==draft,"migration lost common settings");
    check(c.FourSideDraft()==c,"migration reset existing schema3 edits");
    std::array<ViewGeometry,2> views{{{{500,500},{1000,1000},10},{{500,500},{1000,1000},20}}};
    views[0].binocular={{{.0008,0,0,0,.001,0,0,0,1}},7,1};
    views[1].binocular={{{.0008,0,.2,0,.001,0,0,0,1}},7,1};
    auto plan=[&]{return Plan(c,views,FixedPlacement(c),1,1,1);};
    const auto maskAt=[&](const RegionPlan& r,Point p,unsigned eye){return CentralWeight(r,{(p.x-(eye?.2:0))/.0008,p.y*1000});};
    const std::array<std::array<uint32_t,4>,7> cases{{{2000,2000,2000,2000},{2000,2000,0,0},{0,0,2000,2000},
        {3500,500,2000,2000},{2000,2000,3500,500},{500,3000,3500,1000},{4500,4500,4500,4500}}};
    for(auto cuts:cases)for(auto shape:{Shape::Ellipse,Shape::Rectangle})for(uint32_t transition:{0u,1000u,5000u}){
        c.central.shape=shape;c.central.topBps=cuts[0];c.central.bottomBps=cuts[1];
        c.central.rightBps=cuts[2];c.central.leftBps=cuts[3];c.central.transitionBps=transition;
        const auto p=plan();
        const double t=cuts[0]/10000.,b=1-cuts[1]/10000.,r=1-cuts[2]/10000.,l=cuts[3]/10000.;
        for(unsigned eye=0;eye<2;++eye){
            const auto& m=p.views[eye].central;
            check(maskAt(m,{.5,.5},eye)==1,"fixed center missing");
            // Points immediately inside each independently requested extremum.
            for(auto point:{Point{.5,t+1e-6},Point{.5,b-1e-6},Point{r-1e-6,.5},Point{l+1e-6,.5}})
                check(maskAt(m,point,eye)==1,"independent cardinal boundary incorrect");
            const double scale=1+transition/10000.;
            check(maskAt(m,{.5+(.5-l)*(-scale)-1e-6,.5},eye)==0,"left outer transition wrong");
            // Analytic ellipse/circle at q=sqrt(2)*.8, or rectangle at q=.8.
            const double expectedQ=shape==Shape::Ellipse?std::sqrt(2.)*.8:.8;
            const double fade=transition?std::clamp((expectedQ-1)/(transition/10000.),0.,1.):(expectedQ>1?1.:0.);
            const double expected=1-fade*fade*(3-2*fade);
            check(std::abs(maskAt(m,{.5+(r-.5)*.8,.5+(.5-t)*(-.8)},eye)-expected)<1e-10,"quarter ellipse analytic weight");
            for(unsigned y=0;y<1000;y+=9)for(unsigned x=0;x<1000;x+=9)for(auto region:{p.views[eye].central,p.views[eye].neural}){
                if(CentralWeight(region,{x+.5,y+.5})>0){
                    const double u=(x+.5)*region.cropScale.x+region.cropBias.x,v=(y+.5)*region.cropScale.y+region.cropBias.y;
                    check(u>=0 && u<=1 && v>=0 && v<=1,"positive mask outside reconstruction crop");
                }
            }
        }
        for(double y=.05;y<1;y+=.035)for(double x=.2;x<.8;x+=.027)
            check(std::abs(maskAt(p.views[0].central,{x,y},0)-maskAt(p.views[1].central,{x,y},1))<1e-12,"overlapping eyes disagree");
    }
    c={};c.central.topBps=3500;c.central.bottomBps=500;c.neuralLinked=false;
    auto original=views;
    // Rotated/sheared, perspective and asymmetric-sized views: support must stay in crop.
    views[1].input={540,450};views[1].output={1080,900};
    for(double perspective:{-.0003,0.,.0004}){
        for(auto& g:views)g.binocular.outputToCommon={.0008,.00012,-.08,-.00007,.001,.08,perspective,.0001,1};
        const auto p=plan();
        for(auto& v:p.views)for(unsigned y=0;y<v.geometry.output.height;y+=5)for(unsigned x=0;x<v.geometry.output.width;x+=5){
            const auto& m=v.central;if(CentralWeight(m,{x+.5,y+.5})>0)
                check((x+.5)*m.cropScale.x+m.cropBias.x>=0 && (x+.5)*m.cropScale.x+m.cropBias.x<=1 &&
                      (y+.5)*m.cropScale.y+m.cropBias.y>=0 && (y+.5)*m.cropScale.y+m.cropBias.y<=1,"projective crop too small");
        }
    }
    views=original;views[0].binocular={};rejects(plan);
    views=original;views[1].binocular.revision=2;rejects(plan);
    views=original;views[1].binocular.spaceId=8;rejects(plan);
    views=original;views[0].binocular.outputToCommon[0]=std::numeric_limits<double>::quiet_NaN();rejects(plan);
    views=original;views[0].binocular.outputToCommon[0]=0;rejects(plan);
    views=original;views[0].binocular.outputToCommon[6]=-.002;rejects(plan);
    views=original;views[0].binocular.outputToCommon[2]=100;rejects(plan);
    views=original;auto placement=FixedPlacement(c);placement.centers[0].x=.6;
    rejects([&]{Plan(c,views,placement,1,1,1);});
    std::cout<<"PASS: schema3, explicit migration, independent cuts, overlap, projective crops, limits and missing calibration\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
