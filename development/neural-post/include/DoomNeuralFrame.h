#pragma once
#include "NeuralPostAPI.h"
namespace doomnp {
// DOOM SFS StereoFrame.generation is the pose serial, NOT a history epoch.
// Use the level latched with the SOURCE image so consecutive frames accumulate
// history and replaying an old pair cannot borrow a newer level's identity.
inline void SourceIdentity(es2np::Frame& out,uint64_t sourceLevel,uint64_t serial,int64_t timeNs){
    out.epoch=sourceLevel+1;out.generation=out.epoch;
    out.source=serial;out.timeNs=timeNs>0?uint64_t(timeNs):0;
}
}
