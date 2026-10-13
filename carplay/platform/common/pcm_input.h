// Stateful mono PCM16 linear resampling and exact uplink packet framing.
#pragma once
#include "../../core/protocol.h"
namespace cp {
class PcmInputPackets {
    unsigned rate_,frames_,phase_; bool primed_;short previous_;
    Bytes pending_;
  public:
    PcmInputPackets(unsigned rate,unsigned frames):rate_(rate),frames_(frames),phase_(0),primed_(false),previous_(0){
        require(rate>=8000&&rate<=48000&&frames>=rate/200&&frames<=rate*60/1000,"Invalid input format");
    }
    void append(const Bytes& pcm,std::vector<Bytes>& packets){
        require(pcm.size()<=65536&&pcm.size()%2==0,"Invalid captured PCM");
        for(size_t i=0;i<pcm.size();i+=2){
            short current=short(unsigned(pcm[i])|(unsigned(pcm[i+1])<<8));
            if(!primed_){previous_=current;primed_=true;continue;}
            while(phase_<rate_){
                short sample=short(previous_+I64(current-previous_)*phase_/rate_);
                pending_.push_back(U8(sample));pending_.push_back(U8(unsigned(short(sample))>>8));
                if(pending_.size()==frames_*2){packets.push_back(pending_);pending_.clear();}
                phase_+=22050;
            }
            phase_-=rate_;previous_=current;
        }
    }
};
}
