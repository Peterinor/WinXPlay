#pragma once
#include <psapi.h>
// F3.21-only adapter. Called on the hooked OEM window thread, never on the
// bridge worker. Use the OEM setter + event commit; do not write layer fields.
enum { MRN_LAYER_REPORT=WM_APP+0x530, MRN_LAYER_MODULE_REPORT=WM_APP+0x531,
    MRN_LAYER_CHECK_REPORT=WM_APP+0x532, MRN_LAYER_TIMER=0x5758504c };
enum { LAYER_NO_CHANGE=0,LAYER_SUBMITTED=1,LAYER_UNSUPPORTED=2,
    LAYER_NOT_READY=3,LAYER_WRONG_THREAD=4,LAYER_LOCK_BUSY=5,
    LAYER_INVALID_STATE=6,LAYER_EXCEPTION=7,LAYER_REPLACED=8,
    LAYER_STATE_PROCESSED=9,LAYER_STATE_PENDING=10 };
struct MrnLayerResult { DWORD code,state; };
static BYTE* savedManager=0;
static BYTE* savedShared=0;
static DWORD savedSurface=0,savedVisible=0;
static BOOL layerSaved=FALSE;
static DWORD layerRequested=0,layerRequestTick=0;
static BYTE* layerPendingManager=0;
static BYTE* layerPendingShared=0;
static DWORD layerPendingSurface=0;
// Written only on the OEM UI thread; posted as values to the bridge log worker.
static DWORD layerModuleBase=0,layerModuleSize=0,layerCheck=0,layerObserved=0;
static HWND layerWindow=0;
static BYTE* LayerWindowModule() {
    // The CE 7 vehicle rejects PSAPI's EXE handle with ERROR_INVALID_PARAMETER.
    // CE maps image sections separately: AllocationBase is not the image base.
    DWORD pid=0;
    if(!layerWindow || GetWindowThreadProcessId(layerWindow,&pid)!=GetCurrentThreadId() ||
       pid!=GetCurrentProcessId())return 0;
    DWORD proc=(DWORD)GetWindowLongW(layerWindow,GWL_WNDPROC);
    MEMORY_BASIC_INFORMATION memory={0};
    if(!proc || VirtualQuery((LPCVOID)proc,&memory,sizeof(memory))!=sizeof(memory)){
        layerCheck=4;layerObserved=GetLastError();return 0;
    }
    const DWORD wndProcRva=0x2042038-0x10000;
    BYTE* base=(BYTE*)(proc-wndProcRva);
    layerModuleBase=(DWORD)base;
    // This is the F3.21 common WndProc which the existing patch hooks. A proxy,
    // subclass, another DLL or foreign process is not an acceptable anchor.
    if(proc<=wndProcRva || ((DWORD)base&0xffff) || memory.State!=MEM_COMMIT ||
       (memory.Protect&(PAGE_NOACCESS|PAGE_GUARD)) ||
       proc<(DWORD)memory.BaseAddress || memory.RegionSize<4 ||
       proc-(DWORD)memory.BaseAddress>memory.RegionSize-4){
        layerCheck=5;layerObserved=proc;return 0;
    }
    // This is only a candidate. LayerModule verifies all pinned instruction
    // sequences and the relocated manager reference before using any function.
    return base;
}
static DWORD LayerWord(BYTE* p,DWORD off){return *(volatile DWORD*)(p+off);}
static BOOL LayerWords(BYTE* base,DWORD va,const DWORD* words,DWORD count) {
    for(DWORD i=0;i<count;++i){
        DWORD value=LayerWord(base,va-0x10000+i*4);
        if(value!=words[i]){layerCheck=va+i*4;layerObserved=value;return FALSE;}
    }
    return TRUE;
}
static BYTE* LayerModule() {
    // CE HMODULE is opaque, unlike desktop Windows. CE may also omit mapped
    // PE headers; verify the actual implementation bytes instead of reading MZ.
    HMODULE module=GetModuleHandleW(0);MODULEINFO info={0};
    layerModuleBase=layerModuleSize=layerCheck=layerObserved=0;
    if(!module){layerCheck=1;layerObserved=GetLastError();return 0;}
    BYTE* base=0;
    if(GetModuleInformation(GetCurrentProcess(),module,&info,sizeof(info))){
        base=(BYTE*)info.lpBaseOfDll;
        layerModuleBase=(DWORD)base;layerModuleSize=info.SizeOfImage;
        if(!base || info.SizeOfImage<0x2303000 || info.SizeOfImage>0x2310000){
            layerCheck=3;layerObserved=info.SizeOfImage;return 0;
        }
    } else {
        layerCheck=2;layerObserved=GetLastError();
        if(layerObserved!=ERROR_INVALID_PARAMETER)return 0;
        base=LayerWindowModule();if(!base)return 0;
    }
    static const DWORD setter[]={0xe92d4070,0xe1a05002,0xe1a04001,0xe1a06000,
        0xe596300c,0xe3530000,0x0a00001b,0xe354000a,0xaa000019,0xe5960058,
        0xe3e01000,0xebfeb852,0xe596e00c,0xe0843204,0xe08e3183,0xe5c3502c,
        0xe596300c,0xe083e004,0xe5de3558,0xe3833001,0xe5ce3558,0xe5960058,0xebfffbee};
    static const DWORD wrapper[]={0xe52de004,0xeb000381,0xe3a00001,0xe49df004};
    static const DWORD commit[]={0xe92d41f0,0xe24dd038,0xe1a07000,0xe59f30ac,0xe5933000,0xe58d3034};
    static const DWORD commitBranch[]={0xea0001c7};
    static const DWORD mutexCreation[]={0xebfff5ee,0xe1a03000,0xe2840030,0xe3a02024,0xe3a01000,0xe5843058};
    if(!LayerWords(base,0x1df4bd0,setter,23) || !LayerWords(base,0x1df3dc0,wrapper,4) ||
       !LayerWords(base,0x1df44dc,commit,6) || !LayerWords(base,0x1df3db8,commitBranch,1) ||
       !LayerWords(base,0x1df6418,mutexCreation,6))return 0;
    DWORD instance=LayerWord(base,0x1dd0190-0x10000);
    if(instance!=(DWORD)(base+0x2113368-0x10000)){
        layerCheck=0x1dd0190;layerObserved=instance;return 0;
    }
    return base;
}
// This is a shared-state snapshot, NOT confirmation of pixels on the screen.
static DWORD LayerState(BYTE* shared) {
    DWORD mask=0;
    for(DWORD i=0;i<10;++i)if(shared[i*0x88+0x88] && shared[i*0x88+0x2c])mask|=1<<i;
    return mask | (DWORD(shared[0x558])<<16) | ((LayerWord(shared,0x84)&0x420)<<20);
}
static BOOL LayerIdentity(BYTE* shared) {
    return shared && shared[0x88]==1 && shared[0x24]==1 && shared[0x2c]<=1 &&
        LayerWord(shared,0x8c)==GetCurrentProcessId() && LayerWord(shared,0x1c)!=0;
}
extern "C" __declspec(dllexport) DWORD MrnLayerHandoff(HWND h,BOOL hide,MrnLayerResult* report) {
    report->code=LAYER_EXCEPTION;report->state=0;
    HANDLE lock=0;BOOL held=FALSE;
    __try {
        layerWindow=h;
        BYTE* base=LayerModule();
        if(!base){report->code=LAYER_UNSUPPORTED;return report->code;}
        DWORD pid=0;
        if(GetWindowThreadProcessId(h,&pid)!=GetCurrentThreadId() || pid!=GetCurrentProcessId()) {
            report->code=LAYER_WRONG_THREAD;return report->code;
        }
        if(!hide && !layerSaved){report->code=LAYER_NO_CHANGE;return report->code;}
        BYTE* manager=(BYTE*)LayerWord(base,0x2113368-0x10000);
        if(!manager){report->code=LAYER_NOT_READY;return report->code;}
        lock=(HANDLE)LayerWord(manager,0x58);
        DWORD wait=WaitForSingleObject(lock,0);
        if(wait!=WAIT_OBJECT_0 && wait!=WAIT_ABANDONED){report->code=LAYER_LOCK_BUSY;return report->code;}
        held=TRUE;
        BYTE* shared=(BYTE*)LayerWord(manager,0xc);
        if(!LayerIdentity(shared)){report->code=LAYER_INVALID_STATE;}
        else if(layerSaved && (manager!=savedManager || shared!=savedShared || LayerWord(shared,0x1c)!=savedSurface)) {
            report->code=LAYER_REPLACED;
        } else {
            report->state=LayerState(shared);
            if(hide && !layerSaved) {
                savedManager=manager;savedShared=shared;savedSurface=LayerWord(shared,0x1c);
                savedVisible=shared[0x2c];layerSaved=TRUE;
            }
            DWORD visible=hide?0:savedVisible;
            typedef DWORD (*SetVisibility)(void*,DWORD,DWORD);
            typedef DWORD (*Commit)(void*);
            // OEM setter acquires the same recursive mutex. Keeping our outer
            // ownership makes snapshot, validation and update one transaction.
            ((SetVisibility)(base+0x1df3dc0-0x10000))(manager,0,visible);
            ((Commit)(base+0x1df3db8-0x10000))(manager);
            layerPendingManager=manager;layerPendingShared=shared;
            layerPendingSurface=LayerWord(shared,0x1c);
            layerRequested=visible;layerRequestTick=GetTickCount();
            report->code=LAYER_SUBMITTED;
            if(!hide)layerSaved=FALSE;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {report->code=LAYER_EXCEPTION;}
    if(held)ReleaseMutex(lock);
    return report->code;
}
extern "C" __declspec(dllexport) DWORD MrnLayerObserve(MrnLayerResult* report) {
    report->code=LAYER_STATE_PENDING;report->state=0;
    HANDLE lock=0;BOOL held=FALSE;
    __try {
        BYTE* base=LayerModule();
        if(!base || (BYTE*)LayerWord(base,0x2113368-0x10000)!=layerPendingManager)report->code=LAYER_REPLACED;
        else if(layerPendingManager) {
            lock=(HANDLE)LayerWord(layerPendingManager,0x58);
            DWORD wait=WaitForSingleObject(lock,0);
            if(wait==WAIT_OBJECT_0 || wait==WAIT_ABANDONED) {
                held=TRUE;
                BYTE* shared=(BYTE*)LayerWord(layerPendingManager,0xc);
                if(shared!=layerPendingShared || !LayerIdentity(shared) || LayerWord(shared,0x1c)!=layerPendingSurface)
                    report->code=LAYER_REPLACED;
                else {
                    report->state=LayerState(shared);
                    DWORD flags=LayerWord(shared,0x84)&0x420;
                    if(!(shared[0x558]&1) && shared[0x2c]==layerRequested && flags==(layerRequested?0x400:0x20))
                        report->code=LAYER_STATE_PROCESSED;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {report->code=LAYER_EXCEPTION;}
    if(held)ReleaseMutex(lock);
    return report->code;
}
