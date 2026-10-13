// PCM16 conversion and bounded per-stream queue for a fixed 48 kHz stereo output.
#pragma once
#include "../../core/protocol.h"
#include <algorithm>
#include <cstring>
namespace cp {
class Pcm48StereoQueue {
    std::vector<short> data_;
    unsigned head_,size_,phase_,rate_,channels_;
    short previous_[2];bool primed_;
  public:
    U64 dropped;
    Pcm48StereoQueue(unsigned rate,unsigned channels):data_(48000),head_(0),size_(0),phase_(0),
        rate_(rate),channels_(channels),primed_(false),dropped(0){
        require(rate>=8000 && rate<=48000 && channels>=1 && channels<=2,"Unsupported PCM format");
    }
    void clear(){head_=size_=phase_=0;primed_=false;}
    void push(const Bytes &pcm){
        require(!pcm.empty() && pcm.size()<=65536 && pcm.size()%(channels_*2)==0,"Invalid PCM packet");
        for(size_t at=0;at<pcm.size();at+=channels_*2){
            short current[2];current[0]=short(unsigned(pcm[at])|(unsigned(pcm[at+1])<<8));
            current[1]=channels_==1?current[0]:short(unsigned(pcm[at+2])|(unsigned(pcm[at+3])<<8));
            if(!primed_){previous_[0]=current[0];previous_[1]=current[1];primed_=true;continue;}
            while(phase_<48000){
                if(size_==24000){head_=(head_+1)%24000;--size_;++dropped;}
                unsigned tail=(head_+size_)%24000;
                // At 48 kHz the interpolation phase is always zero. Preserve
                // packet continuity while avoiding two software 64-bit divides
                // per PCM frame on ARM, under the mixer/receiver shared lock.
                for(unsigned c=0;c<2;++c)data_[tail*2+c]=phase_?short(previous_[c]+
                    (I64(current[c]-previous_[c])*phase_)/48000):previous_[c];
                ++size_;phase_+=rate_;
            }
            phase_-=48000;previous_[0]=current[0];previous_[1]=current[1];
        }
    }
    bool pop(short &left,short &right){
        if(!size_)return false;left=data_[head_*2];right=data_[head_*2+1];head_=(head_+1)%24000;--size_;return true;
    }
    unsigned popFrames(short *output,unsigned count){
        count=(std::min)(count,size_);if(!count)return 0;
        unsigned first=(std::min)(count,24000-head_);
        memcpy(output,&data_[head_*2],first*2*sizeof(short));
        if(first<count)memcpy(output+first*2,&data_[0],(count-first)*2*sizeof(short));
        head_=(head_+count)%24000;size_-=count;return count;
    }
    unsigned size()const{return size_;}
};
}
