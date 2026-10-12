#include "../../platform/common/native_services.h"
using namespace cp;
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
    std::wstring folder=executableDirectory();Value report=Value::dictionary();
    bool installed=_wcsicmp(folder.c_str(),L"\\NAND_USER\\WinXPlay")==0;
    std::wstring exe=folder+L"\\WinXPlay.exe";
    PROCESS_INFORMATION process;ZeroMemory(&process,sizeof(process));
    try{
        if(installed){
            HANDLE guard=CreateMutexW(0,FALSE,L"MRN_BAND_TEST_SINGLETON_V1");
            require(guard!=0 && GetLastError()!=ERROR_ALREADY_EXISTS,"Installed test guard unavailable");
            PROCESS_INFORMATION blocked;ZeroMemory(&blocked,sizeof(blocked));
            BOOL launched=CreateProcessW(exe.c_str(),0,0,0,FALSE,0,0,0,0,&blocked);
            DWORD waited=launched?WaitForSingleObject(blocked.hProcess,10000):WAIT_FAILED;
            if(launched){CloseHandle(blocked.hThread);CloseHandle(blocked.hProcess);}CloseHandle(guard);
            require(launched && waited==WAIT_OBJECT_0 && FindWindowW(L"WinXPlay",0)==0,
                    "Installed receiver ignored FILEOPS install guard");
        }
        // Exercise the production window/timer/worker, with no USB device present.
        require(CreateProcessW(exe.c_str(),0,0,0,FALSE,0,0,0,0,&process)!=FALSE,"Receiver launch failed");
        DWORD began=GetTickCount();Value state=Value::dictionary();bool retried=false;
        while(DWORD(GetTickCount()-began)<30000){
            Sleep(100);
            try{state=parsePlist(readFile(folder+L"/auto-connect.plist"));
                retried=state.at("attempts").integer>=3 && state.at("automatic").boolean && state.at("retry_pending").boolean;
            }catch(...){}
            if(retried)break;
        }
        require(retried,"Production receiver did not automatically retry three times");
        require(state.at("retry_delay_ms").integer==3000,"Unexpected retry delay");
        HWND window=FindWindowW(L"WinXPlay",0);require(window!=0,"Receiver window missing");
        if(installed){
            HANDLE guard=CreateMutexW(0,FALSE,L"MRN_BAND_TEST_SINGLETON_V1");
            bool protectedUpdate=guard && GetLastError()==ERROR_ALREADY_EXISTS;
            if(guard)CloseHandle(guard);require(protectedUpdate,"Running receiver did not protect Installed files");
            PROCESS_INFORMATION duplicate;ZeroMemory(&duplicate,sizeof(duplicate));
            require(CreateProcessW(exe.c_str(),0,0,0,FALSE,0,0,0,0,&duplicate)!=FALSE,"Repeat Installed launch failed");
            DWORD waited=WaitForSingleObject(duplicate.hProcess,10000);
            CloseHandle(duplicate.hThread);CloseHandle(duplicate.hProcess);
            require(waited==WAIT_OBJECT_0 && FindWindowW(L"WinXPlay",0)==window,"Repeat Installed created a second receiver");
        }
        PostMessageW(window,WM_CLOSE,0,0);
        require(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0,"Receiver failed to stop after close");
        state=parsePlist(readFile(folder+L"/auto-connect.plist"));
        require(!state.at("automatic").boolean && !state.at("retry_pending").boolean,"Closed receiver still scheduled retries");
        report.dict["passed"]=Value::flag(true);report.dict["attempts"]=state.at("attempts");
    }catch(const std::exception &e){report.dict["passed"]=Value::flag(false);report.dict["error"]=Value::str(e.what());
        if(process.hProcess)TerminateProcess(process.hProcess,1);
    }
    if(process.hThread)CloseHandle(process.hThread);if(process.hProcess)CloseHandle(process.hProcess);
    report.dict["physical_replug_verified"]=Value::flag(false);
    report.dict["installed_entry_tested"]=Value::flag(installed);
    writeFile(folder+L"/reconnect-smoke.plist",xmlPlist(report));return report.at("passed").boolean?0:1;
}
