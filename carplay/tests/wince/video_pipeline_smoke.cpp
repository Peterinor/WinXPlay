// Live producer/consumer DirectShow test on CE. RGB bypasses absent OEM H264.
#include "../../platform/wince/video_dshow.h"
#include <memory>
using namespace cp;
namespace {
HWND window;HANDLE thread;Value result=Value::dictionary();
DWORD WINAPI run(void*){
    try {
        unsigned total=0;const unsigned rates[]={20,25,30};
        for(unsigned round=0;round<3;++round){
            std::auto_ptr<VideoPipeline> p(createCeRgbTestVideo(window,64,48,rates[round]));
            for(unsigned i=0;i<60;++i){
                if(i==30)Sleep(1200); // Static phone screen, then fresh updates.
                Bytes b(64*48*2,U8(i));p->submit(b,0);Sleep(40);}
            DWORD start=GetTickCount();while(p->frames()<60&&GetTickCount()-start<5000)Sleep(10);
            require(p->frames()==60,"Live source did not supply 60 samples");total+=p->frames();
            Value metrics=p->metrics();
            require(metrics.at("max_fps").integer==rates[round],"Pipeline lost selected frame rate");
            require(metrics.at("supplied").integer==60 && metrics.at("enqueued").integer==60,"Pipeline telemetry lost samples");
            require(metrics.at("q_frames").integer==0 && metrics.at("q_peak").integer<=8,"Pipeline queue telemetry invalid");
            require(metrics.at("supply_gap_max_ms").integer>=1000,"Telemetry missed idle gap");
            require(p->metrics().at("supply_gap_max_ms").integer==0,"Telemetry window peak did not reset");
            p->finish();p->finish();
            Value backend=parsePlist(readFile(executableDirectory()+L"/video-backend.plist"));
            require(!backend.dict.count("error"),"Graph teardown error");
            require(backend.at("stage").string=="closed" &&
                backend.at("diagnostic_reports_failed").integer==0,"Final asynchronous report was not flushed");
            require(backend.at("max_fps").integer==rates[round] &&
                backend.at("sample_duration_100ns").integer==10000000/rates[round],"Frame rate timing mismatch");
            require(backend.at("window_messages_processed").integer>0,"Graph thread did not pump its window messages");
            require(backend.at("clock_scheduled").boolean && backend.at("samples_clocked").integer>0,"Default graph clock was not used");
            require(backend.at("last_sample_time_100ns").integer>=30000000,"Live timestamps did not follow the idle gap");
        }
        {
            std::auto_ptr<VideoPipeline> p(createCeRgbTestVideo(window,64,48,30));
            Sleep(300);p->finish();require(p->frames()==0,"Unexpected empty queue frame");
        }
        {
            std::auto_ptr<VideoPipeline> p(createCeSystemVideo(window,800,480,30));
            Sleep(1000);bool rejected=false,idleRejected=false;
            try{p->checkStatus();}catch(...){idleRejected=true;}
            require(idleRejected,"Idle decoder failure was not reported");
            try{p->submit(unhex("00000001670102030000000168010203"),0);}catch(...){rejected=true;}
            p->finish();require(rejected,"Emulator should report absent OEM decoder");
        }
        result.dict["samples_supplied"]=Value::num(total);
        result.dict["repeated_sessions"]=Value::num(3);
        result.dict["empty_queue_cancelled"]=Value::flag(true);
        result.dict["missing_decoder_rejected"]=Value::flag(true);
        result.dict["idle_decoder_failure_reported"]=Value::flag(true);
        result.dict["graph_thread_messages_processed"]=Value::flag(true);
        result.dict["default_clock_and_idle_gap"]=Value::flag(true);
        result.dict["configured_frame_rates_verified"]=Value::str("20,25,30");
        result.dict["passed"]=Value::flag(true);
    }catch(const std::exception &e){result.dict["passed"]=Value::flag(false);result.dict["error"]=Value::str(e.what());}
    result.dict["physical_h264_verified"]=Value::flag(false);
    try{writeFile(executableDirectory()+L"/video-pipeline-smoke.plist",xmlPlist(result));}catch(...){}
    PostMessageW(window,WM_CLOSE,0,0);return 0;
}
LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b){
    if(m==WM_CLOSE){if(WaitForSingleObject(thread,0)==WAIT_OBJECT_0)DestroyWindow(w);return 0;}
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,m,a,b);
}
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    WNDCLASSW wc={0};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=L"MRN_VIDEO_PIPELINE_SMOKE";
    RegisterClassW(&wc);window=CreateWindowW(wc.lpszClassName,L"Live pipeline test",WS_POPUP|WS_VISIBLE|WS_CLIPCHILDREN,
        0,0,240,320,0,0,instance,0);
    if(!window)return 2;thread=CreateThread(0,0,run,0,0,0);if(!thread)return 3;
    MSG m;while(WaitForSingleObject(thread,10)==WAIT_TIMEOUT){while(PeekMessageW(&m,0,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
    CloseHandle(thread);DestroyWindow(window);return result.at("passed").boolean?0:1;
}
