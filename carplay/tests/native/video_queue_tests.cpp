#include "../../platform/common/video_queue.h"
#include "../../tools/mrn/av_clip.h"
#include <fstream>
#include <iostream>
using namespace cp;
Bytes read(const char *path){std::ifstream f(path,std::ios::binary);require(bool(f),"fixture missing");return Bytes(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());}
int main(int argc,char **argv){try{
    require(argc==3,"fixture and output required");
    VideoQueue q;VideoQueue::Sample s;
    Bytes config=unhex("00000001670102030000000168010203");
    Bytes idr=unhex("0000000165010203"),delta=unhex("00000141010203");
    require(!q.push(config)&&!q.push(delta)&&q.size()==0,"wait for initial IDR");
    q.push(idr);idr[4]=0;require(q.pop(s)&&s.key&&s.bytes[config.size()+4]==0x65,"owned configuration and IDR");
    for(int i=0;i<VideoQueue::MaxFrames;++i)q.push(delta);
    bool failed=false;try{q.push(delta);}catch(...){failed=true;}
    require(failed&&q.size()==VideoQueue::MaxFrames,"bounded queue");
    q.clear();require(!q.push(delta),"fresh session waits for IDR");
    failed=false;try{q.push(unhex("00000001"));}catch(...){failed=true;}
    require(failed,"truncated NAL rejected");
    failed=false;try{q.push(Bytes(VideoQueue::MaxSample+1,0));}catch(...){failed=true;}
    require(failed,"oversized packet rejected");
    q.clear();AvClip clip(read(argv[1]));std::ofstream out(argv[2],std::ios::binary);unsigned n=0;
    for(size_t i=0;i<clip.frames.size();++i){q.push(clip.frames[i].bytes);require(q.pop(s),"fixture frame lost");out.write((const char*)&s.bytes[0],s.bytes.size());++n;}
    require(n==180&&q.size()==0&&bool(out),"fixture roundtrip");
    std::cout<<"PASS queue bounds, ownership, IDR gating, reset, malformed NAL and 180 real H264 access units\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}}
