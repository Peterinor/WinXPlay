// Host regression for the exact PCM meter/WAV encoder used by the CE probe.
// Does not open a real recording device.
#include "../../tools/mrn/microphone_pcm.h"
#include <assert.h>
#include <stdio.h>
int main(int argc,char** argv) {
    using namespace microphone_pcm;
    unsigned char pcm[]={0,0,0,0, 255,127,0,0, 0,128,210,4, 100,0,46,251};
    Meter m[2];assert(measure(pcm,sizeof(pcm),2,m));
    assert(m[0].samples==4&&m[0].peak==32768&&m[0].clipped==2&&m[0].nonzero==3);
    assert(m[0].absoluteSum==65635.0);
    assert(m[1].samples==4&&m[1].peak==1234&&m[1].nonzero==2&&m[1].absoluteSum==2468.0);
    assert(!measure(pcm,3,2,m)&&m[0].samples==4);
    assert(!measure(pcm,sizeof(pcm),0,m));assert(!measure(pcm,sizeof(pcm),3,m));
    Meter silence[2];unsigned char zeros[16]={0};assert(measure(zeros,sizeof(zeros),1,silence));
    assert(silence[0].samples==8&&silence[0].peak==0&&silence[0].nonzero==0);
    Meter split[2];assert(measure(pcm,8,2,split));assert(measure(pcm+8,8,2,split));
    assert(split[0].absoluteSum==m[0].absoluteSum&&split[1].samples==m[1].samples);
    unsigned char header[44];wavHeader(header,32000,2,sizeof(pcm));
    assert(!memcmp(header,"RIFF",4)&&!memcmp(header+8,"WAVEfmt ",8)&&!memcmp(header+36,"data",4));
    assert(argc==2);FILE* f=fopen(argv[1],"wb");assert(f);
    assert(fwrite(header,1,44,f)==44);assert(fwrite(pcm,1,sizeof(pcm),f)==sizeof(pcm));assert(!fclose(f));
    puts("PCM: signed extrema, silence, split buffers, stereo separation and alignment passed.");
    return 0;
}
