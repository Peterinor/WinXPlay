// Hidden integration host: production session/window input, synthetic mic only.
#include "../../platform/common/native_services.h"
#include "../../platform/common/receiver_context.h"
#include "../../platform/common/window_input.h"
#include "../../platform/common/phone_ui.h"
#include <iostream>
using namespace cp;
namespace {
Receiver* receiver=0;
WindowInput input;
struct Capture:AudioInput {
    unsigned samples;DWORD last;
    Capture(unsigned n):samples(n),last(0){}
    ~Capture(){writeFile(executableDirectory()+L"/input-microphone-closed.flag",bytes("closed"));}
    std::vector<Bytes> poll() {
        std::vector<Bytes> out;
        if(DWORD(GetTickCount()-last)<20)return out;
        last=GetTickCount();Bytes b(samples*2);
        for(unsigned i=0;i<samples;++i){b[i*2]=0x34;b[i*2+1]=0x12;}
        out.push_back(b);return out;
    }
};
struct Media:MediaFactory {
    MediaFactory& native;
    bool syntheticVideo;
    struct Video:VideoPipeline {
        unsigned n;Video():n(0){}
        void submit(const Bytes&,U64){++n;}
        void finish(){}
        unsigned frames()const{return n;}
    };
    Media(MediaFactory& m,bool synthetic=false):native(m),syntheticVideo(synthetic){}
    MediaCapabilities capabilities() const { return native.capabilities(); }
    VideoPipeline* video(unsigned w,unsigned h){return syntheticVideo?new Video:native.video(w,h);}
    AudioOutput* audio(unsigned r,unsigned c,bool aac){return native.audio(r,c,aac);}
    AudioInput* microphone(unsigned,unsigned,unsigned n){return new Capture(n);}
};
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    RECT r={0,0,0,0};GetClientRect(h,&r);
    int w=r.right,ht=w*480/800;if(ht>r.bottom){ht=r.bottom;w=ht*800/480;}
    RECT picture={(r.right-w)/2,(r.bottom-ht)/2,(r.right+w)/2,(r.bottom+ht)/2};
    if(input.handle(h,msg,wp,lp,receiver,picture))return msg==0x0319?TRUE:0;
    if(msg==PhoneCommandMessage){applyPhoneCommand(h,lp);return 0;}
    if(msg==WM_TIMER || msg==WM_CLOSE){DestroyWindow(h);return 0;}
    if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,msg,wp,lp);
}
}
int main(int argc,char** argv) {
    try {
        UsbMux sockets;
        ReceiverContext native(0,0);ReceiverServices& s=native.services();
        if(argc==2 && std::string(argv[1])=="--microphone-unavailable") {
            // Invalid ID guarantees this test cannot record a physical microphone.
            SetEnvironmentVariableW(L"CARPLAY_WAVE_IN",L"2147483647");
            std::auto_ptr<AudioInput> mic(s.media.microphone(16000,1,320));
            require(mic->poll().empty(),"Unavailable microphone produced data");
            Value first=parsePlist(readFile(executableDirectory()+L"/audio-input.plist"));
            require(first.at("device_failures").integer==1,"Missing first microphone failure");
            require(mic->poll().empty(),"Backoff produced data");
            Sleep(2100);require(mic->poll().empty(),"Retry produced data");
            Value second=parsePlist(readFile(executableDirectory()+L"/audio-input.plist"));
            require(second.at("device_failures").integer==2,"Microphone did not retry");
            return 0;
        }
        Media media(s.media,argc==2 && std::string(argv[1])=="--startup-home");ReceiverServices services(s.crypto,s.store,s.transport,s.runtime,media,s.identity);
        Receiver session(services,0,0,17001);receiver=&session;
        WNDCLASSW wc={0};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(0);wc.lpszClassName=L"WinXPlayInputTest";
        require(RegisterClassW(&wc)!=0,"Register input test window");
        HWND h=CreateWindowW(wc.lpszClassName,L"WinXPlay input test",WS_OVERLAPPEDWINDOW,0,0,800,600,0,0,wc.hInstance,0);
        require(h!=0,"Create input test window");
        session.setControlCallback(postPhoneCommand,h);
        SetTimer(h,1,45000,0);session.start();
        Value info=Value::dictionary();info.dict["window"]=Value::num((ULONG_PTR)h);
        writeFile(executableDirectory()+L"/input-listener.plist",xmlPlist(info));
        MSG message;while(GetMessageW(&message,0,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
        session.stop();receiver=0;return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
