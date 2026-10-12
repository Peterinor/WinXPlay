#include <windows.h>
#include <tlhelp32.h>
#include "config_policy.h"

static HANDLE g_log=INVALID_HANDLE_VALUE;
static DWORD g_begin=0;
static volatile DWORD g_progress=0;
static BOOL g_logOK=TRUE;
static FileJob g_jobs[MAX_JOBS];
static void Note(const WCHAR* text);
#include "progress_window.h"
static void Note(const WCHAR* text){
    WCHAR prefix[40];wsprintfW(prefix,L"[%lu] ",GetTickCount()-g_begin);
    UiAppend(prefix,text);
    const WCHAR* parts[]={prefix,text,L"\r\n"};
    for(int i=0;i<3;++i){DWORD n=0,size=lstrlenW(parts[i])*2;if(!WriteFile(g_log,parts[i],size,&n,0)||n!=size)g_logOK=FALSE;}
    if(!FlushFileBuffers(g_log))g_logOK=FALSE;
}
static void Value(const WCHAR* label,DWORD a,DWORD b=0){WCHAR text[160];wsprintfW(text,L"%s a=%lu b=%lu",label,a,b);Note(text);}
static DWORD WINAPI Deadline(LPVOID){
    for(;;){Sleep(1000);if(GetTickCount()-g_progress>=240000)ExitProcess(124);}
    return 0;
}
static BOOL Exists(const WCHAR* path,BOOL* exists){
    DWORD attr=GetFileAttributesW(path);if(attr!=INVALID_FILE_ATTRIBUTES){*exists=TRUE;return !(attr&FILE_ATTRIBUTE_DIRECTORY);}
    DWORD e=GetLastError();*exists=FALSE;return e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND;
}
static BOOL Same(const WCHAR* one,const WCHAR* two){
    HANDLE a=CreateFileW(one,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);
    if(a==INVALID_HANDLE_VALUE)return FALSE;
    HANDLE b=CreateFileW(two,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
    if(b==INVALID_HANDLE_VALUE){CloseHandle(a);return FALSE;}
    DWORD ah=0,bh=0,as=GetFileSize(a,&ah),bs=GetFileSize(b,&bh),off=0;
    BOOL ok=ah==0&&bh==0&&as!=INVALID_FILE_SIZE&&bs==as;
    BYTE ab[1024],bb[1024];
    while(ok&&off<as){DWORD size=as-off;if(size>1024)size=1024;DWORD ar=0,br=0;
        if(!ReadFile(a,ab,size,&ar,0)||!ReadFile(b,bb,size,&br,0)||ar!=size||br!=size){ok=FALSE;break;}
        for(DWORD i=0;i<size;++i)if(ab[i]!=bb[i]){ok=FALSE;break;}off+=size;}
    if(!CloseHandle(a))ok=FALSE;if(!CloseHandle(b))ok=FALSE;return ok;
}
static BOOL CopyChecked(const WCHAR* source,const WCHAR* target,BOOL failIfExists=TRUE){
    Note(source);Note(target);
    if(!g_logOK)return FALSE;
    if(!CopyFileW(source,target,failIfExists)){Value(L"COPY_ERROR",GetLastError());return FALSE;}
    BOOL ok=Same(source,target);Note(ok?L"BYTE_COMPARE_PASS":L"BYTE_COMPARE_FAIL");return ok;
}
static BOOL Parents(const WCHAR* path){
    WCHAR part[MAX_PATH];lstrcpyW(part,path);
    // First-level directory/volume must already exist; only create descendants.
    BOOL first=TRUE;
    for(DWORD i=1;part[i];++i)if(part[i]==L'\\'){
        part[i]=0;DWORD attr=GetFileAttributesW(part);
        if(attr==INVALID_FILE_ATTRIBUTES){DWORD error=GetLastError();
            if(first||(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)||!CreateDirectoryW(part,0))return FALSE;
        }else if(!(attr&FILE_ATTRIBUTE_DIRECTORY))return FALSE;
        part[i]=L'\\';first=FALSE;
    }return TRUE;
}
static BOOL NotRunning(const WCHAR* target){
    // Conservative basename check: a matching process blocks replacement.
    const WCHAR* leaf=target;for(const WCHAR* s=target;*s;++s)if(*s==L'\\')leaf=s+1;
    HMODULE module=LoadLibraryW(L"toolhelp.dll");if(!module)return FALSE;
    typedef HANDLE(WINAPI *Snap)(DWORD,DWORD);typedef BOOL(WINAPI *Step)(HANDLE,LPPROCESSENTRY32);typedef BOOL(WINAPI *Close)(HANDLE);
    Snap snap=(Snap)GetProcAddress(module,L"CreateToolhelp32Snapshot");Step first=(Step)GetProcAddress(module,L"Process32First"),next=(Step)GetProcAddress(module,L"Process32Next");Close close=(Close)GetProcAddress(module,L"CloseToolhelp32Snapshot");
    BOOL clear=FALSE;
    if(snap&&first&&next&&close){HANDLE h=snap(TH32CS_SNAPPROCESS,0);
        if(h&&h!=INVALID_HANDLE_VALUE){PROCESSENTRY32 e={0};e.dwSize=sizeof(e);BOOL ok=first(h,&e);DWORD count=0;
            if(ok){clear=TRUE;while(ok&&count++<256){e.szExeFile[MAX_PATH-1]=0;const WCHAR* name=e.szExeFile;
                    for(const WCHAR* s=name;*s;++s)if(*s==L'\\')name=s+1;
                    if(Equal(name,leaf)){clear=FALSE;break;}e.dwSize=sizeof(e);ok=next(h,&e);}
                if(ok||GetLastError()!=ERROR_NO_MORE_FILES)clear=FALSE;}
            close(h);}}
    FreeLibrary(module);return clear;
}
// Return 0 committed, 1 backup rename failed, 2 commit failed/restored,
// 3 commit and restoration both failed (backup path remains in the log).
extern "C" __declspec(dllexport) DWORD ReplaceStaged(const WCHAR* target,const WCHAR* staged,const WCHAR* backup,DWORD existed){
    if(existed&&!MoveFileW(target,backup))return 1;
    if(MoveFileW(staged,target))return 0;
    if(existed&&!MoveFileW(backup,target))return 3;
    return 2;
}
static BOOL Install(const FileJob& job,DWORD index){
    Note(L"INSTALL_BEGIN");Note(job.target);
    Value(L"INSTALL_TARGET_ATTRIBUTES",GetFileAttributesW(job.target));
    if(!g_logOK)return FALSE;
    BOOL sourceExists=FALSE;
    if(!Exists(job.source,&sourceExists)||!sourceExists){Note(L"INSTALL_SOURCE_MISSING_AT_THIS_STEP");return FALSE;}
    BOOL exists=FALSE;if(!Exists(job.target,&exists))return FALSE;
    if(exists&&Same(job.source,job.target)){Note(L"INSTALL_ALREADY_IDENTICAL");return TRUE;}
    if(!NotRunning(job.target)){Note(L"INSTALL_BLOCKED_RUNNING_OR_SNAPSHOT_ERROR");return FALSE;}
    if(!Parents(job.target)){Value(L"DIRECTORY_ERROR",GetLastError());return FALSE;}
    WCHAR staging[MAX_PATH],backup[MAX_PATH];
    wsprintfW(staging,L"%s.new-%lu-%lu",job.target,g_begin,index);
    wsprintfW(backup,L"%s.bak-%lu-%lu",job.target,g_begin,index);
    BOOL old=FALSE;if(!Exists(staging,&old)||old||!Exists(backup,&old)||old){Note(L"INSTALL_STALE_TEMP_NO_OVERWRITE");return FALSE;}
    if(!CopyChecked(job.source,staging))return FALSE;
    if(!g_logOK)return FALSE;
    if(exists){Note(L"BACKUP_PATH");Note(backup);}
    if(!g_logOK)return FALSE;
    // Same-volume rename; this sequence is recoverable, not power-loss atomic.
    DWORD replace=ReplaceStaged(job.target,staging,backup,exists);
    if(replace){DWORD error=GetLastError();Value(L"INSTALL_REPLACE_FAILED_STATUS_ERROR",replace,error);return FALSE;}
    if(!Same(job.source,job.target)){
        BOOL moved=MoveFileW(job.target,staging);BOOL restored=moved&&(!exists||MoveFileW(backup,job.target));
        Value(L"INSTALL_VERIFY_FAIL_ROLLBACK",restored);return FALSE;
    }
    Note(L"INSTALL_PASS_BACKUP_RETAINED_IF_PRESENT");return TRUE;
}
// Used for MOVE and DELETE's retained backup. Never overwrites the destination.
// 0 success; 1 source invalid; 2 destination exists/invalid; 3 copy failed;
// 4 byte comparison failed; 5 removal failed; 6 source absence unconfirmed.
extern "C" __declspec(dllexport) DWORD TransferThenRemove(const WCHAR* source,const WCHAR* target){
    BOOL exists=FALSE;
    if(!Exists(source,&exists)||!exists)return 1;
    if(!Exists(target,&exists)||exists)return 2;
    if(!CopyFileW(source,target,TRUE))return 3;
    if(!Same(source,target))return 4;
    if(!DeleteFileW(source))return 5;
    if(!Exists(source,&exists)||exists)return 6;
    return 0;
}
static BOOL DeleteOrMove(const FileJob& job,DWORD index,const WCHAR* folder){
    BOOL deleting=job.operation==OP_DELETE;
    Note(deleting?L"DELETE_BEGIN":L"MOVE_BEGIN");Note(job.source);
    Value(L"FILE_SOURCE_ATTRIBUTES",GetFileAttributesW(job.source));
    if(!g_logOK)return FALSE;
    if(!SafePath(job.source,3)||(!deleting&&!SafePath(job.target,3)))return FALSE;
    if(!NotRunning(job.source)||(!deleting&&!NotRunning(job.target))){
        Note(L"FILE_CHANGE_BLOCKED_RUNNING_OR_SNAPSHOT_ERROR");return FALSE;
    }
    WCHAR target[MAX_PATH];
    if(deleting){wsprintfW(target,L"%s\\deleted-%lu.bak",folder,index);Note(L"DELETE_BACKUP_PATH");}
    else {lstrcpyW(target,job.target);if(!Parents(target)){Note(L"MOVE_PARENT_ERROR");return FALSE;}}
    Note(target);
    // A durable log contains both recovery paths before copying or deleting.
    if(!g_logOK)return FALSE;
    DWORD result=TransferThenRemove(job.source,target);
    DWORD error=(result==3||result==5||result==6)?GetLastError():0;
    Value(L"TRANSFER_REMOVE_STATUS",result);Value(L"LAST_FILE_API_ERROR",error);
    if(result){Note(L"FILE_CHANGE_FAILED_PRESERVE_SOURCE_AND_DESTINATION_AS_FOUND");return FALSE;}
    Note(deleting?L"DELETE_PASS_BACKUP_RETAINED":L"MOVE_PASS_BYTES_VERIFIED_SOURCE_ABSENT");
    return g_logOK;
}
static BOOL WatchOne(const FileJob& job,DWORD index,const WCHAR* folder){
    DWORD start=GetTickCount(),sample=0,successes=0;WCHAR path[MAX_PATH];
    while(g_logOK&&GetTickCount()-start<180000){
        g_progress=GetTickCount();
        Value(L"WATCH_SAMPLE_JOB",sample,index+1);
        wsprintfW(path,L"%s\\%lu_%04lu_%s",folder,index+1,sample,job.target);
        if(CopyChecked(job.source,path)){++successes;Note(L"WATCH_SNAPSHOT_PASS");}
        else Note(L"WATCH_RETRY_NEXT_SAMPLE");
        ++sample;if(g_logOK)Sleep(5000);
    }
    Value(L"WATCH_SAMPLES_SUCCESSES",sample,successes);
    if(!successes)Note(L"WATCH_NO_VALID_SNAPSHOT_FAILED");
    return g_logOK&&successes>0;
}
static BOOL RunExecutable(const WCHAR* path){
    Note(L"RUN_BEGIN");Note(path);
    if(!g_logOK)return FALSE;
    BOOL exists=FALSE;
    if(!Exists(path,&exists)||!exists){Note(L"RUN_EXE_MISSING_OR_INVALID");return FALSE;}
    PROCESS_INFORMATION process={0};
    if(!CreateProcessW(path,0,0,0,FALSE,0,0,0,0,&process)){
        Value(L"RUN_CREATE_PROCESS_ERROR",GetLastError());return FALSE;
    }
    Value(L"RUN_PROCESS_CREATED_PID",process.dwProcessId);
    if(process.hThread)CloseHandle(process.hThread);
    if(process.hProcess)CloseHandle(process.hProcess);
    // The child owns its lifetime. Creation does not imply application readiness.
    return g_logOK;
}
// Sequential operations; RUN completes when the OS creates the child process.
extern "C" __declspec(dllexport) BOOL ExecuteFileJobs(const FileJob* jobs,DWORD count,const WCHAR* folder){
    HANDLE bLock=0,rLock=0;BOOL allOK=TRUE,installBatchOK=TRUE;
    DWORD succeeded=0,failed=0;
    for(DWORD i=0;i<count;++i){
        g_progress=GetTickCount();const FileJob& job=jobs[i];
        UiJob(i+1,count,job);
        BOOL change=job.operation==OP_INSTALL||job.operation==OP_DELETE||job.operation==OP_MOVE||
            (job.operation==OP_COPY&&job.target[0]==L'\\');
        if(!change){if(bLock)CloseHandle(bLock);if(rLock)CloseHandle(rLock);bLock=rLock=0;}
        Value(L"JOB_BEGIN_INDEX_OPERATION",i+1,job.operation);Note(job.source);Note(job.target);
        if(!g_logOK){allOK=FALSE;break;}
        if(job.operation==OP_INSTALL&&(i==0||jobs[i-1].operation!=OP_INSTALL))installBatchOK=TRUE;
        BOOL allowed=TRUE;
        if(change&&g_legacyGuards&&!bLock){
            bLock=CreateMutexW(0,FALSE,L"MRN_BAND_TEST_SINGLETON_V1");DWORD be=GetLastError();
            rLock=CreateMutexW(0,FALSE,L"MRN_B_DISPLAY_RECOVERY_V1");DWORD re=GetLastError();
            if(!bLock||!rLock||be==ERROR_ALREADY_EXISTS||re==ERROR_ALREADY_EXISTS){
                Note(L"FILE_CHANGE_BLOCKED_B_OR_HELPER_ACTIVE");allowed=FALSE;
                if(bLock)CloseHandle(bLock);if(rLock)CloseHandle(rLock);bLock=rLock=0;
            }
        }
        BOOL ok=FALSE;WCHAR path[MAX_PATH];
        if(!allowed){} // This job is blocked; later jobs still get their own checks.
        else if(job.operation==OP_INSTALL)ok=Install(job,i);
        else if(job.operation==OP_DELETE||job.operation==OP_MOVE)ok=DeleteOrMove(job,i,folder);
        else if(job.operation==OP_COPY){
            if(job.target[0]==L'\\'){
                if(NotRunning(job.target)&&Parents(job.target))ok=CopyChecked(job.source,job.target,FALSE);
                else Note(L"COPY_TARGET_RUNNING_OR_PARENT_UNAVAILABLE");
            }else{wsprintfW(path,L"%s\\%s",folder,job.target);ok=CopyChecked(job.source,path,FALSE);}
        }else if(job.operation==OP_WATCH)ok=WatchOne(job,i,folder);
        else if(job.operation==OP_RUN)ok=RunExecutable(job.source);
        if(!ok||!g_logOK){
            Value(L"JOB_FAILED_CONTINUE_INDEX_OPERATION",i+1,job.operation);allOK=FALSE;++failed;
            if(job.operation==OP_INSTALL)installBatchOK=FALSE;
        }else {Value(L"JOB_COMPLETE_INDEX_OPERATION",i+1,job.operation);++succeeded;}
        if(!g_logOK)break; // Cannot record further operations when the log device fails.
        // Retain the existing B-package marker after its consecutive INSTALL jobs.
        if(job.operation==OP_INSTALL&&(i+1==count||jobs[i+1].operation!=OP_INSTALL))
            Note(installBatchOK?L"INSTALL_PHASE_DONE_BAND_MAY_BE_PRESSED":L"INSTALL_PHASE_FINISHED_WITH_FAILURES");
    }
    if(bLock)CloseHandle(bLock);if(rLock)CloseHandle(rLock);
    Value(L"FILEOPS_SUCCESS_FAILED",succeeded,failed);
    return allOK&&g_logOK;
}
extern "C" void FileOpsEntry(){
    g_begin=GetTickCount();g_progress=g_begin;HANDLE single=CreateMutexW(0,FALSE,L"MRN_FILEOPS_V1_SINGLETON");
    if(!single||GetLastError()==ERROR_ALREADY_EXISTS)ExitProcess(10);
    WCHAR exe[MAX_PATH];DWORD length=GetModuleFileNameW(0,exe,MAX_PATH);
    if(!length||length>=MAX_PATH)ExitProcess(11);
    lstrcpyW(g_toolPath,exe);WCHAR base[MAX_PATH];DWORD last=0;
    for(DWORD j=0;j<length;++j)if(exe[j]==L'\\')last=j+1;
    if(!last||last>160)ExitProcess(11);
    for(DWORD k=0;k<last;++k)base[k]=exe[k];base[last]=0;
    wsprintfW(g_configPath,L"%sfileops.txt",base);
    WCHAR folder[MAX_PATH],path[MAX_PATH];BOOL made=FALSE;
    for(DWORD i=1;i<=9999;++i){wsprintfW(folder,L"%sFILEOPS_%04lu",base,i);if(CreateDirectoryW(folder,0)){made=TRUE;break;}
        DWORD e=GetLastError();if(e!=ERROR_ALREADY_EXISTS&&e!=ERROR_FILE_EXISTS)ExitProcess(12);}
    if(!made)ExitProcess(13);wsprintfW(path,L"%s\\RESULT.txt",folder);
    g_log=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);if(g_log==INVALID_HANDLE_VALUE)ExitProcess(14);
    WCHAR bom=0xfeff;DWORD written=0;if(!WriteFile(g_log,&bom,2,&written,0)||written!=2)ExitProcess(15);
    g_uiThread=UiStart();
    Note(L"START WINCE_FILEOPS_V2_9_PORTABLE");Value(L"PID_UPTIME",GetCurrentProcessId(),g_begin);
    Note(g_uiThread?L"UI_THREAD_REQUESTED_VISIBILITY_REQUIRES_DEVICE_CHECK":L"UI_THREAD_FAILED_FILE_LOG_ONLY");
    Note(L"RESULT_LOG_PATH");Note(path);
    HANDLE guard=CreateThread(0,0,Deadline,0,0,0);if(!guard)FinishFileOps(16);CloseHandle(guard);
    wsprintfW(path,L"%sfileops.txt",base);
    HANDLE config=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
    if(config==INVALID_HANDLE_VALUE){Value(L"CONFIG_OPEN_ERROR",GetLastError());FinishFileOps(17);}
    static char text[MAX_CONFIG_BYTES+4]={0};DWORD high=0,size=GetFileSize(config,&high),got=0,count=0;
    Value(L"CONFIG_LIMIT_JOBS_BYTES",MAX_JOBS,MAX_CONFIG_BYTES);
    BOOL readOK=high==0&&size>0&&size<=MAX_CONFIG_BYTES&&ReadFile(config,text,size,&got,0)&&got==size;CloseHandle(config);
    if(high||size>MAX_CONFIG_BYTES)Note(L"CONFIG_TOO_LARGE_NO_INTERNAL_WRITES");
    for(DWORD c=0;readOK&&c<size;++c)if(text[c]==0)readOK=FALSE;
    if(!readOK||!ParseFileConfig(text,g_jobs,&count)){Note(L"CONFIG_REJECTED_NO_INTERNAL_WRITES");FinishFileOps(18);}
    Value(L"CONFIG_ACCEPTED_JOBS",count);
    for(DWORD i=0;i<count;++i){
        const FileJob& job=g_jobs[i];BOOL allowed=TRUE;
        if(job.operation==OP_INSTALL||job.operation==OP_MOVE||(job.operation==OP_COPY&&job.target[0]==L'\\'))
            allowed=MutationPathAllowed(job.target,folder);
        if(job.operation==OP_DELETE||job.operation==OP_MOVE)allowed=allowed&&MutationPathAllowed(job.source,folder);
        if(!allowed){Note(L"ACTIVE_TOOL_OR_OUTPUT_PATH_REJECTED_NO_FILE_CHANGES");FinishFileOps(18);}
    }
    BOOL ok=ExecuteFileJobs(g_jobs,count,folder);
    Note(!g_logOK?L"FILEOPS_ABORTED_LOG_ERROR":ok?L"ALL_JOBS_COMPLETE":L"FILEOPS_FINISHED_WITH_FAILURES");
    Note(L"END_FILEOPS_REQUESTED");FinishFileOps(ok&&g_logOK?0:23);
}
