#define KHARVOX_DLSS_RECORD_TEST
#include "vulkan_dlss_tests.cpp"

int wmain(int argc,wchar_t** argv){try{
    need(argc==4,"Expected SR runtime, private cache and Neural DLL");
    Lab lab;lab.Initialize(argv[1],argv[2]);lab.o.neuralLibrary=std::filesystem::absolute(argv[3]);
    auto other=std::make_unique<VulkanDlss>(lab.o);
    VulkanDlss neural(lab.o);
    auto pair=lab.Inputs(256,256,1,Mode::Dlaa,true);pair.mode=Mode::Neural;pair.reset=false;
    Output out;lab.Begin();need(neural.Record(pair,lab.command,out)==Status::Rejected,"HDR admitted to LDR Neural");lab.Finish();pair.hdr=false;
    for(unsigned model=0;model<3;++model){pair.neuralModel=model;
        for(unsigned n=0;n<3;++n){
            const uint64_t frame=1+model*3+n;
            for(auto& eye:pair.eyes)for(auto* i:{&eye.color,&eye.depth,&eye.motion})i->identity.frame=frame;
            lab.Begin();const auto status=neural.Record(pair,lab.command,out);
            if(status!=Status::Recorded)throw std::runtime_error(neural.Error());
            need(out.ticket&&out.eyes[0].layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,"Output contract");
            Output ignored;need(neural.Record(pair,lab.command,ignored)==Status::Busy,"Neural overwritten before consumer");
            lab.Finish();lab.CheckPixels(out);need(neural.RetireRecordedAfterCompletion(out.ticket),"Neural retirement");
            // The original SR owner can disappear while the Neural branch
            // continues. A raw per-provider Shutdown would break the next call.
            if(frame==1)other.reset();
        }
    }
    need(neural.Evaluations()==18&&neural.ResetEvaluations()==6,"Neural history reset every frame or model switch lost");
    // A new SR branch shares the device while Neural keeps its model/history.
    {
        VulkanDlss sr(lab.o);auto srPair=pair;srPair.mode=Mode::Quality;
        for(auto& size:srPair.output)size={512,512};
        lab.Begin();Output srOut;if(sr.Record(srPair,lab.command,srOut)!=Status::Recorded)throw std::runtime_error(sr.Error());
        lab.Finish();lab.CheckPixels(srOut);need(sr.RetireRecordedAfterCompletion(srOut.ticket),"SR retirement");
    }
    for(auto& eye:pair.eyes)for(auto* i:{&eye.color,&eye.depth,&eye.motion})i->identity.frame=10;
    lab.Begin();need(neural.Record(pair,lab.command,out)==Status::Recorded,"SR branch shutdown broke Neural");lab.Finish();lab.CheckPixels(out);need(neural.RetireRecordedAfterCompletion(out.ticket),"Final Neural retirement");
    need(neural.ResetEvaluations()==6,"SR lifetime reset Neural history");
    std::cout<<"PASS Neural Vulkan Auto/A/B: 20 eye evaluations, six model-init resets, full pixel readback, isolated eyes, shared SR lifetime and explicit GPU retirement\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
