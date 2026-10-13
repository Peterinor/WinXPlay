// Finite native GPU integration test. Input: Annex B stream with AUD delimiters.
#include "../../platform/windows/mf_video.h"
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <mmsystem.h>

static HWND window;
static const wchar_t *inputPath,*outputPath;
static unsigned width,height,expected,interval,maxFPS=30;
static int result=1;
static unsigned __int64 ticks(FILETIME f) { return ((unsigned __int64)f.dwHighDateTime<<32)|f.dwLowDateTime; }
static DWORD WINAPI run(void*) {
    FILE *f=_wfopen(inputPath,L"rb"); if(!f) { PostMessage(window,WM_CLOSE,0,0); return 1; }
    fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
    if(size<=0) { fclose(f); PostMessage(window,WM_CLOSE,0,0); return 1; }
    std::vector<unsigned char> bytes(size); size_t read=fread(&bytes[0],1,size,f); fclose(f);
    if(read!=size_t(size)) { PostMessage(window,WM_CLOSE,0,0); return 1; }
    std::vector<unsigned> starts;
    for(unsigned i=0;i+5<bytes.size();++i) {
        unsigned n=0;
        if(bytes[i]==0 && bytes[i+1]==0) {
            if(bytes[i+2]==1) n=3;
            else if(bytes[i+2]==0 && bytes[i+3]==1) n=4;
        }
        if(n && (bytes[i+n]&31)==9) { starts.push_back(i); i+=n; }
    }
    starts.push_back(unsigned(size));
    FILETIME c,e,k0,u0,k1,u1; GetProcessTimes(GetCurrentProcess(),&c,&e,&k0,&u0);
    DWORD begin=GetTickCount(); void *handle=0;
    bool bounds=FAILED(cpMfCreate(window,width,height,0,0,0,&handle)) && !handle &&
                FAILED(cpMfCreate(window,width,height,61,0,0,&handle)) && !handle;
    HRESULT hr=cpMfCreate(window,width,height,maxFPS,0,0,&handle);
    bool lifecycle=true;
    if(SUCCEEDED(hr)) {
        bounds=bounds && FAILED(cpMfSubmit(handle,0,1)) && FAILED(cpMfSubmit(handle,&bytes[0],0)) &&
               FAILED(cpMfSubmit(handle,&bytes[0],8*1024*1024+1));
        for(unsigned i=0;i+1<starts.size() && SUCCEEDED(hr);++i) {
            hr=cpMfSubmit(handle,&bytes[starts[i]],starts[i+1]-starts[i]);
            if(interval) Sleep(interval);
        }
        if(SUCCEEDED(hr)) hr=cpMfFinish(handle);
        lifecycle=SUCCEEDED(cpMfFinish(handle)) && FAILED(cpMfSubmit(handle,&bytes[0],1));
    }
    CpMfStats stats={}; if(handle) cpMfStats(handle,&stats);
    GetProcessTimes(GetCurrentProcess(),&c,&e,&k1,&u1);
    f=_wfopen(outputPath,L"wt");
    if(f) {
        fprintf(f,"{\"hresult\":%lu,\"frames\":%u,\"expected\":%u,\"gpu_samples\":%u,\"decoder_surfaces\":%u,\"cpu_samples\":%u,\"width\":%u,\"height\":%u,\"elapsed_ms\":%lu,\"cpu_ms\":%I64u,\"max_queue_bytes\":%u,\"present_us\":%I64u,\"queue_us\":%I64u,\"max_queue_us\":%I64u}\n",
            (unsigned long)hr,stats.frames,expected,stats.gpuSamples,stats.decoderSurfaces,stats.cpuSamples,stats.width,stats.height,
            GetTickCount()-begin,(ticks(k1)+ticks(u1)-ticks(k0)-ticks(u0))/10000,stats.maxQueueBytes,stats.totalPresentUs,stats.totalQueueUs,stats.maxQueueUs);
        fclose(f);
    }
    result=SUCCEEDED(hr) && bounds && lifecycle && stats.maxFPS==maxFPS && stats.frames==expected && stats.gpuSamples==expected && stats.cpuSamples==0 && stats.width==width && stats.height==height?0:1;
    cpMfDestroy(handle); PostMessage(window,WM_CLOSE,0,0); return result;
}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_ERASEBKGND) return 1;
    if(m==WM_PAINT) { PAINTSTRUCT p; BeginPaint(h,&p); EndPaint(h,&p); return 0; }
    if(m==WM_TIMER) { static bool large=false; large=!large; SetWindowPos(h,0,0,0,large?1100:820,large?680:520,SWP_NOMOVE|SWP_NOZORDER); return 0; }
    if(m==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h,m,w,l);
}
int wmain(int argc,wchar_t **argv) {
    if(argc<7) return 2;
    timeBeginPeriod(1);
    inputPath=argv[1]; outputPath=argv[2]; width=_wtoi(argv[3]); height=_wtoi(argv[4]); expected=_wtoi(argv[5]); interval=_wtoi(argv[6]);
    if(argc>7)maxFPS=_wtoi(argv[7]);
    WNDCLASSW wc={}; wc.lpfnWndProc=proc; wc.hInstance=GetModuleHandle(0); wc.lpszClassName=L"CarPlaySystemMediaTest";
    RegisterClassW(&wc); window=CreateWindowW(wc.lpszClassName,L"CarPlay system media test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,980,620,0,0,wc.hInstance,0);
    if(!window) return 3;
    SetTimer(window,1,700,0); HANDLE thread=CreateThread(0,0,run,0,0,0);
    MSG msg; while(GetMessage(&msg,0,0,0)>0) { TranslateMessage(&msg); DispatchMessage(&msg); }
    WaitForSingleObject(thread,INFINITE); CloseHandle(thread); timeEndPeriod(1); return result;
}
