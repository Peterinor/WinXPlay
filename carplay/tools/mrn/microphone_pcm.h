#pragma once
#include <stddef.h>
#include <string.h>
namespace microphone_pcm {
struct Meter {
    unsigned samples, nonzero, clipped, peak;
    double absoluteSum;
    Meter():samples(0),nonzero(0),clipped(0),peak(0),absoluteSum(0){}
    void add(int sample) {
        unsigned value=sample<0?unsigned(-sample):unsigned(sample);
        ++samples;if(value)++nonzero;if(value>=32767)++clipped;
        if(value>peak)peak=value;absoluteSum+=value;
    }
};
// Explicit little-endian PCM16; separate channels expose one-sided ADC data.
inline bool measure(const unsigned char* data,size_t bytes,unsigned channels,Meter* meters) {
    if(!channels || channels>2 || bytes%(channels*2))return false;
    for(size_t i=0;i<bytes;i+=2) {
        int sample=int(data[i])|(int(data[i+1])<<8);
        if(sample>=32768)sample-=65536;
        meters[(i/2)%channels].add(sample);
    }
    return true;
}
inline void put16(unsigned char* p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
inline void put32(unsigned char* p,unsigned v){for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(v>>(i*8));}
inline void wavHeader(unsigned char* p,unsigned rate,unsigned channels,unsigned bytes) {
    memset(p,0,44);memcpy(p,"RIFF",4);put32(p+4,bytes+36);memcpy(p+8,"WAVEfmt ",8);
    put32(p+16,16);put16(p+20,1);put16(p+22,channels);put32(p+24,rate);
    put32(p+28,rate*channels*2);put16(p+32,channels*2);put16(p+34,16);
    memcpy(p+36,"data",4);put32(p+40,bytes);
}
}
