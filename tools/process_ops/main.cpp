#include <windows.h>
#include <tlhelp32.h>
#include "process_config.h"

static HANDLE g_log=INVALID_HANDLE_VALUE;
static DWORD g_begin=0,g_deadline=0;
static BOOL g_logOK=TRUE;
static ProcessJob g_jobs[MAX_PROCESS_JOBS];
static BYTE g_config[MAX_PROCESS_CONFIG_BYTES];
static void Note(const WCHAR* text) {
    WCHAR prefix[40];wsprintfW(prefix,L"[%lu] ",GetTickCount()-g_begin);
    DWORD n=0,size=lstrlenW(prefix)*2;
    if(!WriteFile(g_log,prefix,size,&n,0)||n!=size)g_logOK=FALSE;
    size=lstrlenW(text)*2;
    if(!WriteFile(g_log,text,size,&n,0)||n!=size)g_logOK=FALSE;
    if(!WriteFile(g_log,L"\r\n",4,&n,0)||n!=4)g_logOK=FALSE;
}
static BOOL Commit() {return g_logOK && FlushFileBuffers(g_log);}
static void Finish(DWORD code) {
    if(g_log!=INVALID_HANDLE_VALUE){FlushFileBuffers(g_log);CloseHandle(g_log);}
    ExitProcess(code);
}
static BOOL Folder(WCHAR* path) {
    DWORD n=GetModuleFileNameW(0,path,MAX_PATH),last=0;
    if(!n||n>=MAX_PATH)return FALSE;
    for(DWORD i=0;i<n;++i)if(path[i]==L'\\')last=i+1;
    if(!last)return FALSE;path[last]=0;
    return last<=170;
}
static DWORD WINAPI Deadline(LPVOID) {Sleep(g_deadline);ExitProcess(124);return 0;}

typedef HANDLE(WINAPI *SnapshotFn)(DWORD,DWORD);
typedef BOOL(WINAPI *StepFn)(HANDLE,LPPROCESSENTRY32);
typedef BOOL(WINAPI *CloseSnapshotFn)(HANDLE);
static SnapshotFn g_snapshot=0;
static StepFn g_first=0,g_next=0;
static CloseSnapshotFn g_closeSnapshot=0;

// Exact basename only; caller handles each lookup status within its own job.
static DWORD FindTarget(const WCHAR* name,DWORD* pid,DWORD* error) {
    *pid=0;*error=0;
    HANDLE s=g_snapshot(TH32CS_SNAPPROCESS,0);
    if(!s||s==INVALID_HANDLE_VALUE){*error=GetLastError();return 2;}
    PROCESSENTRY32 p={0};p.dwSize=sizeof(p);
    BOOL ok=g_first(s,&p);DWORD count=0,matches=0,status=0;
    while(ok){
        if(++count>4096){status=2;break;}
        p.szExeFile[MAX_PATH-1]=0;const WCHAR* leaf=p.szExeFile;
        for(const WCHAR* q=leaf;*q;++q)if(*q==L'\\'||*q==L'/')leaf=q+1;
        if(!lstrcmpiW(leaf,name)){++matches;*pid=p.th32ProcessID;}
        p.dwSize=sizeof(p);ok=g_next(s,&p);
    }
    if(!status && GetLastError()!=ERROR_NO_MORE_FILES){*error=GetLastError();status=2;}
    if(!g_closeSnapshot(s)){*error=GetLastError();status=2;}
    if(status)return status;
    if(matches>1)return 3;
    if(!matches)return 1;
    if(!*pid||*pid==GetCurrentProcessId())return 4;
    return 0;
}
// Each job keeps its own handle, retry clock and observation deadline.
// No long WaitForSingleObject: another pending job must not gate this one.
struct JobState {
    HANDLE process;DWORD pid,deadline,next,observeUntil,attempts;
    BOOL started,done,accepted;DWORD result;
};
static JobState g_state[MAX_PROCESS_JOBS];
static void CompleteJob(DWORD index,DWORD result,const WCHAR* reason,DWORD error=0) {
    JobState* state=&g_state[index];WCHAR line[240];
    state->done=TRUE;state->result=result;
    wsprintfW(line,L"JOB_END job=%lu target=%s pid=%lu",index+1,g_jobs[index].name,state->pid);Note(line);
    wsprintfW(line,L"%s result=%lu error=%lu",reason,result,error);Note(line);
    if(state->process){CloseHandle(state->process);state->process=0;}
}
static void StepJob(DWORD index) {
    JobState* state=&g_state[index];ProcessJob* job=&g_jobs[index];WCHAR line[240];
    DWORD now=GetTickCount()-g_begin;
    if(state->done||now<job->atMs)return;
    if(!state->started){
        state->started=TRUE;state->deadline=now+job->waitMs;state->next=now;
        wsprintfW(line,L"JOB_START job=%lu target=%s actual_ms=%lu",index+1,job->name,now);Note(line);
    }
    if(state->process){
        DWORD wait=WaitForSingleObject(state->process,0);
        if(wait==WAIT_OBJECT_0){CompleteJob(index,0,L"TARGET_EXIT_CONFIRMED");return;}
        if(wait!=WAIT_TIMEOUT){CompleteJob(index,7,L"EXIT_CHECK_FAILED",GetLastError());return;}
        if(state->attempts && now>=state->observeUntil){CompleteJob(index,9,L"EXIT_NOT_CONFIRMED_TIMEOUT");return;}
        // Successful requests are never repeated. Failed requests retry only
        // within the configured window; then observe the SAME handle for 60s.
        if(state->accepted || (state->attempts && now>state->deadline))return;
    }
    if(now<state->next)return;
    DWORD pid=0,error=0,status=FindTarget(job->name,&pid,&error);
    wsprintfW(line,L"LOOKUP job=%lu status=%lu pid=%lu",index+1,status,pid);Note(line);
    if(status==3||status==4){CompleteJob(index,status,L"TARGET_IDENTITY_REJECTED",error);return;}
    if(!status){
        if(state->process && pid!=state->pid){CompleteJob(index,6,L"TARGET_IDENTITY_CHANGED");return;}
        if(!state->process){
            state->process=OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,pid);
            if(!state->process){error=GetLastError();status=5;}
            else state->pid=pid;
        }
        if(!status){
            DWORD current=0;status=FindTarget(job->name,&current,&error);
            if(!status && current!=state->pid)status=6;
            if(status==3||status==4||status==6){CompleteJob(index,status,L"TARGET_IDENTITY_CHANGED",error);return;}
            if(!status){
                DWORD wait=WaitForSingleObject(state->process,0);
                if(wait==WAIT_OBJECT_0){CompleteJob(index,0,L"TARGET_EXIT_CONFIRMED");return;}
                if(wait!=WAIT_TIMEOUT){CompleteJob(index,7,L"EXIT_CHECK_FAILED",GetLastError());return;}
                wsprintfW(line,L"KILL_ATTEMPT job=%lu attempt=%lu pid=%lu",index+1,++state->attempts,state->pid);Note(line);
                if(!Commit())Finish(17);
                BOOL accepted=TerminateProcess(state->process,0);error=accepted?0:GetLastError();
                state->accepted=accepted;
                state->observeUntil=GetTickCount()-g_begin+60000;
                wsprintfW(line,L"KILL_REQUEST job=%lu accepted=%lu error=%lu",index+1,accepted,error);Note(line);
                // Regardless of API return, later polls decide whether it exited.
                if(WaitForSingleObject(state->process,0)==WAIT_OBJECT_0){CompleteJob(index,0,L"TARGET_EXIT_CONFIRMED");return;}
            }
        }
    }
    now=GetTickCount()-g_begin;
    if(!state->attempts && now>=state->deadline){CompleteJob(index,status?status:1,L"TARGET_PREPARATION_TIMEOUT",error);return;}
    state->next=now+job->retryMs;
    if(state->next>state->deadline && now<state->deadline)state->next=state->deadline;
}
extern "C" void ProcessOpsEntry() {
    g_begin=GetTickCount();
    HANDLE singleton=CreateMutexW(0,FALSE,L"MRN_PROCESSOPS_V1_SINGLETON");
    if(!singleton||GetLastError()==ERROR_ALREADY_EXISTS)ExitProcess(10);
    WCHAR folder[MAX_PATH],path[MAX_PATH],line[240];
    if(!Folder(folder))ExitProcess(11);
    DWORD run;
    for(run=1;run<=9999;++run){
        wsprintfW(path,L"%sprocess_ops_%04lu.txt",folder,run);
        g_log=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);
        if(g_log!=INVALID_HANDLE_VALUE)break;
        DWORD e=GetLastError();if(e!=ERROR_FILE_EXISTS&&e!=ERROR_ALREADY_EXISTS)ExitProcess(12);
    }
    if(run>9999)ExitProcess(13);
    WCHAR bom=0xfeff;DWORD bytes=0;
    if(!WriteFile(g_log,&bom,2,&bytes,0)||bytes!=2)Finish(14);
    Note(L"START WINCE_PROCESSOPS_V1_3_PORTABLE; times relative to this process entry; no audio operations");
    wsprintfW(line,L"CONFIG_LIMIT_JOBS_BYTES jobs=%lu bytes=%lu",MAX_PROCESS_JOBS,MAX_PROCESS_CONFIG_BYTES);Note(line);
    wsprintfW(path,L"%sprocessops.txt",folder);
    HANDLE config=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    if(config==INVALID_HANDLE_VALUE){Note(L"ABORT_CONFIG_MISSING_OR_UNREADABLE");Finish(15);}
    DWORD high=0,size=GetFileSize(config,&high),count=0;
    BOOL readOK=!high&&size>0&&size<=sizeof(g_config)&&ReadFile(config,g_config,size,&bytes,0)&&bytes==size;
    CloseHandle(config);
    if(high||size>sizeof(g_config))Note(L"CONFIG_TOO_LARGE_NO_KILL");
    if(!readOK||!ParseProcessConfig(g_config,size,g_jobs,&count)){Note(L"ABORT_INVALID_CONFIG_NO_KILL");Finish(16);}
    Note(L"CONFIG_VALIDATED");
    for(DWORD i=0;i<count;++i){
        wsprintfW(line,L"PLAN job=%lu at_ms=%lu target=%s",i+1,g_jobs[i].atMs,g_jobs[i].name);Note(line);
    }
    if(!Commit())Finish(17);
    HMODULE library=LoadLibraryW(L"toolhelp.dll");
    if(library){
        g_snapshot=(SnapshotFn)GetProcAddress(library,L"CreateToolhelp32Snapshot");
        g_first=(StepFn)GetProcAddress(library,L"Process32First");
        g_next=(StepFn)GetProcAddress(library,L"Process32Next");
        g_closeSnapshot=(CloseSnapshotFn)GetProcAddress(library,L"CloseToolhelp32Snapshot");
    }
    if(!g_snapshot||!g_first||!g_next||!g_closeSnapshot){Note(L"ABORT_TOOLHELP_UNAVAILABLE");Finish(18);}
    g_deadline=g_jobs[count-1].atMs+120000;
    for(DWORD d=0;d<count;++d)g_deadline+=g_jobs[d].waitMs+g_jobs[d].retryMs+5000;
    HANDLE guard=CreateThread(0,0,Deadline,0,0,0);
    if(!guard){Note(L"ABORT_NO_DEADLINE");Finish(19);}CloseHandle(guard);
    DWORD failures=0;
    for(;;){
        BOOL allDone=TRUE;
        for(DWORD j=0;j<count;++j){StepJob(j);if(!g_state[j].done)allDone=FALSE;}
        if(!Commit())Finish(17);
        if(allDone)break;
        Sleep(50);
    }
    for(DWORD k=0;k<count;++k)if(g_state[k].result)++failures;
    wsprintfW(line,L"PROCESSOPS_FINISHED jobs=%lu failures=%lu",count,failures);Note(line);
    if(!failures)Note(L"PROCESSOPS_COMPLETE; all configured exits confirmed");
    FreeLibrary(library);if(!Commit())Finish(17);Finish(failures?23:0);
}
